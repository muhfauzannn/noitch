#include "usage_controller.h"

#include <cmath>

namespace noitch {

namespace {

// A quick return to the page shouldn't pay for a new surface and a fresh read of every log.
constexpr UINT kReleaseDelayMs = 10000;

}  // namespace

void UsageController::PageWillShow(Painter& painter, const UsageLayout& layout, float left, float scale) {
    KillTimer(window_, kReleaseTimer);
    if (!scene_.Exists() && FAILED(scene_.Create(*compositor_, painter, layout, left, scale))) scene_.Release();
    Refresh();
}

void UsageController::PageWillHide() {
    KillTimer(window_, kTickTimer);
    // One-shot, not periodic: it fires once, unless the page is shown again first.
    SetTimer(window_, kReleaseTimer, kReleaseDelayMs, nullptr);
}

void UsageController::DeviceLost() {
    // The surface went with the old device. The next showing makes a new one.
    scene_.Release();
}

void UsageController::Release() {
    KillTimer(window_, kReleaseTimer);
    KillTimer(window_, kTickTimer);
    scene_.Release();
    source_.reset();
}

void UsageController::Refresh() {
    if (!scene_.Exists()) return;
    if (!source_) source_ = std::make_unique<UsageSource>();
    const double now = UnixNow();
    scene_.Paint(MakeUsageModel(Summarize(source_->Refresh(now), now, LocalDayStart()), now));
    // One shot at the next minute, only while shown: the countdowns are in minutes, and new log
    // lines are picked up on the same look.
    const double delay = 60 - std::fmod(now, 60) + 0.5;
    SetTimer(window_, kTickTimer, static_cast<UINT>(delay * 1000), nullptr);
}

bool UsageController::HandleMessage(UINT message, WPARAM wparam) {
    if (message != WM_TIMER) return false;
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
}

}  // namespace noitch
