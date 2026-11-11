#pragma once

#include <windows.h>

#include <algorithm>

#include "media_session_source.h"

namespace noitch {

// The one media session subscription, shared by Home and the closed island. It runs while Home is
// open (and its grace period) and while the closed island shows music. Windows' media session events
// cost nothing at rest and need no other process, so unlike macOS there is no list of player apps.
class NowPlayingMonitor {
public:
    enum Listener { kHome = 1, kLive = 2 };

    void Initialize(HWND window, double (*now)()) {
        window_ = window;
        now_ = now;
    }

    void SetNeeded(Listener listener, bool needed) {
        needs_ = needed ? (needs_ | listener) : (needs_ & ~listener);
        if (needs_ != 0) {
            source_.Start(window_, now_);
        } else {
            source_.Stop();
        }
    }

    // Artwork is decoded once, at the larger of the sizes asked for, and each listener scales it.
    void SetArtworkPixels(Listener listener, int pixels) {
        (listener == kHome ? homePixels_ : livePixels_) = pixels;
        source_.SetArtworkPixels(std::max(homePixels_, livePixels_));
    }

    // Handles MediaSessionSource::kMessage and tells both listeners when something changed.
    bool HandleMessage(UINT message, WPARAM wparam, LPARAM lparam) {
        if (message != MediaSessionSource::kMessage) return false;
        if (source_.HandleMessage(wparam, lparam)) {
            if (onHome) onHome(homeContext);
            if (onLive) onLive(liveContext);
        }
        return true;
    }

    MediaSessionSource& Source() { return source_; }

    void (*onHome)(void* context) = nullptr;
    void* homeContext = nullptr;
    void (*onLive)(void* context) = nullptr;
    void* liveContext = nullptr;

private:
    HWND window_ = nullptr;
    double (*now_)() = nullptr;
    int needs_ = 0;
    int homePixels_ = 0;
    int livePixels_ = 0;
    MediaSessionSource source_;
};

}  // namespace noitch
