// SPDX-License-Identifier: MIT
// Still images for the panel's preview: NV12 to 24-bit BMP, downscaled.
#pragma once
#include "MotionBlur.hpp"
#include <cstdio>
#include <string>

namespace preview {
using motion::Byte;
using motion::Frame;

struct Colour { bool bt709 = true, fullRange = false; };

// Box-filtered downscale by a whole factor, so the panel decodes a small file.
inline int factorFor(int width, int maxWidth) { return std::max(1, (width + maxWidth - 1) / maxWidth); }

// Packed bottom-up BGR rows padded to four bytes, as BMP stores them.
inline std::vector<Byte> toBmp(const Frame& f, Colour colour, int maxWidth = 960) {
    const int k = factorFor(f.width, maxWidth), w = f.width / k, h = f.height / k;
    const int stride = (w * 3 + 3) & ~3;
    const std::uint32_t imageBytes = std::uint32_t(stride) * std::uint32_t(h);
    std::vector<Byte> bmp(54 + imageBytes);
    auto put16 = [&](int at, unsigned v) { bmp[at] = Byte(v); bmp[at+1] = Byte(v >> 8); };
    auto put32 = [&](int at, std::uint32_t v) { for (int i = 0; i < 4; ++i) bmp[at+i] = Byte(v >> (8*i)); };
    bmp[0] = 'B'; bmp[1] = 'M';
    put32(2, std::uint32_t(bmp.size())); put32(10, 54);
    put32(14, 40); put32(18, std::uint32_t(w)); put32(22, std::uint32_t(h));
    put16(26, 1); put16(28, 24); put32(34, imageBytes);
    put32(38, 2835); put32(42, 2835);
    // Y'CbCr to R'G'B' (ITU-R BT.709 or BT.601), limited or full range.
    const float kr = colour.bt709 ? 0.2126f : 0.299f, kb = colour.bt709 ? 0.0722f : 0.114f, kg = 1 - kr - kb;
    const float ys = colour.fullRange ? 1.f : 255.f/219, yo = colour.fullRange ? 0.f : 16.f;
    const float cs = colour.fullRange ? 1.f : 255.f/224;
    const Byte* uv = f.pixels.data() + std::size_t(f.width) * f.height;
    motion::parallelRows(h, [&](int row) {
        Byte* out = bmp.data() + 54 + std::size_t(h-1-row) * stride;
        for (int col = 0; col < w; ++col) {
            float y = 0, cb = 0, cr = 0;
            for (int j = 0; j < k; ++j) for (int i = 0; i < k; ++i) {
                const int x = col*k + i, yy = row*k + j;
                y += f.pixels[std::size_t(yy) * f.width + x];
                const Byte* c = uv + std::size_t(yy/2) * f.width + (x/2)*2;
                cb += c[0]; cr += c[1];
            }
            const float n = float(k*k);
            const float Y = (y/n - yo) * ys, Pb = (cb/n - 128) * cs, Pr = (cr/n - 128) * cs;
            const float r = Y + 2*(1-kr)*Pr, b = Y + 2*(1-kb)*Pb;
            const float g = (Y - kr*r - kb*b) / kg;
            auto byte = [](float v) { return Byte(std::clamp(std::lround(v), 0L, 255L)); };
            out[3*col] = byte(b); out[3*col+1] = byte(g); out[3*col+2] = byte(r);
        }
    });
    return bmp;
}

inline bool writeFile(const std::wstring& path, const std::vector<Byte>& data) {
#ifdef _WIN32
    FILE* file = nullptr;
    if (_wfopen_s(&file, path.c_str(), L"wb") != 0) file = nullptr;
#else
    FILE* file = std::fopen(std::string(path.begin(), path.end()).c_str(), "wb");
#endif
    if (!file) return false;
    const bool ok = std::fwrite(data.data(), 1, data.size(), file) == data.size();
    return std::fclose(file) == 0 && ok;
}
}
