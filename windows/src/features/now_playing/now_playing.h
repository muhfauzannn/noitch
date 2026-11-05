#pragma once

// Pure values. No Windows headers, so it is unit-testable on any host. Text is UTF-8 throughout, and
// only the scene converts it, when drawing.

#include <algorithm>
#include <string>

namespace noitch {

enum class RepeatMode { Off, One, All };

inline RepeatMode NextRepeatMode(RepeatMode mode) {
    switch (mode) {
        case RepeatMode::Off: return RepeatMode::All;
        case RepeatMode::All: return RepeatMode::One;
        default: return RepeatMode::Off;
    }
}

struct NowPlaying {
    std::string title;
    std::string artist;
    std::string album;
    double duration = 0;
    // Playback position at `timestamp`, in seconds of the app's monotonic clock. The clock is derived
    // from these two, never pushed per second.
    double elapsed = 0;
    double timestamp = 0;
    double rate = 1;
    bool isPlaying = false;
    bool isShuffled = false;
    RepeatMode repeatMode = RepeatMode::Off;

    bool HasTrack() const { return !title.empty(); }

    double Position(double now) const {
        if (!isPlaying) return elapsed;
        const double position = std::max(elapsed + (now - timestamp) * rate, 0.0);
        return duration > 0 ? std::min(position, duration) : position;
    }
};

}  // namespace noitch
