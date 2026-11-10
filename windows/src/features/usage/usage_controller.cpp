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
    ++claudeGeneration_;
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
    claudeLimits_ = ToolLimits::Failed(LimitProblem::Loading);
}

void UsageController::Refresh() {
    if (!scene_.Exists()) return;
    if (!source_) source_ = std::make_unique<UsageSource>();
    const double now = UnixNow();
    source_->Refresh(now);
    Render();
    ClaudeLimitSource::Fetch(window_, ++claudeGeneration_, now);
    // One shot at the next minute, only while shown: the countdowns are in minutes, and new log
    // lines and limits are picked up on the same look.
    const double delay = 60 - std::fmod(now, 60) + 0.5;
    SetTimer(window_, kTickTimer, static_cast<UINT>(delay * 1000), nullptr);
}

// Combines the latest limits with the local logs. Cheap: no reading, no request.
void UsageController::Render() {
    if (!scene_.Exists() || !source_) return;
    const double now = UnixNow();
    const UsageLog& log = source_->Log();
    ToolLimits claude = claudeLimits_;
    if (claude.fiveHour) claude.recentRate = ClaudeRate(log.Entries(), *claude.fiveHour, now);
    ToolLimits codex = ToolLimits::Failed(LimitProblem::NoData);
    if (!log.CodexSamples().empty()) {
        codex = ToolLimits{};
        codex.fiveHour = log.CodexSamples().back().fiveHour;
        codex.week = log.CodexSamples().back().week;
        codex.recentRate = CodexRate(log.CodexSamples(), now);
    }
    scene_.Paint(MakeUsageModel(claude, codex, Summarize(log, now, LocalDayStart()), now, LocalClock));
}

bool UsageController::HandleMessage(UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == ClaudeLimitSource::kMessage) {
        std::unique_ptr<ToolLimits> limits(reinterpret_cast<ToolLimits*>(lparam));
        if (wparam == claudeGeneration_ && limits) {
            claudeLimits_ = *limits;
            Render();
        }
        return true;
    }
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
