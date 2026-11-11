#pragma once

// Home's preferences. No Windows headers, so tests run on any host. Matches HomeSettings.swift.

namespace noitch {

struct HomeSettings {
    bool showLyrics = true;
    bool showNextLyric = true;
    // Seconds added to the playback position when picking the lyric: negative for lyrics that run
    // early, positive for late ones.
    double lyricsOffset = 0;
    bool showShuffleAndRepeat = true;
    // The progress bar takes the artwork's color.
    bool tintWithArtwork = true;
    // A soft wash of the artwork's color behind the page.
    bool artworkGlow = true;
    // While something plays, the closed island widens to show its artwork and level bars.
    bool showLive = true;
    // The closed island's pill also shows the lyric line being sung, under the notch.
    bool showLiveLyrics = true;

    bool operator==(const HomeSettings&) const = default;
};

inline constexpr double kLyricsOffsets[] = {-1.0, -0.5, -0.25, 0, 0.25, 0.5, 1.0};

}  // namespace noitch
