#pragma once

// The closed island while something plays: the pill widens into a wing on each side of the notch,
// the artwork in the left one and six thin level bars in the right. Pure: no Windows headers, so tests run
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
    // Six thin bars, our own look: thinner and more of them than the usual four.
    static constexpr int kBarCount = 6;
    static constexpr float kBarWidth = 1.5f;
    static constexpr float kBarGap = 1.5f;
    // Each bar's resting height as a share of the tallest it reaches.
    static constexpr float kRestingScale = 0.3f;
    // Seconds per rise and fall for each bar. Unequal and not multiples of each other, so the four
    // never line up into a visible loop.
    static constexpr double kBarPeriods[kBarCount] = {0.52, 0.43, 0.61, 0.47, 0.56, 0.40};
    // How far each bar's colour is mixed toward white: the artwork's colour at the edges, nearly
    // white in the middle, so the group reads as one glow rather than six sticks.
    static constexpr float kBarWhiteness[kBarCount] = {0, 0.4f, 0.7f, 0.7f, 0.4f, 0};

    // Logical sizes of the pill without wings.
    LiveLayout(float collapsedWidth, float collapsedHeight) : width_(collapsedWidth), height_(collapsedHeight) {}

    float ArtSide() const { return std::max(height_ - 14, 14.0f); }
    float Wing() const { return ArtSide() + kOuterPadding + kInnerGap; }
    float Width() const { return width_ + Wing() * 2; }
    float Height() const { return height_; }

    HomeRect Artwork() const { return {kOuterPadding, (height_ - ArtSide()) / 2, ArtSide(), ArtSide()}; }

    float BarsWidth() const { return kBarCount * kBarWidth + (kBarCount - 1) * kBarGap; }

    // Centered in the right wing's free space, as tall as most of the artwork.
    HomeRect Bars() const {
        const float start = Wing() + width_ + kInnerGap;
        const float space = Wing() - kInnerGap - kOuterPadding;
        const float height = std::round(ArtSide() * 0.83f);
        return {start + (space - BarsWidth()) / 2, (height_ - height) / 2, BarsWidth(), height};
    }

    HomeRect Bar(int index) const {
        const HomeRect area = Bars();
        return {area.x + static_cast<float>(index) * (kBarWidth + kBarGap), area.y, kBarWidth, area.height};
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

// One cubic piece of a bar's rise or fall: value = c0 + c1 t + c2 t^2 + c3 t^3, t in seconds from
// `offset`. Matches IDCompositionAnimation::AddCubic.
struct BarSegment {
    double offset;
    float c0, c1, c2, c3;
};

// A smoothstep from `from` to `to` over `period`, then back, as two cubics. The animation repeats them
// forever, so the render server moves the bar and the app does nothing.
inline std::vector<BarSegment> BarCycle(double period, float from, float to) {
    const float span = to - from;
    const float p2 = static_cast<float>(period * period);
    const float p3 = p2 * static_cast<float>(period);
    return {
        {0, from, 0, 3 * span / p2, -2 * span / p3},
        {period, to, 0, -3 * span / p2, 2 * span / p3},
    };
}

inline double BarSegmentValue(const BarSegment& s, double t) { return s.c0 + s.c1 * t + s.c2 * t * t + s.c3 * t * t * t; }

// An opaque BGRA colour mixed `amount` of the way toward white.
inline uint32_t TowardWhite(uint32_t color, float amount) {
    uint32_t mixed = 0xFF000000;
    for (int shift = 0; shift < 24; shift += 8) {
        const float channel = static_cast<float>((color >> shift) & 0xFF);
        mixed |= static_cast<uint32_t>(channel + (255 - channel) * amount + 0.5f) << shift;
    }
    return mixed;
}

// A bar's surface: a vertical pill of `color` (BGRA, opaque) in premultiplied pixels.
inline std::vector<uint32_t> BarPixels(int width, int height, uint32_t color) {
    std::vector<uint32_t> pixels(static_cast<size_t>(width) * static_cast<size_t>(height), 0);
    const float radius = width / 2.0f;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const float cx = x + 0.5f;
            const float cy = y + 0.5f;
            const float centerY = std::clamp(cy, radius, height - radius);
            const float distance = std::hypot(cx - radius, cy - centerY);
            const float alpha = std::clamp(radius - distance + 0.5f, 0.0f, 1.0f);
            const auto channel = [&](int shift) { return static_cast<uint32_t>(((color >> shift) & 0xFF) * alpha + 0.5f) << shift; };
            pixels[static_cast<size_t>(y * width + x)] = channel(0) | channel(8) | channel(16) | (static_cast<uint32_t>(alpha * 255 + 0.5f) << 24);
        }
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
