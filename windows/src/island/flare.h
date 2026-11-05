#pragma once

// The concave curve where the island meets the top of the screen. It never changes shape, only
// position, so its pixels are computed once per DPI. No Windows headers.

#include <cstdint>

namespace noitch {

// Flares reach this far into the pill, so no seam shows when an edge lands between two pixels.
inline constexpr int kFlareOverlapPx = 1;
inline constexpr int kMaxFlareRadiusPx = 63;

// How much of a left flare's pixel is filled, from 0 to 1. The flare is a square with a quarter
// circle removed from its bottom-left corner, so the top edge tapers out from the pill.
inline float FlareCoverage(int radius, int x, int y) {
    constexpr int kSamples = 4;
    int filled = 0;
    for (int sy = 0; sy < kSamples; ++sy) {
        for (int sx = 0; sx < kSamples; ++sx) {
            const float dx = x + (sx + 0.5f) / kSamples;
            const float dy = radius - (y + (sy + 0.5f) / kSamples);
            if (dx * dx + dy * dy >= static_cast<float>(radius * radius)) ++filled;
        }
    }
    return static_cast<float>(filled) / (kSamples * kSamples);
}

// Writes (radius + overlap) x radius premultiplied black BGRA pixels.
inline void FillFlarePixels(int radius, bool mirrored, std::uint32_t* pixels) {
    const int width = radius + kFlareOverlapPx;
    for (int y = 0; y < radius; ++y) {
        for (int x = 0; x < width; ++x) {
            const float coverage = FlareCoverage(radius, mirrored ? width - 1 - x : x, y);
            pixels[y * width + x] = static_cast<std::uint32_t>(coverage * 255 + 0.5f) << 24;
        }
    }
}

}  // namespace noitch
