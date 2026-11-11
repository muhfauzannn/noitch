#pragma once

// The closed island while something plays: the pill widens into a wing on each side of the notch,
// the artwork in the left one and a flowing wave in the right. Pure: no Windows headers, so tests run
// on any host. The macOS app uses the same numbers (LiveLayout.swift).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "features/home/home_layout.h"
#include "features/now_playing/now_playing.h"

namespace noitch {

class LiveLayout {
public:
    static constexpr float kOuterPadding = 9;
    static constexpr float kInnerGap = 8;
    static constexpr float kLineWidth = 1.6f;
    // Black fades this wide at each end of the wave, so the lines dissolve into the pill.
    static constexpr float kEdgeFade = 4;

    // A gentle wave, two lines of it: a bright short one drifting left and a faint long one drifting
    // right at another speed. Where they cross the shape keeps changing, and each line is only slid
    // sideways, so DWM does all the work.
    struct Wave {
        float length;     // Points per crest-to-crest.
        float amplitude;  // Share of the half-height the line swings through.
        double period;    // Seconds to drift one length.
        float opacity;
        float direction;  // -1 drifts left, 1 right.
    };
    static constexpr int kWaveCount = 2;
    static constexpr Wave kWaves[kWaveCount] = {{13, 0.75f, 1.7, 1.0f, -1}, {21, 0.5f, 2.9, 0.45f, 1}};

    // Logical sizes of the pill without wings.
    LiveLayout(float collapsedWidth, float collapsedHeight) : width_(collapsedWidth), height_(collapsedHeight) {}

    float ArtSide() const { return std::max(height_ - 14, 14.0f); }
    float Wing() const { return ArtSide() + kOuterPadding + kInnerGap; }
    float Width() const { return width_ + Wing() * 2; }
    float Height() const { return height_; }

    HomeRect Artwork() const { return {kOuterPadding, (height_ - ArtSide()) / 2, ArtSide(), ArtSide()}; }

    // The right wing's free space, as tall as most of the artwork.
    HomeRect WaveBox() const {
        const float height = std::round(ArtSide() * 0.7f);
        return {Wing() + width_ + kInnerGap, (height_ - height) / 2, Wing() - kInnerGap - kOuterPadding, height};
    }

    // Height of a wave's line at `x` from the box's left edge, in points.
    float WaveY(const Wave& wave, float x) const {
        const float swing = (WaveBox().height - kLineWidth) / 2 * wave.amplitude;
        return WaveBox().height / 2 + swing * std::sin(x / wave.length * 2 * 3.14159265358979f);
    }

private:
    float width_;
    float height_;
};

// When the closed island shows what is playing.
struct LiveRule {
    // A pause keeps the wings this long, so a quick pause and resume doesn't flicker the pill.
    static constexpr double kPauseGrace = 10;

    // `pausedSince` is when playback last stopped, or negative while playing.
    static bool Shows(const NowPlaying& nowPlaying, bool enabled, double pausedSince, double now) {
        if (!enabled || !nowPlaying.HasTrack()) return false;
        if (nowPlaying.isPlaying) return true;
        return pausedSince >= 0 && now - pausedSince < kPauseGrace;
    }
};

// One wave's line drawn once into premultiplied pixels: the box's width plus one length each side,
// so sliding by a length never shows an end. `color` is opaque BGRA.
inline std::vector<uint32_t> WavePixels(const LiveLayout& layout, const LiveLayout::Wave& wave, float scale, uint32_t color, int* width, int* height) {
    const HomeRect box = layout.WaveBox();
    *width = static_cast<int>((box.width + wave.length * 2) * scale + 0.5f);
    *height = static_cast<int>(box.height * scale + 0.5f);
    std::vector<uint32_t> pixels(static_cast<size_t>(*width) * static_cast<size_t>(*height), 0);
    const float half = LiveLayout::kLineWidth * scale / 2;
    for (int x = 0; x < *width; ++x) {
        const float px = (x + 0.5f) / scale - wave.length;
        const float y = layout.WaveY(wave, px) * scale;
        // Distance to the line across its slope, so steep parts are as thick as flat ones.
        const float slope = (layout.WaveY(wave, px + 0.05f) - layout.WaveY(wave, px - 0.05f)) / 0.1f;
        const float across = std::sqrt(1 + slope * slope);
        for (int row = 0; row < *height; ++row) {
            const float distance = std::fabs(row + 0.5f - y) / across;
            const float alpha = std::clamp(half - distance + 0.5f, 0.0f, 1.0f);
            if (alpha <= 0) continue;
            uint32_t pixel = static_cast<uint32_t>(alpha * 255 + 0.5f) << 24;
            for (int shift = 0; shift < 24; shift += 8) pixel |= static_cast<uint32_t>(((color >> shift) & 0xFF) * alpha + 0.5f) << shift;
            pixels[static_cast<size_t>(row * *width + x)] = pixel;
        }
    }
    return pixels;
}

// Black fading to clear across `width` pixels, from the left when `leading`.
inline std::vector<uint32_t> EdgeFadePixels(int width, int height, bool leading) {
    std::vector<uint32_t> pixels(static_cast<size_t>(width) * static_cast<size_t>(height));
    for (int x = 0; x < width; ++x) {
        const float t = (x + 0.5f) / static_cast<float>(width);
        const uint32_t alpha = static_cast<uint32_t>((leading ? 1 - t : t) * 255 + 0.5f);
        for (int y = 0; y < height; ++y) pixels[static_cast<size_t>(y * width + x)] = alpha << 24;
    }
    return pixels;
}

// Copies a square premultiplied image with its corners rounded off to `radius` pixels.
inline std::vector<uint32_t> RoundedCorners(const uint32_t* pixels, int size, float radius) {
    std::vector<uint32_t> out(pixels, pixels + static_cast<size_t>(size) * static_cast<size_t>(size));
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const float cx = std::clamp(x + 0.5f, radius, size - radius);
            const float cy = std::clamp(y + 0.5f, radius, size - radius);
            const float alpha = std::clamp(radius - std::hypot(x + 0.5f - cx, y + 0.5f - cy) + 0.5f, 0.0f, 1.0f);
            if (alpha >= 1) continue;
            uint32_t& pixel = out[static_cast<size_t>(y * size + x)];
            uint32_t scaled = 0;
            for (int shift = 0; shift < 32; shift += 8) {
                scaled |= static_cast<uint32_t>(((pixel >> shift) & 0xFF) * alpha + 0.5f) << shift;
            }
            pixel = scaled;
        }
    }
    return out;
}

}  // namespace noitch
