#pragma once

// What Home shows, reduced to values. The scene draws exactly this and decides nothing. No Windows
// headers, so it is unit-testable on any host.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "features/lyrics/lyrics.h"
#include "features/now_playing/now_playing.h"
#include "home_settings.h"

namespace noitch {

inline std::string FormatTime(double seconds) {
    const int total = static_cast<int>(std::max(seconds, 0.0));
    char text[32];
    if (total >= 3600) {
        std::snprintf(text, sizeof(text), "%d:%02d:%02d", total / 3600, total / 60 % 60, total % 60);
    } else {
        std::snprintf(text, sizeof(text), "%d:%02d", total / 60, total % 60);
    }
    return text;
}

struct HomeModel {
    std::string title = "Nothing playing";
    std::string artist = "Start something to see it here";
    // The line being sung, bright, and the one after it, dim. A status message takes the first slot.
    std::string lyric;
    std::string nextLyric;
    int lyricIndex = -1;  // The synced line, or -1.
    bool lyricIsStatus = false;
    std::string elapsedText = "0:00";
    std::string remainingText = "0:00";
    double progress = 0;
    bool hasTrack = false;
    bool isPlaying = false;
    bool isShuffled = false;
    RepeatMode repeatMode = RepeatMode::Off;
    bool showsShuffleAndRepeat = true;
    // After next or previous, until the player reports the new track: the old one stays, dimmed,
    // so the press is answered at once even when the player is slow to say what comes next.
    bool isSkipping = false;
    // The lyrics button shows only while lyrics are on.
    bool showsLyricsButton = true;
    // The whole page is the lyrics stage.
    bool isOnStage = false;
    // The stage's lines: the one before, the one being sung, and the two after. An empty line shows
    // as a note, so every slot reads as part of the song.
    std::array<std::string, 4> stageLines;
    bool stageIsStatus = false;
};

inline std::string LyricStatus(const Lyrics* lyrics, bool isLoading) {
    if (isLoading) return "Looking for lyrics\xE2\x80\xA6";
    if (!lyrics) return "";
    if (lyrics->isInstrumental) return "Instrumental";
    if (!lyrics->plain.empty()) return "Lyrics aren't synced for this track";
    return "No lyrics found";
}

inline HomeModel MakeHomeModel(const NowPlaying& nowPlaying, const Lyrics* lyrics, bool isLoadingLyrics, double position,
                               const HomeSettings& settings, bool isSkipping = false, bool onStage = false) {
    HomeModel model;
    model.showsShuffleAndRepeat = settings.showShuffleAndRepeat;
    model.showsLyricsButton = settings.showLyrics;
    model.isOnStage = onStage && settings.showLyrics;
    if (!nowPlaying.HasTrack()) {
        model.stageLines[1] = model.title;
        model.stageIsStatus = true;
        return model;
    }

    model.hasTrack = true;
    model.title = nowPlaying.title;
    model.artist = nowPlaying.artist;
    model.isPlaying = nowPlaying.isPlaying;
    model.isShuffled = nowPlaying.isShuffled;
    model.repeatMode = nowPlaying.repeatMode;
    model.isSkipping = isSkipping;
    if (isSkipping) {
        model.remainingText.clear();
        model.stageLines[1] = "\xE2\x99\xAA";
        model.stageIsStatus = true;
        return model;
    }
    model.elapsedText = FormatTime(position);
    model.remainingText = "-" + FormatTime(std::max(nowPlaying.duration - position, 0.0));
    model.progress = nowPlaying.duration > 0 ? std::clamp(position / nowPlaying.duration, 0.0, 1.0) : 0;
    if (!settings.showLyrics) return model;

    if (lyrics && !lyrics->synced.empty()) {
        const int index = lyrics->LineIndex(position + settings.lyricsOffset);
        model.lyricIndex = index;
        // Before the first line, and in the gaps a line leaves empty, a note holds the place.
        const std::string current = index >= 0 ? lyrics->synced[static_cast<size_t>(index)].text : "";
        model.lyric = current.empty() ? "\xE2\x99\xAA" : current;
        const int next = index + 1;
        if (settings.showNextLyric && next < static_cast<int>(lyrics->synced.size())) model.nextLyric = lyrics->synced[static_cast<size_t>(next)].text;
        for (int step = -1; step <= 2; ++step) {
            const int line = index + step;
            std::string& slot = model.stageLines[static_cast<size_t>(step + 1)];
            if (line < 0 || line >= static_cast<int>(lyrics->synced.size())) {
                if (step == 0) slot = "\xE2\x99\xAA";
                continue;
            }
            const std::string& text = lyrics->synced[static_cast<size_t>(line)].text;
            slot = text.empty() ? "\xE2\x99\xAA" : text;
        }
    } else {
        model.lyric = LyricStatus(lyrics, isLoadingLyrics);
        model.lyricIsStatus = true;
        model.stageLines[1] = model.lyric;
        model.stageIsStatus = true;
    }
    return model;
}

// How long until anything on screen changes: the next whole second or the next lyric. One one-shot
// timer covers both, and it only exists while the island is open and playing. The stage shows no
// clock, so there only lyrics count, and -1 means nothing will change.
inline double NextHomeTick(double position, double rate, double nextLyric, bool wholeSeconds = true) {
    const double speed = std::max(rate, 0.1);
    double delay = wholeSeconds ? (std::floor(position) + 1 - position) / speed : -1;
    if (nextLyric > position) {
        const double untilLyric = (nextLyric - position) / speed;
        delay = delay < 0 ? untilLyric : std::min(delay, untilLyric);
    }
    return delay < 0 ? -1 : std::max(delay, 0.05);
}

// The average of premultiplied BGRA pixels, lifted so a dark cover still gives a visible bar.
inline uint32_t ArtworkTint(const uint32_t* pixels, size_t count) {
    if (count == 0) return 0xFFFFFFFF;
    uint64_t blue = 0, green = 0, red = 0;
    for (size_t i = 0; i < count; ++i) {
        blue += pixels[i] & 0xFF;
        green += pixels[i] >> 8 & 0xFF;
        red += pixels[i] >> 16 & 0xFF;
    }
    double b = static_cast<double>(blue) / static_cast<double>(count);
    double g = static_cast<double>(green) / static_cast<double>(count);
    double r = static_cast<double>(red) / static_cast<double>(count);
    const double brightest = std::max({r, g, b, 1.0});
    const double lift = std::max(191.0 / brightest, 1.0);
    const auto channel = [lift](double value) { return static_cast<uint32_t>(std::min(value * lift, 255.0)); };
    return 0xFF000000u | channel(r) << 16 | channel(g) << 8 | channel(b);
}

// The stage's background: a square premultiplied BGRA cover averaged down to `columns` x `rows`
// opaque pixels, which the scene stretches across the page with a smooth filter so the colours melt
// into each other. Made once per track, never blurred live.
inline std::vector<uint32_t> ArtworkMosaic(const uint32_t* pixels, int size, int columns, int rows) {
    std::vector<uint32_t> out(static_cast<size_t>(columns) * static_cast<size_t>(rows), 0xFF000000u);
    if (size <= 0) return out;
    for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < columns; ++column) {
            const int x0 = column * size / columns, x1 = std::max((column + 1) * size / columns, x0 + 1);
            const int y0 = row * size / rows, y1 = std::max((row + 1) * size / rows, y0 + 1);
            uint64_t sum[3] = {0, 0, 0};
            for (int y = y0; y < y1 && y < size; ++y) {
                for (int x = x0; x < x1 && x < size; ++x) {
                    const uint32_t pixel = pixels[static_cast<size_t>(y) * static_cast<size_t>(size) + static_cast<size_t>(x)];
                    for (int c = 0; c < 3; ++c) sum[c] += pixel >> (c * 8) & 0xFF;
                }
            }
            const uint64_t count = static_cast<uint64_t>(std::min(x1, size) - x0) * static_cast<uint64_t>(std::min(y1, size) - y0);
            uint32_t color = 0xFF000000u;
            for (int c = 0; c < 3; ++c) color |= static_cast<uint32_t>(sum[c] / std::max<uint64_t>(count, 1)) << (c * 8);
            out[static_cast<size_t>(row * columns + column)] = color;
        }
    }
    return out;
}

}  // namespace noitch
