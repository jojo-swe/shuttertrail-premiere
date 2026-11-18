// SPDX-License-Identifier: MIT
// Colour conversion, downscaling and BMP layout of the panel preview.
#include "../worker/Preview.hpp"
#include <cassert>
#include <iostream>
using namespace preview;

static Frame solid(int w, int h, Byte y, Byte cb, Byte cr) {
    Frame f(w,h);
    std::fill(f.pixels.begin(), f.pixels.begin()+w*h, y);
    for (std::size_t i = std::size_t(w)*h; i < f.pixels.size(); i += 2) { f.pixels[i] = cb; f.pixels[i+1] = cr; }
    return f;
}
static unsigned u32(const std::vector<Byte>& b, int at) {
    return b[at] | b[at+1] << 8 | b[at+2] << 16 | unsigned(b[at+3]) << 24;
}
static void pixelIs(const std::vector<Byte>& bmp, int r, int g, int b, int tolerance) {
    const Byte* p = bmp.data() + 54;
    assert(std::abs(p[2]-r) <= tolerance && std::abs(p[1]-g) <= tolerance && std::abs(p[0]-b) <= tolerance);
}
int main() {
    // Limited range: 16 is black, 235 is white, neutral chroma is grey.
    pixelIs(toBmp(solid(8,8,16,128,128),{}), 0,0,0, 0);
    pixelIs(toBmp(solid(8,8,235,128,128),{}), 255,255,255, 0);
    pixelIs(toBmp(solid(8,8,126,128,128),{}), 128,128,128, 1);
    // Full range maps 0..255 directly.
    pixelIs(toBmp(solid(8,8,200,128,128),{true,true}), 200,200,200, 0);
    // Saturated primaries in BT.709 and BT.601 limited range.
    pixelIs(toBmp(solid(8,8,63,102,240),{true,false}), 255,0,0, 3);
    pixelIs(toBmp(solid(8,8,173,42,26),{true,false}), 0,255,0, 3);
    pixelIs(toBmp(solid(8,8,32,240,118),{true,false}), 0,0,255, 3);
    pixelIs(toBmp(solid(8,8,81,90,240),{false,false}), 255,0,0, 3);
    // Header, dimensions and four-byte row padding for an odd width.
    auto small = toBmp(solid(6,4,16,128,128),{},3);   // factor 2 -> 3x2
    assert(small[0]=='B' && small[1]=='M' && u32(small,2)==small.size());
    assert(u32(small,18)==3 && u32(small,22)==2 && u32(small,10)==54);
    assert(small.size()==54u+12*2);
    // A 1920-pixel frame shrinks to 960 by averaging 2x2 blocks; rows are
    // stored bottom-up, so the top image row is the last one in the file.
    Frame stripes = solid(1920,4,16,128,128);
    for (int x = 0; x < 1920; ++x) stripes.pixels[x] = 235;   // only the top row is white
    auto bmp = toBmp(stripes,{});
    assert(u32(bmp,18)==960 && u32(bmp,22)==2);
    const Byte* top = bmp.data() + 54 + 960*3, *bottom = bmp.data() + 54;
    assert(std::abs(top[0]-128) <= 1 && bottom[0] == 0);
    std::cout << "Preview tests passed\n";
}
