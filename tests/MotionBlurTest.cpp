// SPDX-License-Identifier: MIT
#include "../worker/MotionBlur.hpp"
#include <cassert>
#include <iostream>
using namespace motion;

// A high-contrast textured plane moving horizontally by four pixels/frame.
// Its stationary counterpart must survive both integer and subpixel sampling.
Frame texture(int offset, int width=96, int height=64) {
    Frame f(width,height);
    for (int y=0;y<f.height;++y) for (int x=0;x<f.width;++x) {
        int xx=x-offset+128;
        unsigned seed=unsigned(xx)*2654435761u ^ unsigned(y)*2246822519u;
        f.pixels[y*f.width+x]=Byte(32+((seed^(seed>>16))%192));
    }
    std::fill(f.pixels.begin()+f.width*f.height,f.pixels.end(),128);
    return f;
}
// Smooth value noise (6-pixel cells): a textured 120x80 block moving 10 pixels
// per frame over a different, static textured background.
float lattice(int x,int y,unsigned salt) {
    unsigned s=unsigned(x)*2654435761u ^ unsigned(y)*2246822519u ^ salt;
    s^=s>>15; s*=2246822519u; s^=s>>13;
    return float(s%192);
}
Byte smooth(int x,int y,unsigned salt) {
    int gx=x/6, gy=y/6; float fx=(x%6)/6.f, fy=(y%6)/6.f;
    return Byte(32+(1-fy)*((1-fx)*lattice(gx,gy,salt)+fx*lattice(gx+1,gy,salt))
                  +fy*((1-fx)*lattice(gx,gy+1,salt)+fx*lattice(gx+1,gy+1,salt)));
}
// The same noise sampled at a fractional horizontal offset.
Frame shifted(float offset) {
    Frame f(192,128);
    for (int y=0;y<f.height;++y) for (int x=0;x<f.width;++x) {
        float u=(x-offset+60)/6.f, v=y/6.f;
        int gx=int(std::floor(u)), gy=int(std::floor(v));
        float fx=u-gx, fy=v-gy;
        f.pixels[y*f.width+x]=Byte(32+(1-fy)*((1-fx)*lattice(gx,gy,3)+fx*lattice(gx+1,gy,3))
                                      +fy*((1-fx)*lattice(gx,gy+1,3)+fx*lattice(gx+1,gy+1,3)));
    }
    std::fill(f.pixels.begin()+f.width*f.height,f.pixels.end(),128);
    return f;
}
Frame occlusion(int left) {
    Frame f(320,200);
    for (int y=0;y<f.height;++y) for (int x=0;x<f.width;++x) {
        bool block=x>=left && x<left+120 && y>=60 && y<140;
        f.pixels[y*f.width+x]=block ? smooth(x-left,y,7) : smooth(x,y,99);
    }
    std::fill(f.pixels.begin()+f.width*f.height,f.pixels.end(),128);
    return f;
}
int main() {
    auto prev=texture(-4), current=texture(0), next=texture(4);
    assert(expose(prev,current,next,8,0).pixels==current.pixels);
    assert(expose(current,current,current,8,180).pixels==current.pixels);
    auto still=current;
    assert(expose(still,current,still,8,180).pixels==current.pixels);
    auto abrupt=current;
    std::fill(abrupt.pixels.begin(),abrupt.pixels.end(),0);
    assert(sceneCut(current,abrupt));
    assert(expose(current,current,abrupt,8,360).pixels==current.pixels);
    Field field(current,next);
    auto v=field.at(42,30);
    assert(std::abs(v.x-4)<0.1 && std::abs(v.y)<0.1);
    auto partial=expose(prev,current,next,8,90), full=expose(prev,current,next,8,360);
    auto distance=[&](const Frame& frame) {
        long total=0;
        for(int y=12;y<48;++y) for(int x=20;x<76;++x)
            total+=std::abs(pixel(frame,x,y)-pixel(current,x,y));
        return total;
    };
    assert(distance(partial)>0 && distance(full)>distance(partial));
    // Neutral chroma remains neutral, even while luminance moves.
    assert(std::all_of(full.pixels.begin()+full.width*full.height,full.pixels.end(),
                       [](Byte b){ return b==128; }));
    // Clamp the closing frame of a one-frame selection at both edges.
    assert(expose(current,current,current,16,360).pixels==current.pixels);
    // Fast motion beyond the 15-pixel fine search window is found through the
    // half-resolution level, and the result does not depend on the thread count.
    auto fastPrev=texture(-24,256,192), fastCur=texture(0,256,192), fastNext=texture(24,256,192);
    auto fast=Field(fastCur,fastNext).at(128,96);
    assert(std::abs(fast.x-24)<0.1 && std::abs(fast.y)<0.1 && fast.confidence>0.5);
    auto back=Field(fastCur,fastPrev).at(128,96);
    assert(std::abs(back.x+24)<0.1 && std::abs(back.y)<0.1);
    auto threaded=expose(fastPrev,fastCur,fastNext,8,360);
    threadLimit()=1;
    auto serial=expose(fastPrev,fastCur,fastNext,8,360);
    threadLimit()=0;
    assert(threaded.pixels==serial.pixels);
    long change=0;
    for (int y=48;y<144;++y) for (int x=64;x<192;++x) change+=std::abs(pixel(threaded,x,y)-pixel(fastCur,x,y));
    assert(change>96L*128*10);
    // Covered and uncovered background yields confident but wrong vectors.
    // The forward-backward check removes most of that confidence and keeps
    // the correct vectors.
    {
        auto a=occlusion(100), b=occlusion(110);
        Field raw(a,b), reverse(b,a), checked=raw;
        crossCheck(checked,reverse);
        auto tally=[&](const Field& f, float& wrong, int& right) {
            wrong=0; right=0;
            for (int row=0;row<f.rows;++row) for (int col=0;col<f.cols;++col) {
                const auto& vec=f.vectors[row*f.cols+col];
                if (vec.confidence<=0) continue;
                int cx=col*tile+tile/2, cy=row*tile+tile/2;
                float truth=cx>=100 && cx<220 && cy>=60 && cy<140 ? 10.f : 0.f;
                if (std::abs(vec.x-truth)<=2 && std::abs(vec.y)<=2) ++right; else wrong+=vec.confidence;
            }
        };
        float wrongRaw, wrongChecked; int rightRaw, rightChecked;
        tally(raw,wrongRaw,rightRaw); tally(checked,wrongChecked,rightChecked);
        assert(wrongRaw>1 && wrongChecked<wrongRaw*0.5f);
        assert(rightRaw>=60 && rightChecked*100>=rightRaw*95);
    }
    // Motion between whole pixels is measured, not rounded.
    for (float motion : {0.5f, 1.25f, 2.5f, 3.75f}) {
        auto a=shifted(0), b=shifted(motion);
        Field f(a,b);
        float worst=0;
        for (float x=48;x<=144;x+=24) for (float y=36;y<=92;y+=28)
            worst=std::max(worst,std::abs(f.at(x,y).x-motion));
        assert(worst<0.25f);
    }
    // The streaming renderer reuses each field for its neighbour and must
    // produce exactly what independent calls produce, including clamped ends.
    {
        std::vector<Frame> clip;
        for (int n=0;n<5;++n) clip.push_back(occlusion(80+10*n));
        Exposer stream(8,180);
        for (int n=0;n<5;++n) {
            const Frame& p=clip[n?n-1:n];
            const Frame& q=clip[n+1<5?n+1:n];
            assert(stream(p,clip[n],q).pixels==expose(p,clip[n],q,8,180).pixels);
        }
    }
    // Shutter phase moves the exposure window. Trailing at 180° (phase -90°)
    // samples only the past, so the next frame cannot change it; leading
    // (phase +90°) samples only the future. Centred uses both.
    {
        auto other=texture(9);
        assert(expose(prev,current,next,8,180,-90).pixels==expose(prev,current,other,8,180,-90).pixels);
        assert(expose(prev,current,next,8,180,90).pixels==expose(other,current,next,8,180,90).pixels);
        assert(expose(prev,current,next,8,180,0).pixels!=expose(prev,current,other,8,180,0).pixels);
        assert(expose(prev,current,next,8,180,0).pixels==expose(prev,current,next,8,180).pixels);
    }
    // Strength mixes the blur with the original frame: 0% is the original,
    // 100% the full blur, and 50% lies halfway within rounding.
    {
        auto none=expose(prev,current,next,8,360,0,0), half=expose(prev,current,next,8,360,0,50);
        assert(none.pixels==current.pixels);
        assert(expose(prev,current,next,8,360,0,100).pixels==full.pixels);
        for (size_t i=0;i<half.pixels.size();++i)
            assert(std::abs(2*int(half.pixels[i])-int(current.pixels[i])-int(full.pixels[i]))<=2);
        assert(half.pixels!=current.pixels && half.pixels!=full.pixels);
    }
    std::cout<<"Motion tests passed\n";
}
