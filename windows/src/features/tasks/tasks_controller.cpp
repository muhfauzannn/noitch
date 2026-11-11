#include "tasks_controller.h"

#include <shellapi.h>

#include <algorithm>
#include <cmath>

namespace noitch {

namespace {

// A quick return to the page shouldn't pay for a new surface.
constexpr UINT kReleaseDelayMs = 10000;

}  // namespace

void TasksController::PageWillShow(Painter& painter, const TasksLayout& layout, float left, float scale) {
    KillTimer(window_, kReleaseTimer);
    isShown_ = true;
    offset_ = 0;
    scrolled_ = 0;
    hoveredRow_ = -1;
    newOnShow_ = watch_->Unviewed();
    if (!scene_.Exists() && FAILED(scene_.Create(*compositor_, painter, layout, left, scale))) scene_.Release();
    Render();
    watch_->MarkViewed();
    watch_->PageWillShow();
}

void TasksController::PageWillHide() {
    isShown_ = false;
    pressed_ = Target::None;
    hoveredRow_ = -1;
    KillTimer(window_, kTickTimer);
    // One-shot, not periodic: it fires once, unless the page is shown again first.
    SetTimer(window_, kReleaseTimer, kReleaseDelayMs, nullptr);
}

void TasksController::WatchChanged() {
    if (!isShown_) return;
    for (const int64_t id : watch_->Unviewed()) newOnShow_.insert(id);
    watch_->MarkViewed();
    Render();
}

void TasksController::Render() {
    if (!isShown_ || !scene_.Exists()) return;
    const double now = TaskNow();
    model_ = TasksPageModel::Make(watch_->Tasks(), newOnShow_, watch_->Settings().IsSignedIn(), watch_->IsChecking(), watch_->Problem(), watch_->AsOf(), now,
                                  offset_, LocalUtcOffset());
    scene_.Paint(model_, hoveredRow_);
    // One shot at the next minute while shown: the countdown and due times are in minutes.
    const double delay = 60 - std::fmod(now, 60) + 0.2;
    SetTimer(window_, kTickTimer, static_cast<UINT>(delay * 1000), nullptr);
}

TasksController::Target TasksController::TargetAt(float x, float y) const {
    if (!scene_.Exists()) return Target::None;
    const TasksLayout& layout = scene_.Layout();
    if (model_.isMessage) return !model_.button.empty() && layout.MessageButton().Contains(x, y) ? Target::Message : Target::None;
    if (layout.OpenButton().Contains(x, y)) return Target::Open;
    for (int i = 0; i < static_cast<int>(model_.rows.size()); ++i) {
        if (layout.Row(i).Contains(x, y)) return static_cast<Target>(static_cast<int>(Target::Row0) + i);
    }
    return Target::None;
}

void TasksController::PointerMoved(float x, float y) {
    const Target target = TargetAt(x, y);
    const int row = target >= Target::Row0 && target <= Target::Row2 ? static_cast<int>(target) - static_cast<int>(Target::Row0) : -1;
    if (row == hoveredRow_) return;
    hoveredRow_ = row;
    scene_.Paint(model_, hoveredRow_);
}

void TasksController::PointerDown(float x, float y) { pressed_ = TargetAt(x, y); }

void TasksController::PointerUp(float x, float y) {
    const Target pressed = pressed_;
    pressed_ = Target::None;
    if (pressed == Target::None || TargetAt(x, y) != pressed) return;
    if (pressed == Target::Message) {
        if (onOpenSettings) onOpenSettings(onOpenSettingsContext);
    } else if (pressed == Target::Open) {
        Open(model_.heroUrl);
    } else {
        const auto row = static_cast<size_t>(static_cast<int>(pressed) - static_cast<int>(Target::Row0));
        if (row < model_.rows.size()) Open(model_.rows[row].url);
    }
}

void TasksController::ScrolledVertically(double delta) {
    if (!isShown_ || model_.isMessage || model_.more.empty()) return;
    scrolled_ += delta;
    const int step = static_cast<int>(scrolled_ / TasksLayout::kRowHeight);
    if (step == 0) return;
    scrolled_ -= step * TasksLayout::kRowHeight;
    const int count = static_cast<int>(watch_->Tasks().size());
    const int next = std::clamp(offset_ - step, 0, std::max(count - TaskRule::kVisibleRows, 0));
    if (next == offset_) return;
    offset_ = next;
    Render();
}

// Only pages on the signed-in site open, so a task's link can't send the browser elsewhere.
void TasksController::Open(const std::string& url) const {
    const std::string site = watch_->Settings().site;
    if (url.compare(0, site.size(), site) != 0 || (url.size() > site.size() && url[site.size()] != '/')) return;
    const int length = MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, nullptr, 0);
    std::wstring wide(static_cast<size_t>(std::max(length, 1)), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, wide.data(), length);
    ShellExecuteW(nullptr, L"open", wide.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

bool TasksController::HandleMessage(UINT message, WPARAM wparam, LPARAM) {
    if (message != WM_TIMER) return false;
    if (wparam == kTickTimer) {
        KillTimer(window_, kTickTimer);
        Render();
        return true;
    }
    if (wparam == kReleaseTimer) {
        KillTimer(window_, kReleaseTimer);
        scene_.Release();
        return true;
    }
    return false;
}

}  // namespace noitch
