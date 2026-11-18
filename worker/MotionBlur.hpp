// SPDX-License-Identifier: MIT
// Frames use tightly packed NV12 (one luma plane, then interleaved half-size UV).
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
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

struct Field {
    int cols, rows;
    std::vector<Vector> vectors;
    Field(const Frame& a, const Frame& b)
        : cols((a.width+tile-1)/tile), rows((a.height+tile-1)/tile), vectors(cols*rows) {
        if (&a == &b || sceneCut(a,b)) return;
        for (int row=0; row<rows; ++row) for (int col=0; col<cols; ++col) {
            int cx=col*tile+tile/2, cy=row*tile+tile/2;
            int bx=0, by=0;
            auto score = [&](int dx, int dy) {
                int sad=0;
                for (int j=-5; j<=5; j+=2) for (int i=-5; i<=5; i+=2)
                    sad += std::abs(pixel(a,cx+i,cy+j)-pixel(b,cx+i+dx,cy+j+dy));
                return sad + std::abs(dx) + std::abs(dy);
            };
            int best=score(0,0);
            if (!best) continue;
            // Coarse search plus local refinement. A small penalty breaks ties
            // in flat patches in favour of the shortest displacement.
            for (int step : {4,2,1}) {
                int ox=bx, oy=by, radius=step==4 ? 3 : 1;
                for (int j=-radius; j<=radius; ++j) for (int i=-radius; i<=radius; ++i) {
                    int dx=ox+i*step, dy=oy+j*step, cost=score(dx,dy);
                    if (cost < best) { bx=dx; by=dy; best=cost; }
                }
            }
            vectors[row*cols+col] = {float(bx),float(by),std::clamp(1.f-best/(36.f*28.f),0.f,1.f)};
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

inline Frame expose(const Frame& prev, const Frame& cur, const Frame& next, int samples, int angle) {
    Frame out=cur;
    if (angle==0) return out;
    Field backward(cur,prev), forward(cur,next);
    for (int plane=0; plane<2; ++plane) {
        bool uv=plane==1;
        int scale=uv?2:1, offset=uv?cur.width*cur.height:0;
        for (int y=0; y<cur.height/scale; ++y) for (int x=0; x<cur.width/scale; ++x) {
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
    }
    return out;
}
}
