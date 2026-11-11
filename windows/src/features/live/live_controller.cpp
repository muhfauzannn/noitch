#include "live_controller.h"

#include "features/home/home_model.h"

namespace noitch {

void LiveController::Initialize(HWND window, Compositor* compositor, NowPlayingMonitor* monitor, double (*now)(), const HomeSettings& settings) {
    window_ = window;
    compositor_ = compositor;
    monitor_ = monitor;
    now_ = now;
    enabled_ = settings.showLive;
    tintBars_ = settings.tintWithArtwork;
    monitor->onLive = [](void* context) { static_cast<LiveController*>(context)->Receive(); };
    monitor->liveContext = this;
    monitor->SetNeeded(NowPlayingMonitor::kLive, enabled_);
}

void LiveController::Apply(const HomeSettings& settings) {
    const bool tintChanged = settings.tintWithArtwork != tintBars_;
    tintBars_ = settings.tintWithArtwork;
    if (settings.showLive != enabled_) {
        enabled_ = settings.showLive;
        monitor_->SetNeeded(NowPlayingMonitor::kLive, enabled_);
        if (!enabled_) nowPlaying_ = NowPlaying{};
    }
    if (tintChanged && scene_.Exists()) {
        shownArtwork_.reset();
        ShowArtwork();
    }
    Evaluate();
}

void LiveController::SetGeometry(float collapsedWidth, float collapsedHeight, int windowWidth, float scale) {
    const LiveLayout layout(collapsedWidth, collapsedHeight);
    if (hasLayout_ && windowWidth == windowWidth_ && scale == scale_ && layout.Width() == layout_.Width() && layout.Height() == layout_.Height()) return;
    hasLayout_ = true;
    layout_ = layout;
    windowWidth_ = windowWidth;
    scale_ = scale;
    monitor_->SetArtworkPixels(NowPlayingMonitor::kLive, static_cast<int>(layout.ArtSide() * scale + 0.5f));
    if (scene_.Exists()) {
        ReleaseScene();
        BuildScene();
        if (!islandOpen_) scene_.Show(true, 0, now_());
    }
    Evaluate();
}

void LiveController::IslandWillOpen() {
    islandOpen_ = true;
    scene_.Show(false, 0, now_());
}

void LiveController::IslandWillClose() {
    islandOpen_ = false;
    // After the pill has narrowed most of the way back to the wings.
    if (active_) scene_.Show(true, 0.22, now_());
}

void LiveController::DeviceLost() {
    // The visuals went with the old device.
    const bool had = scene_.Exists();
    ReleaseScene();
    if (had && active_) {
        BuildScene();
        if (!islandOpen_) scene_.Show(true, 0, now_());
    }
}

void LiveController::Receive() {
    if (!enabled_) return;
    const bool wasPlaying = nowPlaying_.isPlaying && nowPlaying_.HasTrack();
    nowPlaying_ = monitor_->Source().State();
    const bool playing = nowPlaying_.isPlaying && nowPlaying_.HasTrack();
    if (playing) {
        pausedSince_ = -1;
        KillTimer(window_, kPauseTimer);
    } else if (wasPlaying) {
        pausedSince_ = now_();
        // One-shot: it fires once, at the end of the pause grace.
        SetTimer(window_, kPauseTimer, static_cast<UINT>(LiveRule::kPauseGrace * 1000) + 100, nullptr);
    }
    ShowArtwork();
    scene_.SetPlaying(playing);
    Evaluate();
}

void LiveController::Evaluate() {
    const bool active = hasLayout_ && LiveRule::Shows(nowPlaying_, enabled_, pausedSince_, now_());
    if (active == active_) return;
    active_ = active;
    if (active) {
        KillTimer(window_, kReleaseTimer);
        if (!scene_.Exists()) BuildScene();
        // Waits for the pill to widen.
        if (!islandOpen_) scene_.Show(true, 0.12, now_());
    } else {
        scene_.Show(false, 0, now_());
        // One-shot: the visuals go once the fade is over.
        SetTimer(window_, kReleaseTimer, 2000, nullptr);
    }
    if (onActiveChange) onActiveChange(context, active);
}

void LiveController::BuildScene() {
    const float liveWidth = layout_.Width() * scale_;
    const float left = (static_cast<float>(windowWidth_) - liveWidth) / 2;
    if (FAILED(scene_.Create(*compositor_, layout_, left, scale_))) {
        scene_.Release();
        return;
    }
    shownArtwork_.reset();
    ShowArtwork();
    scene_.SetPlaying(nowPlaying_.isPlaying && nowPlaying_.HasTrack());
}

// Nothing holding an image outlives the wings.
void LiveController::ReleaseScene() {
    KillTimer(window_, kReleaseTimer);
    scene_.Release();
    shownArtwork_.reset();
}

void LiveController::ShowArtwork() {
    if (!scene_.Exists()) return;
    const auto artwork = monitor_->Source().CurrentArtwork();
    if (artwork == shownArtwork_ && artwork) return;
    shownArtwork_ = artwork;
    const uint32_t tint = tintBars_ && artwork ? ArtworkTint(artwork->pixels.data(), artwork->pixels.size()) : 0xFFFFFFFF;
    scene_.SetArtwork(artwork, tint);
}

bool LiveController::HandleMessage(UINT message, WPARAM wparam, LPARAM) {
    if (message != WM_TIMER) return false;
    if (wparam == kPauseTimer) {
        KillTimer(window_, kPauseTimer);
        Evaluate();
        return true;
    }
    if (wparam == kReleaseTimer) {
        KillTimer(window_, kReleaseTimer);
        if (!active_) ReleaseScene();
        return true;
    }
    return false;
}

}  // namespace noitch
