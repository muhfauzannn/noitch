#pragma once

#include <windows.h>

#include <memory>
#include <optional>

#include "features/lyrics/lyrics_source.h"
#include "features/now_playing/now_playing_monitor.h"
#include "home_settings.h"
#include "home_layout.h"
#include "home_model.h"
#include "home_scene.h"

namespace noitch {

// Owns everything Home knows: what is playing, its lyrics, and how the pointer is using the controls.
// The island tells it when it opens and closes, and it keeps nothing alive while closed.
class HomeController {
public:
    static constexpr UINT_PTR kTickTimer = 2;
    static constexpr UINT_PTR kReleaseTimer = 3;
    static constexpr UINT_PTR kSkipTimer = 8;

    void Initialize(HWND window, Compositor* compositor, double (*now)(), NowPlayingMonitor* monitor) {
        window_ = window;
        compositor_ = compositor;
        now_ = now;
        monitor_ = monitor;
        monitor->onHome = [](void* context) {
            auto* self = static_cast<HomeController*>(context);
            if (self->isHolding_) self->ReceiveMedia();
        };
        monitor->homeContext = this;
    }

    void IslandWillOpen(const HomeLayout& layout, float left, float scale);
    void IslandWillClose();
    void Apply(const HomeSettings& settings);

    // The compositor rebuilt its tree, so the scene's surface is gone with the old device.
    void DeviceLost();

    // True for a message Home owns. Call with the island's window messages.
    bool HandleMessage(UINT message, WPARAM wparam, LPARAM lparam);

    // Points are from the pill's top-left corner, in pixels.
    void PointerMoved(float x, float y);
    void PointerDown(float x, float y);
    void PointerUp(float x, float y);

    // True during a slider drag, which may leave the island without closing it.
    bool IsInteracting() const { return pressed_ == HomeControl::Slider; }


private:
    void Release();
    void Refresh();
    void ScheduleTick();
    void Activate(HomeControl control);
    int StageLineIndex(float x, float y) const;
    void JumpToLine(int line);
    bool IsOnStage() const { return prefersStage_ && settings_.showLyrics; }
    void FinishScrub(double fraction);
    void ReceiveMedia();
    void ShowArtwork();
    void BeginSkip();
    void RequestLyricsIfNeeded();
    double CurrentPosition() const;

    HWND window_ = nullptr;
    Compositor* compositor_ = nullptr;
    double (*now_)() = nullptr;
    HomeSettings settings_;
    HomeLayout layout_;
    float left_ = 0;
    float scale_ = 0;
    HomeScene scene_;
    NowPlayingMonitor* monitor_ = nullptr;
    // True from opening until the grace period ends: Home keeps the subscription meanwhile.
    bool isHolding_ = false;
    MediaSessionSource& Source() { return monitor_->Source(); }

    NowPlaying nowPlaying_;
    std::unique_ptr<Lyrics> lyrics_;
    std::string lyricsKey_;
    uintptr_t lyricsGeneration_ = 0;
    bool isLoadingLyrics_ = false;
    // The title playing when next or previous was pressed, until the player reports another.
    std::optional<std::string> skippingFrom_;
    std::shared_ptr<const Artwork> shownArtwork_;
    bool isOpen_ = false;

    HomeControl hovered_ = HomeControl::None;
    HomeControl pressed_ = HomeControl::None;
    // While set, the slider shows this instead of the playback clock.
    std::optional<double> scrubFraction_;
    // The lyrics stage was chosen. Kept through closing, so the island reopens the way it was left.
    bool prefersStage_ = false;
    int stageHover_ = kNoStageSlot;
    // A stage line pressed, answered with a jump to it when released over the same line.
    int pressedStageSlot_ = kNoStageSlot;
};

}  // namespace noitch
