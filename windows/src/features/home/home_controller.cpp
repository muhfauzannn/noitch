#include "home_controller.h"

namespace noitch {

namespace {

// A quick return to the island shouldn't pay for new sessions and a new surface.
constexpr UINT kReleaseDelayMs = 10000;

}  // namespace

void HomeController::IslandWillOpen(const HomeLayout& layout, float left, float scale) {
    KillTimer(window_, kReleaseTimer);
    isOpen_ = true;
    if (!scene_.Exists() || scale != scale_ || left != left_) {
        scene_.Release();
        layout_ = layout;
        left_ = left;
        scale_ = scale;
        if (FAILED(scene_.Create(*compositor_, layout, left, scale))) scene_.Release();
        monitor_->SetArtworkPixels(NowPlayingMonitor::kHome, static_cast<int>(layout.Artwork().width + 0.5f));
        shownArtwork_.reset();
    }
    isHolding_ = true;
    monitor_->SetNeeded(NowPlayingMonitor::kHome, true);
    ReceiveMedia();
}

void HomeController::IslandWillClose() {
    isOpen_ = false;
    KillTimer(window_, kTickTimer);
    hovered_ = HomeControl::None;
    pressed_ = HomeControl::None;
    stageHover_ = kNoStageSlot;
    pressedStageSlot_ = kNoStageSlot;
    scrubFraction_.reset();
    // One-shot, not periodic: it fires once, unless the island reopens first.
    SetTimer(window_, kReleaseTimer, kReleaseDelayMs, nullptr);
}

// Nothing that holds an image, text or a subscription outlives this.
void HomeController::Release() {
    KillTimer(window_, kReleaseTimer);
    KillTimer(window_, kSkipTimer);
    skippingFrom_.reset();
    isHolding_ = false;
    monitor_->SetNeeded(NowPlayingMonitor::kHome, false);
    scene_.Release();
    shownArtwork_.reset();
    nowPlaying_ = NowPlaying{};
    lyrics_.reset();
    lyricsKey_.clear();
    ++lyricsGeneration_;
    isLoadingLyrics_ = false;
}

void HomeController::Apply(const HomeSettings& settings) {
    settings_ = settings;
    RequestLyricsIfNeeded();
    if (scene_.Exists()) ShowArtwork();
    Refresh();
}

void HomeController::ShowArtwork() {
    const auto& artwork = shownArtwork_;
    const uint32_t tint = artwork && (settings_.tintWithArtwork || settings_.artworkGlow) ? ArtworkTint(artwork->pixels.data(), artwork->pixels.size()) : 0xFFFFFFFF;
    scene_.SetArtwork(artwork, settings_.tintWithArtwork ? tint : 0xFFFFFFFF, settings_.artworkGlow ? tint : 0xFFFFFFFF);
}

void HomeController::DeviceLost() {
    const bool wasOpen = isOpen_;
    scene_.Release();
    if (wasOpen) IslandWillOpen(layout_, left_, scale_);
}

bool HomeController::HandleMessage(UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case LyricsSource::kMessage: {
            std::unique_ptr<Lyrics> found(reinterpret_cast<Lyrics*>(lparam));
            if (wparam != lyricsGeneration_) return true;
            lyrics_ = std::move(found);
            isLoadingLyrics_ = false;
            Refresh();
            return true;
        }
        case WM_TIMER:
            if (wparam == kTickTimer) {
                KillTimer(window_, kTickTimer);
                Refresh();
                return true;
            }
            if (wparam == kSkipTimer) {
                KillTimer(window_, kSkipTimer);
                skippingFrom_.reset();
                Refresh();
                return true;
            }
            if (wparam == kReleaseTimer) {
                Release();
                return true;
            }
            return false;
        default:
            return false;
    }
}

void HomeController::ReceiveMedia() {
    nowPlaying_ = Source().State();
    if (skippingFrom_ && nowPlaying_.title != *skippingFrom_) {
        skippingFrom_.reset();
        KillTimer(window_, kSkipTimer);
    }
    RequestLyricsIfNeeded();
    if (Source().CurrentArtwork() != shownArtwork_) {
        shownArtwork_ = Source().CurrentArtwork();
        ShowArtwork();
    }
    Refresh();
}

void HomeController::RequestLyricsIfNeeded() {
    if (!settings_.showLyrics) {
        ++lyricsGeneration_;
        isLoadingLyrics_ = false;
        lyricsKey_.clear();
        return;
    }
    // Without an artist this is usually a browser tab, whose title is neither a song nor ours to send.
    if (!nowPlaying_.HasTrack() || nowPlaying_.artist.empty()) return;
    const std::string key = nowPlaying_.artist + "\n" + nowPlaying_.title;
    if (key == lyricsKey_) return;
    lyricsKey_ = key;
    lyrics_.reset();
    isLoadingLyrics_ = true;
    LyricsSource::LookUp(window_, ++lyricsGeneration_, nowPlaying_.title, nowPlaying_.artist, nowPlaying_.album, nowPlaying_.duration);
}

double HomeController::CurrentPosition() const {
    if (scrubFraction_) return *scrubFraction_ * nowPlaying_.duration;
    return nowPlaying_.Position(now_());
}

void HomeController::Refresh() {
    if (!isOpen_) return;
    scene_.Paint(MakeHomeModel(nowPlaying_, lyrics_.get(), isLoadingLyrics_, CurrentPosition(), settings_, skippingFrom_.has_value(), IsOnStage()),
                 hovered_, pressed_ != HomeControl::None, stageHover_);
    ScheduleTick();
}

// One shot at a time, only while open and playing. Paused or closed, nothing is scheduled.
void HomeController::ScheduleTick() {
    KillTimer(window_, kTickTimer);
    if (!isOpen_ || !nowPlaying_.isPlaying || scrubFraction_) return;
    const double position = CurrentPosition();
    // Lyrics run on their own clock, shifted by the timing setting.
    const double offset = settings_.lyricsOffset;
    const double nextChange = lyrics_ ? lyrics_->NextChange(position + offset) : -1;
    const double nextLyric = nextChange >= 0 ? nextChange - offset : -1;
    // The stage shows no clock, so only the next line wakes it.
    const double delay = NextHomeTick(position, nowPlaying_.rate, nextLyric, !IsOnStage());
    if (delay < 0) return;
    SetTimer(window_, kTickTimer, static_cast<UINT>(delay * 1000) + 1, nullptr);
}

void HomeController::PointerMoved(float x, float y) {
    if (pressed_ == HomeControl::Slider && scrubFraction_) {
        scrubFraction_ = layout_.SliderFraction(x);
        Refresh();
        return;
    }
    if (pressed_ != HomeControl::None) return;
    const HomeControl control = layout_.ControlAt(x, y, IsOnStage());
    const int stageHover = IsOnStage() && StageLineIndex(x, y) >= 0 ? layout_.StageSlotAt(x, y) : kNoStageSlot;
    if (control == hovered_ && stageHover == stageHover_) return;
    hovered_ = control;
    stageHover_ = stageHover;
    Refresh();
}

void HomeController::PointerDown(float x, float y) {
    const HomeControl control = layout_.ControlAt(x, y, IsOnStage());
    if (control == HomeControl::None) {
        if (IsOnStage() && StageLineIndex(x, y) >= 0) pressedStageSlot_ = layout_.StageSlotAt(x, y);
        return;
    }
    pressed_ = control;
    hovered_ = control;
    if (control == HomeControl::Slider && nowPlaying_.HasTrack() && !skippingFrom_) scrubFraction_ = layout_.SliderFraction(x);
    Refresh();
}

void HomeController::PointerUp(float x, float y) {
    if (pressedStageSlot_ != kNoStageSlot) {
        const int slot = pressedStageSlot_;
        pressedStageSlot_ = kNoStageSlot;
        const int line = StageLineIndex(x, y);
        if (layout_.StageSlotAt(x, y) == slot && line >= 0) JumpToLine(line);
        return;
    }
    const HomeControl control = pressed_;
    if (control == HomeControl::None) return;
    pressed_ = HomeControl::None;
    if (control == HomeControl::Slider && scrubFraction_) {
        FinishScrub(*scrubFraction_);
    } else if (layout_.ControlAt(x, y, IsOnStage()) == control) {
        Activate(control);
    }
    hovered_ = layout_.ControlAt(x, y, IsOnStage());
    Refresh();
}

// The synced line shown in the stage slot under the point, or -1 when there is none to jump to.
int HomeController::StageLineIndex(float x, float y) const {
    const int slot = layout_.StageSlotAt(x, y);
    if (slot == kNoStageSlot || !lyrics_ || lyrics_->synced.empty() || !nowPlaying_.HasTrack() || skippingFrom_) return -1;
    const int current = lyrics_->LineIndex(CurrentPosition() + settings_.lyricsOffset);
    // Before the first line the middle slot holds a note, not a line.
    if (current < 0 && slot <= 0) return -1;
    const int line = current + slot;
    return line >= 0 && line < static_cast<int>(lyrics_->synced.size()) ? line : -1;
}

// Seeks to just inside the line, so the timing setting can't leave it a hair before.
void HomeController::JumpToLine(int line) {
    const double position = std::max(lyrics_->synced[static_cast<size_t>(line)].start - settings_.lyricsOffset + 0.02, 0.0);
    nowPlaying_.elapsed = position;
    nowPlaying_.timestamp = now_();
    Source().Seek(position);
    Refresh();
}

void HomeController::FinishScrub(double fraction) {
    scrubFraction_.reset();
    const double position = fraction * nowPlaying_.duration;
    // Shown at once. The session's own update follows and corrects any difference.
    nowPlaying_.elapsed = position;
    nowPlaying_.timestamp = now_();
    Source().Seek(position);
}

// Answers the press at once: players can take a second or more to report the new track. Ends when
// another title arrives, or after a few seconds in case the same track restarted.
void HomeController::BeginSkip() {
    skippingFrom_ = nowPlaying_.title;
    // One-shot, not periodic: it fires once, unless the new track arrives first.
    SetTimer(window_, kSkipTimer, 4000, nullptr);
}

void HomeController::Activate(HomeControl control) {
    switch (control) {
        case HomeControl::Artwork:
            Source().OpenApp();
            break;
        case HomeControl::PlayPause:
            if (!nowPlaying_.HasTrack()) break;
            nowPlaying_.elapsed = nowPlaying_.Position(now_());
            nowPlaying_.timestamp = now_();
            nowPlaying_.isPlaying = !nowPlaying_.isPlaying;
            Source().TogglePlayPause();
            break;
        case HomeControl::Previous:
            if (!nowPlaying_.HasTrack()) break;
            Source().Previous();
            BeginSkip();
            break;
        case HomeControl::Next:
            if (!nowPlaying_.HasTrack()) break;
            Source().Next();
            BeginSkip();
            break;
        case HomeControl::Shuffle:
            if (!nowPlaying_.HasTrack() || !settings_.showShuffleAndRepeat) break;
            nowPlaying_.isShuffled = !nowPlaying_.isShuffled;
            Source().SetShuffle(nowPlaying_.isShuffled);
            break;
        case HomeControl::Repeat:
            if (!nowPlaying_.HasTrack() || !settings_.showShuffleAndRepeat) break;
            nowPlaying_.repeatMode = NextRepeatMode(nowPlaying_.repeatMode);
            Source().SetRepeat(nowPlaying_.repeatMode);
            break;
        case HomeControl::Lyrics:
            if (!settings_.showLyrics) break;
            prefersStage_ = !prefersStage_;
            stageHover_ = kNoStageSlot;
            break;
        default:
            break;
    }
}

}  // namespace noitch
