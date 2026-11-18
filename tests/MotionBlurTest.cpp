// SPDX-License-Identifier: MIT
#include "../worker/MotionBlur.hpp"
#include <cassert>
#include <iostream>
using namespace motion;

// A high-contrast textured plane moving horizontally by four pixels/frame.
// Its stationary counterpart must survive both integer and subpixel sampling.
Frame texture(int offset) {
    Frame f(96,64);
    for (int y=0;y<f.height;++y) for (int x=0;x<f.width;++x) {
        int xx=x-offset+128;
        unsigned seed=unsigned(xx)*2654435761u ^ unsigned(y)*2246822519u;
        f.pixels[y*f.width+x]=Byte(32+((seed^(seed>>16))%192));
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
    std::cout<<"Motion tests passed\n";
}
