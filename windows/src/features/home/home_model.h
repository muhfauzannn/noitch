#pragma once

// What Home shows, reduced to values. The scene draws exactly this and decides nothing. No Windows
// headers, so it is unit-testable on any host.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

#include "features/lyrics/lyrics.h"
#include "features/now_playing/now_playing.h"
#include "features/settings/app_settings.h"
#include "home_layout.h"

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
    std::string lyricLine;
    std::array<std::string, 3> panelLines;
    std::string elapsedText = "0:00";
    std::string remainingText = "0:00";
    double progress = 0;
    bool hasTrack = false;
    bool isPlaying = false;
    bool isShuffled = false;
    RepeatMode repeatMode = RepeatMode::Off;
    bool showsLyricsPanel = false;
    bool showsLyricsButton = true;
    bool showsShuffleAndRepeat = true;
};

// Whether hovering a control shows the plate behind it. The slider and artwork have none, and a
// button the settings turned off isn't there to hover.
inline bool ControlHasPlate(HomeControl control, const HomeModel& model) {
    switch (control) {
        case HomeControl::Settings:
        case HomeControl::Previous:
        case HomeControl::PlayPause:
        case HomeControl::Next: return true;
        case HomeControl::Lyrics: return model.showsLyricsButton;
        case HomeControl::Shuffle:
        case HomeControl::Repeat: return model.showsShuffleAndRepeat;
        default: return false;
    }
}

inline std::string LyricStatus(const Lyrics* lyrics, bool isLoading) {
    if (isLoading) return "Looking for lyrics…";
    if (!lyrics) return "";
    if (lyrics->isInstrumental) return "Instrumental";
    if (!lyrics->plain.empty()) return "Lyrics aren't synced for this track";
    return "No lyrics found";
}

inline HomeModel MakeHomeModel(const NowPlaying& nowPlaying, const Lyrics* lyrics, bool isLoadingLyrics, double position,
                               bool showsPanel, const AppSettings& settings) {
    HomeModel model;
    model.showsLyricsButton = settings.showLyrics;
    model.showsShuffleAndRepeat = settings.showShuffleAndRepeat;
    model.showsLyricsPanel = showsPanel && settings.showLyrics;
    if (!nowPlaying.HasTrack()) return model;

    model.hasTrack = true;
    model.title = nowPlaying.title;
    model.artist = nowPlaying.artist;
    model.isPlaying = nowPlaying.isPlaying;
    model.isShuffled = nowPlaying.isShuffled;
    model.repeatMode = nowPlaying.repeatMode;
    model.elapsedText = FormatTime(position);
    model.remainingText = "-" + FormatTime(std::max(nowPlaying.duration - position, 0.0));
    model.progress = nowPlaying.duration > 0 ? std::clamp(position / nowPlaying.duration, 0.0, 1.0) : 0;
    if (!settings.showLyrics) return model;

    const std::string status = LyricStatus(lyrics, isLoadingLyrics);
    if (lyrics && !lyrics->synced.empty()) {
        const int index = lyrics->LineIndex(position);
        if (index >= 0) model.lyricLine = lyrics->synced[static_cast<size_t>(index)].text;
        for (int i = 0; i < 3; ++i) {
            const int line = index - 1 + i;
            if (line >= 0 && line < static_cast<int>(lyrics->synced.size())) {
                model.panelLines[static_cast<size_t>(i)] = lyrics->synced[static_cast<size_t>(line)].text;
            }
        }
    } else if (lyrics && !lyrics->plain.empty()) {
        model.lyricLine = status;
        for (size_t i = 0; i < 3 && i < lyrics->plain.size(); ++i) model.panelLines[i] = lyrics->plain[i];
    } else {
        model.lyricLine = status;
        model.panelLines[1] = status;
    }
    return model;
}

// How long until anything on screen changes: the next whole second or the next lyric. One one-shot
// timer covers both, and it only exists while the island is open and playing.
inline double NextHomeTick(double position, double rate, double nextLyric) {
    const double speed = std::max(rate, 0.1);
    double delay = (std::floor(position) + 1 - position) / speed;
    if (nextLyric > position) delay = std::min(delay, (nextLyric - position) / speed);
    return std::max(delay, 0.05);
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

}  // namespace noitch
