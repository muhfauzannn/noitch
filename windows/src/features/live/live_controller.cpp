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
    lyricsEnabled_ = settings.showLiveLyrics && settings.showLyrics;
    lyricsOffset_ = settings.lyricsOffset;
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
    lyricsOffset_ = settings.lyricsOffset;
    const bool lyricsWanted = settings.showLiveLyrics && settings.showLyrics;
    if (lyricsWanted != lyricsEnabled_) {
        lyricsEnabled_ = lyricsWanted;
        if (!lyricsWanted) DropLyrics();
    }
    RequestLyricsIfNeeded();
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
    KillTimer(window_, kLyricTimer);
}

void LiveController::IslandWillClose() {
    islandOpen_ = false;
    // After the pill has narrowed most of the way back to the wings.
    if (active_) scene_.Show(true, 0.22, now_());
    UpdateLyric();
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
    RequestLyricsIfNeeded();
    Evaluate();
}

void LiveController::RequestLyricsIfNeeded() {
    // Without an artist this is usually a browser tab, whose title is neither a song nor ours to send.
    if (!enabled_ || !lyricsEnabled_ || !nowPlaying_.HasTrack() || nowPlaying_.artist.empty()) return;
    const std::string key = nowPlaying_.artist + "\n" + nowPlaying_.title;
    if (key == lyricsKey_) return;
    lyricsKey_ = key;
    lyrics_.reset();
    LyricsSource::LookUp(window_, ++lyricsGeneration_ | kLyricsTag, nowPlaying_.title, nowPlaying_.artist, nowPlaying_.album, nowPlaying_.duration);
}

void LiveController::DropLyrics() {
    // A lookup still out is ignored when it arrives.
    ++lyricsGeneration_;
    lyrics_.reset();
    lyricsKey_.clear();
}

bool LiveController::CurrentLyric(std::string* line) const {
    return LiveRule::LyricLine(lyrics_.get(), nowPlaying_.Position(now_()), lyricsOffset_, line);
}

void LiveController::UpdateLyric() {
    KillTimer(window_, kLyricTimer);
    std::string line;
    if (!showsLyric_ || !CurrentLyric(&line)) line.clear();
    scene_.SetLyric(line);
    if (!showsLyric_ || islandOpen_ || !nowPlaying_.isPlaying || !lyrics_) return;
    const double position = nowPlaying_.Position(now_());
    const double next = lyrics_->NextChange(position + lyricsOffset_);
    if (next < 0) return;
    const double delay = std::max((next - lyricsOffset_ - position) / std::max(nowPlaying_.rate, 0.1), 0.05);
    // One-shot: it fires once, at the next line.
    SetTimer(window_, kLyricTimer, static_cast<UINT>(delay * 1000) + 1, nullptr);
}

void LiveController::Evaluate() {
    const bool active = hasLayout_ && LiveRule::Shows(nowPlaying_, enabled_, pausedSince_, now_());
    std::string line;
    const bool lyric = active && lyricsEnabled_ && CurrentLyric(&line);
    if (active == active_ && lyric == showsLyric_) {
        UpdateLyric();
        return;
    }
    const bool shapeOnly = active == active_;
    active_ = active;
    showsLyric_ = lyric;
    if (shapeOnly) {
        UpdateLyric();
        if (onActiveChange) onActiveChange(context, active);
        return;
    }
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
    UpdateLyric();
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
    KillTimer(window_, kLyricTimer);
    scene_.Release();
    shownArtwork_.reset();
    DropLyrics();
}

void LiveController::ShowArtwork() {
    if (!scene_.Exists()) return;
    const auto artwork = monitor_->Source().CurrentArtwork();
    if (artwork == shownArtwork_ && artwork) return;
    shownArtwork_ = artwork;
    const uint32_t tint = tintBars_ && artwork ? ArtworkTint(artwork->pixels.data(), artwork->pixels.size()) : 0xFFFFFFFF;
    scene_.SetArtwork(artwork, tint);
}

bool LiveController::HandleMessage(UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == LyricsSource::kMessage) {
        // Home's lookups carry no tag; leave those to it.
        if ((wparam & kLyricsTag) == 0) return false;
        std::unique_ptr<Lyrics> found(reinterpret_cast<Lyrics*>(lparam));
        if (wparam != (lyricsGeneration_ | kLyricsTag)) return true;
        lyrics_ = std::move(found);
        Evaluate();
        return true;
    }
    if (message != WM_TIMER) return false;
    if (wparam == kLyricTimer) {
        KillTimer(window_, kLyricTimer);
        UpdateLyric();
        return true;
    }
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
