#include "page_controller.h"

#include <algorithm>

namespace noitch {

namespace {

// Same as macOS: a short slide reads as direction, not travel.
constexpr float kPageShift = 24;
constexpr double kPageFadeOut = 0.2;
constexpr double kPageFadeIn = 0.3;
constexpr double kPageFadeInDelay = 0.04;
constexpr double kPageSlide = 0.42;
constexpr double kOutgoingSlide = 0.32;
// The pager outlives a page by a little, so the shared painter is never released under one.
constexpr UINT kReleaseDelayMs = 11000;

}  // namespace

void PageController::Initialize(HWND window, Compositor* compositor, double (*now)(), NowPlayingMonitor* monitor, TaskWatch* watch) {
    window_ = window;
    compositor_ = compositor;
    now_ = now;
    home_.Initialize(window, compositor, now, monitor);
    usage_.Initialize(window, compositor);
    tasks_.Initialize(window, compositor, watch);
    tasks_.onOpenSettings = [](void* context) {
        auto* self = static_cast<PageController*>(context);
        if (self->onOpenSettings) self->onOpenSettings(self->onOpenSettingsContext);
    };
    tasks_.onOpenSettingsContext = this;
}

void PageController::Apply(const AppSettings& settings) {
    home_.Apply(settings.home);
    usage_.Apply(settings.usage);
    pages_ = settings.pages;
    // A page turned off while shown gives way to Home. Its tab goes when the header is next built,
    // on the next opening; until then it does nothing.
    if (!pages_.Shows(page_)) {
        if (isOpen_) {
            Select(IslandPage::Home);
        } else {
            Jump(IslandPage::Home);
        }
    }
}

void PageController::Jump(IslandPage page) {
    page_ = page;
    for (const IslandPage candidate : kAllPages) {
        const int i = static_cast<int>(candidate);
        const float opacity = candidate == page ? 1.0f : 0.0f;
        opacity_[i] = {0, 0, 0, opacity, opacity};
        offset_[i] = {};
        if (compositor_) compositor_->SetLayer(PageLayer(candidate), opacity, 0);
    }
}

void PageController::IslandWillOpen(float width, float height, float headerHeight, float left, float scale, std::optional<IslandPage> preferred) {
    KillTimer(window_, kReleaseTimer);
    isOpen_ = true;
    if (scale != scale_ || left != left_ || headerHeight != headerHeight_) {
        header_.Release();
        usage_.DeviceLost();
        tasks_.DeviceLost();
    }
    if (header_.Exists() && !(header_.Layout().Pages() == pages_.Visible())) header_.Release();
    const IslandPage opening = preferred && pages_.Shows(*preferred) ? *preferred : pages_.Opening(page_);
    if (opening != page_) Jump(opening);
    width_ = width;
    height_ = height;
    headerHeight_ = headerHeight;
    left_ = left;
    scale_ = scale;
    if (!painter_.Exists() && FAILED(painter_.Create(compositor_->DxgiDevice()))) painter_.Release();
    if (painter_.Exists() && !header_.Exists()) {
        const HeaderLayout layout(width, headerHeight, scale, pages_.Visible());
        if (FAILED(header_.Create(*compositor_, painter_, layout, width * scale, headerHeight * scale, left, scale))) header_.Release();
    }
    PaintHeader();
    ShowFeature(page_);
}

void PageController::IslandWillClose() {
    isOpen_ = false;
    HideFeature(page_);
    hovered_ = pressed_ = HeaderControl::None;
    pageHasPointer_ = false;
    PaintHeader();
    // One-shot, not periodic: it fires once, unless the island reopens first.
    SetTimer(window_, kReleaseTimer, kReleaseDelayMs, nullptr);
}

void PageController::Release() {
    KillTimer(window_, kReleaseTimer);
    header_.Release();
    painter_.Release();
}

void PageController::DeviceLost() {
    // Everything drawn came from the old device. Pages rebuild when next shown.
    header_.Release();
    usage_.DeviceLost();
    tasks_.DeviceLost();
    painter_.Release();
    home_.DeviceLost();
    if (isOpen_) IslandWillOpen(width_, height_, headerHeight_, left_, scale_);
}

void PageController::ShowFeature(IslandPage page) {
    if (page == IslandPage::Home) {
        home_.IslandWillOpen(HomeLayout(width_, height_, headerHeight_, scale_), left_, scale_);
    } else if (!painter_.Exists()) {
        return;
    } else if (page == IslandPage::Usage) {
        usage_.PageWillShow(painter_, UsageLayout(width_, height_, headerHeight_, scale_), left_, scale_);
    } else {
        tasks_.PageWillShow(painter_, TasksLayout(width_, height_, headerHeight_, scale_), left_, scale_);
    }
}

void PageController::HideFeature(IslandPage page) {
    if (page == IslandPage::Home) {
        home_.IslandWillClose();
    } else if (page == IslandPage::Usage) {
        usage_.PageWillHide();
    } else {
        tasks_.PageWillHide();
    }
}

void PageController::PaintHeader() {
    header_.Paint(page_, hovered_, pressed_ != HeaderControl::None, pinned_);
}

// The old page slides a little toward where it lives in tab order and fades; the new one comes from
// the other side. Each starts from its analytic on-screen value, so a quick second switch reverses.
void PageController::Select(IslandPage next) {
    if (!isOpen_ || next == page_ || !pages_.Shows(next)) return;
    const IslandPage old = page_;
    HideFeature(old);
    page_ = next;
    ShowFeature(next);

    const double now = now_();
    const float direction = static_cast<int>(next) > static_cast<int>(old) ? 1.0f : -1.0f;
    const float shift = kPageShift * scale_;
    const int o = static_cast<int>(old);
    const int n = static_cast<int>(next);
    opacity_[o] = {now, 0, kPageFadeOut, static_cast<float>(opacity_[o].ValueAt(now)), 0};
    offset_[o] = {now, 0, kOutgoingSlide, static_cast<float>(offset_[o].ValueAt(now)), -direction * shift};
    const float visible = static_cast<float>(opacity_[n].ValueAt(now));
    const float from = visible > 0.01f ? static_cast<float>(offset_[n].ValueAt(now)) : direction * shift;
    opacity_[n] = {now, kPageFadeInDelay, kPageFadeIn, visible, 1};
    offset_[n] = {now, 0, kPageSlide, from, 0};
    compositor_->AnimateLayer(PageLayer(old), opacity_[o], offset_[o]);
    compositor_->AnimateLayer(PageLayer(next), opacity_[n], offset_[n]);
    // The header paint commits, so the pages start moving on the same frame as the new tab.
    PaintHeader();
    // One-shot, not periodic: it fires once, when DWM has finished both curves.
    const double end = std::max(opacity_[n].End(), offset_[n].End()) - now;
    SetTimer(window_, kSettleTimer, static_cast<UINT>(end * 1000) + 1, nullptr);
}

// Replacing the animations with constants lets DWM release them.
void PageController::Settle() {
    for (const IslandPage page : kAllPages) {
        const int i = static_cast<int>(page);
        compositor_->SetLayer(PageLayer(page), opacity_[i].to, offset_[i].to);
    }
    compositor_->Commit();
}

void PageController::Scrolled(double delta, double time) {
    if (!isOpen_ || home_.IsInteracting()) return;
    const int step = swipe_.Add(delta, time);
    IslandPage next = page_;
    if (step != 0 && NeighborPage(page_, step, pages_.Visible(), &next)) Select(next);
}

void PageController::ScrolledVertically(double delta) {
    if (isOpen_ && page_ == IslandPage::Tasks) tasks_.ScrolledVertically(delta);
}

bool PageController::HandleMessage(UINT message, WPARAM wparam, LPARAM lparam) {
    if (home_.HandleMessage(message, wparam, lparam)) return true;
    if (usage_.HandleMessage(message, wparam, lparam)) return true;
    if (tasks_.HandleMessage(message, wparam, lparam)) return true;
    if (message != WM_TIMER) return false;
    if (wparam == kSettleTimer) {
        KillTimer(window_, kSettleTimer);
        Settle();
        return true;
    }
    if (wparam == kReleaseTimer) {
        Release();
        return true;
    }
    return false;
}

void PageController::PointerMoved(float x, float y) {
    if (pressed_ == HeaderControl::None) {
        const HeaderControl control = Header().ControlAt(x, y);
        if (control != hovered_) {
            hovered_ = control;
            PaintHeader();
        }
    }
    if (page_ == IslandPage::Home) home_.PointerMoved(x, y);
    if (page_ == IslandPage::Tasks) tasks_.PointerMoved(x, y);
}

void PageController::PointerDown(float x, float y) {
    const HeaderControl control = Header().ControlAt(x, y);
    if (control != HeaderControl::None) {
        pressed_ = hovered_ = control;
        PaintHeader();
        return;
    }
    pageHasPointer_ = true;
    if (page_ == IslandPage::Home) home_.PointerDown(x, y);
    if (page_ == IslandPage::Tasks) tasks_.PointerDown(x, y);
}

void PageController::PointerUp(float x, float y) {
    if (pressed_ != HeaderControl::None) {
        const HeaderControl control = pressed_;
        pressed_ = HeaderControl::None;
        hovered_ = Header().ControlAt(x, y);
        if (hovered_ == control) {
            if (control == HeaderControl::Settings) {
                if (onOpenSettings) onOpenSettings(onOpenSettingsContext);
            } else if (control == HeaderControl::Pin) {
                if (onTogglePin) onTogglePin(onOpenSettingsContext);
            } else {
                Select(control == HeaderControl::HomeTab    ? IslandPage::Home
                       : control == HeaderControl::UsageTab ? IslandPage::Usage
                                                            : IslandPage::Tasks);
            }
        }
        PaintHeader();
        return;
    }
    if (!pageHasPointer_) return;
    pageHasPointer_ = false;
    if (page_ == IslandPage::Home) home_.PointerUp(x, y);
    if (page_ == IslandPage::Tasks) tasks_.PointerUp(x, y);
}

}  // namespace noitch
