// SPDX-License-Identifier: MIT
// Frames use tightly packed NV12 (one luma plane, then interleaved half-size UV).
#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <optional>
#include <thread>
#include <vector>

namespace motion {
using Byte = std::uint8_t;
struct Frame {
    int width, height;
    std::vector<Byte> pixels;
    Frame(int w, int h) : width(w), height(h), pixels(std::size_t(w) * h * 3 / 2) {}
};
struct Vector { float x = 0, y = 0, confidence = 0; };
constexpr int tile = 12;

inline int pixel(const Frame& f, int x, int y) {
    return f.pixels[std::clamp(y, 0, f.height-1)*f.width + std::clamp(x, 0, f.width-1)];
}

// 0 uses every hardware thread. Tests set 1 to compare against the serial result.
inline int& threadLimit() { static int limit = 0; return limit; }

// Runs body(row) for every row, spread over the available cores. Each row
// writes only its own output, so the result does not depend on the thread count.
template <class Body> void parallelRows(int rows, const Body& body) {
    int threads = threadLimit() > 0 ? threadLimit() : int(std::thread::hardware_concurrency());
    threads = std::clamp(std::min(threads, rows / 4), 1, 64);
    std::atomic<int> next{0};
    auto work = [&] { for (int r; (r = next.fetch_add(1)) < rows;) body(r); };
    std::vector<std::thread> pool;
    for (int t = 1; t < threads; ++t) {
        // If the system refuses another thread, the threads already started
        // and this one finish the remaining rows.
        try { pool.emplace_back(work); } catch (...) { break; }
    }
    work();
    for (auto& t : pool) t.join();
}

// A histogram discontinuity suppresses interpolation across many hard cuts.
inline bool sceneCut(const Frame& a, const Frame& b) {
    int bins[32] = {}, count = 0;
    for (int y = 0; y < a.height; y += 4) {
        for (int x = 0; x < a.width; x += 4) {
            ++bins[pixel(a,x,y)/8]; --bins[pixel(b,x,y)/8]; ++count;
        }
    }
    int difference = 0;
    for (int n : bins) difference += std::abs(n);
    return difference > count * 0.90;
}

// Luma at full, half, quarter and eighth resolution (2x2 box filter). Coarse
// levels let the search find motion far beyond the fine search window.
struct Pyramid {
    struct Level {
        int width, height;
        const Byte* data;
        int at(int x, int y) const {
            return data[std::clamp(y, 0, height-1)*width + std::clamp(x, 0, width-1)];
        }
    };
    std::vector<std::vector<Byte>> storage;
    std::vector<Level> levels;
    Pyramid(const Frame& f, int count) {
        levels.push_back({f.width, f.height, f.pixels.data()});
        for (int k = 1; k < count; ++k) {
            const Level& up = levels.back();
            const int w = up.width/2, h = up.height/2;
            storage.emplace_back(std::size_t(w)*h);
            Byte* out = storage.back().data();
            for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
                const Byte* p = up.data + 2*y*up.width + 2*x;
                out[y*w+x] = Byte((p[0] + p[1] + p[up.width] + p[up.width+1] + 2) / 4);
            }
            levels.push_back({w, h, out});
        }
    }
    // Every level must still hold a few tiles in each direction. Frames
    // smaller than 128x128 keep the single-level search.
    static int levelsFor(int width, int height) {
        int n = 1;
        while (n < 4 && (width >> n) >= 64 && (height >> n) >= 64) ++n;
        return n;
    }
};

struct Field {
    int cols, rows;
    std::vector<Vector> vectors;
    Field(const Frame& a, const Frame& b)
        : cols((a.width+tile-1)/tile), rows((a.height+tile-1)/tile), vectors(cols*rows) {
        if (&a == &b || sceneCut(a,b)) return;
        const int count = Pyramid::levelsFor(a.width, a.height);
        const Pyramid pa(a, count), pb(b, count);
        struct Grid { int cols, rows; std::vector<int> dx, dy; };
        std::vector<Grid> grids(count);
        for (int k = count-1; k >= 0; --k) {
            const auto& la = pa.levels[k];
            const auto& lb = pb.levels[k];
            Grid& g = grids[k];
            g.cols = (la.width+tile-1)/tile; g.rows = (la.height+tile-1)/tile;
            g.dx.assign(std::size_t(g.cols)*g.rows, 0); g.dy = g.dx;
            const Grid* parent = k+1 < count ? &grids[k+1] : nullptr;
            parallelRows(g.rows, [&](int row) {
                for (int col = 0; col < g.cols; ++col) {
                    const int cx = col*tile+tile/2, cy = row*tile+tile/2;
                    auto score = [&](int dx, int dy) {
                        int sad = 0;
                        for (int j=-5; j<=5; j+=2) for (int i=-5; i<=5; i+=2)
                            sad += std::abs(la.at(cx+i,cy+j) - lb.at(cx+i+dx,cy+j+dy));
                        return sad + std::abs(dx) + std::abs(dy);
                    };
                    int bx = 0, by = 0, best = score(0,0);
                    if (!best) continue;
                    auto refine = [&](std::initializer_list<int> steps, int firstRadius) {
                        for (int step : steps) {
                            const int ox = bx, oy = by;
                            const int radius = step == *steps.begin() ? firstRadius : 1;
                            for (int j=-radius; j<=radius; ++j) for (int i=-radius; i<=radius; ++i) {
                                const int dx = ox+i*step, dy = oy+j*step, cost = score(dx,dy);
                                if (cost < best) { bx = dx; by = dy; best = cost; }
                            }
                        }
                    };
                    // Coarse search plus local refinement. A small penalty breaks ties
                    // in flat patches in favour of the shortest displacement.
                    // This is the whole search at the coarsest level and a fallback
                    // candidate at finer levels, so no tile matches worse than it did
                    // with the single-level search.
                    refine({4,2,1}, 3);
                    if (parent) {
                        // Doubled vectors from the parent tile and its neighbours.
                        const int pc = std::clamp(int(std::floor((cx/2.f - tile/2)/tile + 0.5f)), 0, parent->cols-1);
                        const int pr = std::clamp(int(std::floor((cy/2.f - tile/2)/tile + 0.5f)), 0, parent->rows-1);
                        const int sx = bx, sy = by, sbest = best;
                        int px = 0, py = 0, pbest = score(0,0);
                        for (int j=-1; j<=1; ++j) for (int i=-1; i<=1; ++i) {
                            const int c = std::clamp(pc+i,0,parent->cols-1), r = std::clamp(pr+j,0,parent->rows-1);
                            const int dx = 2*parent->dx[r*parent->cols+c], dy = 2*parent->dy[r*parent->cols+c];
                            const int cost = score(dx,dy);
                            if (cost < pbest) { px = dx; py = dy; pbest = cost; }
                        }
                        bx = px; by = py; best = pbest;
                        refine({2,1}, 1);
                        if (sbest <= best) { bx = sx; by = sy; best = sbest; }
                    }
                    g.dx[row*g.cols+col] = bx; g.dy[row*g.cols+col] = by;
                    if (k == 0) {
                        // A parabola through the matching error on each side of the
                        // best whole-pixel vector places it between pixels, so slow
                        // motion blurs by its real length instead of a rounded one.
                        auto sad = [&](int dx, int dy) { return float(score(dx,dy) - std::abs(dx) - std::abs(dy)); };
                        auto vertex = [](float minus, float centre, float plus) {
                            const float curve = minus - 2*centre + plus;
                            return curve > 0 ? std::clamp(0.5f*(minus-plus)/curve, -0.5f, 0.5f) : 0.f;
                        };
                        const float c = sad(bx,by);
                        const float fx = vertex(sad(bx-1,by), c, sad(bx+1,by));
                        const float fy = vertex(sad(bx,by-1), c, sad(bx,by+1));
                        vectors[row*cols+col] = {bx+fx, by+fy, std::clamp(1.f-best/(36.f*28.f),0.f,1.f)};
                    }
                }
            });
        }
    }

    Vector at(float x, float y) const {
        float gx=std::clamp((x-tile/2)/tile,0.f,float(cols-1));
        float gy=std::clamp((y-tile/2)/tile,0.f,float(rows-1));
        int i=int(gx), j=int(gy);
        float fx=gx-i, fy=gy-j;
        Vector out;
        for (int v=0; v<=1; ++v) for (int u=0; u<=1; ++u) {
            const auto& p=vectors[std::min(j+v,rows-1)*cols+std::min(i+u,cols-1)];
            float weight=(u?fx:1-fx)*(v?fy:1-fy);
            out.x+=p.x*weight; out.y+=p.y*weight; out.confidence+=p.confidence*weight;
        }
        return out;
    }
};

// Bilinear sampling retains subpixel motion and keeps U and V in separate lanes.
inline float sample(const Frame& f, bool uv, int lane, float x, float y) {
    int scale=uv?2:1, w=f.width/scale, h=f.height/scale;
    x=std::clamp(x,0.f,float(w-1)); y=std::clamp(y,0.f,float(h-1));
    int ix=int(x), iy=int(y), jx=std::min(w-1,ix+1), jy=std::min(h-1,iy+1);
    const Byte* p=f.pixels.data()+(uv?f.width*f.height:0)+lane;
    auto read=[&](int i,int j){return p[j*f.width+i*scale];};
    float dx=x-ix, dy=y-iy;
    return (1-dy)*((1-dx)*read(ix,iy)+dx*read(jx,iy)) +
           dy*((1-dx)*read(ix,jy)+dx*read(jx,jy));
}

// Forward-backward consistency. A vector from A to B should land on a vector
// from B back to A that points the opposite way. Occlusions and false matches
// on repeating patterns break that, and blending along them draws double
// edges, so the confidence of inconsistent tiles falls to zero.
inline void crossCheck(Field& field, const Field& reverse) {
    for (int row=0; row<field.rows; ++row) for (int col=0; col<field.cols; ++col) {
        Vector& v=field.vectors[row*field.cols+col];
        if (v.confidence<=0) continue;
        const float cx=float(col*tile+tile/2), cy=float(row*tile+tile/2);
        const Vector r=reverse.at(cx+v.x,cy+v.y);
        const float error=std::hypot(v.x+r.x,v.y+r.y);
        const float tolerance=1.5f+0.1f*std::hypot(v.x,v.y);
        v.confidence*=std::clamp(1.f-(error-tolerance)/3.f,0.f,1.f);
    }
}

inline Frame blend(const Frame& prev, const Frame& cur, const Frame& next,
                   const Field& backward, const Field& forward, int samples, int angle) {
    Frame out=cur;
    for (int plane=0; plane<2; ++plane) {
        bool uv=plane==1;
        int scale=uv?2:1, offset=uv?cur.width*cur.height:0;
        parallelRows(cur.height/scale, [&](int y) {
            for (int x=0; x<cur.width/scale; ++x) {
                Vector fields[]={backward.at(float(x*scale),float(y*scale)),
                                 forward.at(float(x*scale),float(y*scale))};
                for (int lane=0; lane<scale; ++lane) {
                    int idx=offset+y*cur.width+x*scale+lane;
                    float center=cur.pixels[idx], sum=0;
                    for (int k=0; k<samples; ++k) {
                        float phase=((k+0.5f)/samples-0.5f)*(angle/360.f);
                        int side=phase>=0; float t=std::abs(phase);
                        const auto& v=fields[side];
                        if (v.confidence<=0) { sum+=center; continue; }
                        float dx=v.x/scale, dy=v.y/scale;
                        const Frame& other=side?next:prev;
                        float a=sample(cur,uv,lane,x-t*dx,y-t*dy);
                        float b=sample(other,uv,lane,x+(1-t)*dx,y+(1-t)*dy);
                        sum+=center+v.confidence*((1-t)*a+t*b-center);
                    }
                    out.pixels[idx]=Byte(std::clamp(std::lround(sum/samples),0L,255L));
                }
            }
        });
    }
    return out;
}

// Renders consecutive frames. Every field is computed once and used twice:
// as the motion of one frame and as the consistency check of its neighbour.
// Each call's cur and next must be the next call's prev and cur. Pass the
// same object as prev (or next) to clamp at the start (or end) of the footage.
class Exposer {
    int samples, angle;
    std::optional<Field> backward, reverseBackward;
public:
    Exposer(int samplesPerFrame, int shutterAngle) : samples(samplesPerFrame), angle(shutterAngle) {}
    Frame operator()(const Frame& prev, const Frame& cur, const Frame& next) {
        if (angle==0) return cur;
        Field b=backward ? std::move(*backward) : Field(cur,prev);
        Field rb=reverseBackward ? std::move(*reverseBackward) : Field(prev,cur);
        Field f(cur,next), rf(next,cur);
        backward=rf; reverseBackward=f;
        crossCheck(b,rb); crossCheck(f,rf);
        return blend(prev,cur,next,b,f,samples,angle);
    }
};

inline Frame expose(const Frame& prev, const Frame& cur, const Frame& next, int samples, int angle) {
    return Exposer(samples,angle)(prev,cur,next);
}
}
