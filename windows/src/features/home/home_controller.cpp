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
        source_.SetArtworkPixels(static_cast<int>(layout.Artwork().width + 0.5f));
        shownArtwork_.reset();
        hasShown_ = false;
    }
    source_.Start(window_, now_);
    Refresh();
}

void HomeController::IslandWillClose() {
    isOpen_ = false;
    KillTimer(window_, kTickTimer);
    hovered_ = HomeControl::None;
    pressed_ = HomeControl::None;
    scrubFraction_.reset();
    scene_.SetHover(HomeControl::None, false, shown_, now_());
    // One-shot, not periodic: it fires once, unless the island reopens first.
    SetTimer(window_, kReleaseTimer, kReleaseDelayMs, nullptr);
}

// Nothing that holds an image, text or a subscription outlives this.
void HomeController::Release() {
    KillTimer(window_, kReleaseTimer);
    source_.Stop();
    scene_.Release();
    shownArtwork_.reset();
    hasShown_ = false;
    nowPlaying_ = NowPlaying{};
    lyrics_.reset();
    lyricsKey_.clear();
    ++lyricsGeneration_;
    isLoadingLyrics_ = false;
}

void HomeController::Apply(const AppSettings& settings) {
    settings_ = settings;
    RequestLyricsIfNeeded();
    if (scene_.Exists()) {
        const auto& artwork = shownArtwork_;
        const uint32_t tint = artwork && settings_.tintWithArtwork ? ArtworkTint(artwork->pixels.data(), artwork->pixels.size()) : 0xFFFFFFFF;
        scene_.SetArtwork(artwork, tint);
    }
    Refresh();
}

void HomeController::DeviceLost() {
    const bool wasOpen = isOpen_;
    scene_.Release();
    if (wasOpen) IslandWillOpen(layout_, left_, scale_);
}

bool HomeController::HandleMessage(UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case MediaSessionSource::kMessage:
            if (source_.HandleMessage(wparam, lparam)) ReceiveMedia();
            return true;
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
    nowPlaying_ = source_.State();
    RequestLyricsIfNeeded();
    if (source_.CurrentArtwork() != shownArtwork_) {
        shownArtwork_ = source_.CurrentArtwork();
        const uint32_t tint = shownArtwork_ && settings_.tintWithArtwork ? ArtworkTint(shownArtwork_->pixels.data(), shownArtwork_->pixels.size()) : 0xFFFFFFFF;
        scene_.SetArtwork(shownArtwork_, tint);
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
    const HomeModel model = MakeHomeModel(nowPlaying_, lyrics_.get(), isLoadingLyrics_, CurrentPosition(), showsLyricsPanel_, settings_);
    // A new track or the lyrics panel opening or closing. The per-second time text never counts.
    const bool swapped = hasShown_ && (model.showsLyricsPanel != shown_.showsLyricsPanel || model.title != shown_.title || model.artist != shown_.artist);
    scene_.Paint(model);
    if (swapped) scene_.FadeInContent(now_());
    shown_ = model;
    hasShown_ = true;
    ScheduleTick();
}

// One shot at a time, only while open and playing. Paused or closed, nothing is scheduled.
void HomeController::ScheduleTick() {
    KillTimer(window_, kTickTimer);
    if (!isOpen_ || !nowPlaying_.isPlaying || scrubFraction_) return;
    const double position = CurrentPosition();
    const double nextLyric = lyrics_ ? lyrics_->NextChange(position) : -1;
    const double delay = NextHomeTick(position, nowPlaying_.rate, nextLyric);
    SetTimer(window_, kTickTimer, static_cast<UINT>(delay * 1000) + 1, nullptr);
}

void HomeController::PointerMoved(float x, float y) {
    if (pressed_ == HomeControl::Slider && scrubFraction_) {
        scrubFraction_ = layout_.SliderFraction(x);
        Refresh();
        return;
    }
    if (pressed_ != HomeControl::None) return;
    const HomeControl control = layout_.ControlAt(x, y);
    if (control == hovered_) return;
    hovered_ = control;
    scene_.SetHover(hovered_, false, shown_, now_());
}

void HomeController::PointerDown(float x, float y) {
    const HomeControl control = layout_.ControlAt(x, y);
    if (control == HomeControl::None) return;
    pressed_ = control;
    hovered_ = control;
    if (control == HomeControl::Slider && nowPlaying_.HasTrack() && !showsLyricsPanel_) {
        scrubFraction_ = layout_.SliderFraction(x);
        Refresh();
    }
    scene_.SetHover(control, true, shown_, now_());
}

void HomeController::PointerUp(float x, float y) {
    const HomeControl control = pressed_;
    if (control == HomeControl::None) return;
    pressed_ = HomeControl::None;
    if (control == HomeControl::Slider && scrubFraction_) {
        FinishScrub(*scrubFraction_);
    } else if (layout_.ControlAt(x, y) == control) {
        Activate(control);
    }
    hovered_ = layout_.ControlAt(x, y);
    Refresh();
    scene_.SetHover(hovered_, false, shown_, now_());
}

void HomeController::FinishScrub(double fraction) {
    scrubFraction_.reset();
    const double position = fraction * nowPlaying_.duration;
    // Shown at once. The session's own update follows and corrects any difference.
    nowPlaying_.elapsed = position;
    nowPlaying_.timestamp = now_();
    source_.Seek(position);
}

void HomeController::Activate(HomeControl control) {
    switch (control) {
        case HomeControl::Settings:
            if (onOpenSettings) onOpenSettings(onOpenSettingsContext);
            break;
        case HomeControl::Artwork:
            source_.OpenApp();
            break;
        case HomeControl::Lyrics:
            if (settings_.showLyrics) showsLyricsPanel_ = !showsLyricsPanel_;
            break;
        case HomeControl::PlayPause:
            if (!nowPlaying_.HasTrack()) break;
            nowPlaying_.elapsed = nowPlaying_.Position(now_());
            nowPlaying_.timestamp = now_();
            nowPlaying_.isPlaying = !nowPlaying_.isPlaying;
            source_.TogglePlayPause();
            break;
        case HomeControl::Previous:
            if (nowPlaying_.HasTrack()) source_.Previous();
            break;
        case HomeControl::Next:
            if (nowPlaying_.HasTrack()) source_.Next();
            break;
        case HomeControl::Shuffle:
            if (!nowPlaying_.HasTrack() || !settings_.showShuffleAndRepeat) break;
            nowPlaying_.isShuffled = !nowPlaying_.isShuffled;
            source_.SetShuffle(nowPlaying_.isShuffled);
            break;
        case HomeControl::Repeat:
            if (!nowPlaying_.HasTrack() || !settings_.showShuffleAndRepeat) break;
            nowPlaying_.repeatMode = NextRepeatMode(nowPlaying_.repeatMode);
            source_.SetRepeat(nowPlaying_.repeatMode);
            break;
        default:
            break;
    }
}

}  // namespace noitch
