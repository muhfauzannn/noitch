#pragma once

#include <windows.h>

#include <memory>

#include "features/home/home_settings.h"
#include "features/now_playing/now_playing_monitor.h"
#include "island/compositor.h"
#include "live_layout.h"
#include "live_scene.h"

namespace noitch {

// Owns the closed island's music state: whether the wings show, and what is in them. The island asks
// IsActive() to size the closed pill and is told when that changes. Driven by media session events
// and one one-shot timer for the end of a pause.
class LiveController {
public:
    static constexpr UINT_PTR kPauseTimer = 10;
    static constexpr UINT_PTR kReleaseTimer = 11;

    void Initialize(HWND window, Compositor* compositor, NowPlayingMonitor* monitor, double (*now)(), const HomeSettings& settings);
    void Apply(const HomeSettings& settings);
    // The pill without wings, in logical units, and the fixed window's width in pixels.
    void SetGeometry(float collapsedWidth, float collapsedHeight, int windowWidth, float scale);
    bool IsActive() const { return active_; }

    void IslandWillOpen();
    void IslandWillClose();
    void DeviceLost();
    // True for a message the closed island owns.
    bool HandleMessage(UINT message, WPARAM wparam, LPARAM lparam);

    void (*onActiveChange)(void* context, bool active) = nullptr;
    void* context = nullptr;

private:
    void Receive();
    void Evaluate();
    void BuildScene();
    void ReleaseScene();
    void ShowArtwork();

    HWND window_ = nullptr;
    Compositor* compositor_ = nullptr;
    NowPlayingMonitor* monitor_ = nullptr;
    double (*now_)() = nullptr;
    bool enabled_ = true;
    bool tintBars_ = true;
    bool active_ = false;
    bool islandOpen_ = false;
    bool hasLayout_ = false;
    LiveLayout layout_{0, 0};
    int windowWidth_ = 0;
    float scale_ = 1;
    NowPlaying nowPlaying_;
    // When playback last stopped, or negative while playing.
    double pausedSince_ = -1;
    std::shared_ptr<const Artwork> shownArtwork_;
    LiveScene scene_;
};

}  // namespace noitch
