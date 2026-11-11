#pragma once

#include <windows.h>

#include <set>

#include "island/compositor.h"
#include "island/painter.h"
#include "task_watch.h"
#include "tasks_layout.h"
#include "tasks_scene.h"

namespace noitch {

// Owns the tasks page while it shows. The list itself lives in TaskWatch, which runs whether or not
// the page is open; this only draws it, keeps the countdown current and handles the pointer.
class TasksController {
public:
    static constexpr UINT_PTR kTickTimer = 15;
    static constexpr UINT_PTR kReleaseTimer = 16;

    void Initialize(HWND window, Compositor* compositor, TaskWatch* watch) {
        window_ = window;
        compositor_ = compositor;
        watch_ = watch;
    }

    void PageWillShow(Painter& painter, const TasksLayout& layout, float left, float scale);
    void PageWillHide();
    void DeviceLost() { scene_.Release(); }
    // The watch's list changed. Tasks announced while the page shows count as seen at once.
    void WatchChanged();

    // Points are from the pill's top-left corner, in pixels.
    void PointerMoved(float x, float y);
    void PointerDown(float x, float y);
    void PointerUp(float x, float y);
    // Wheel notches as points, positive when the content should move down.
    void ScrolledVertically(double delta);

    bool HandleMessage(UINT message, WPARAM wparam, LPARAM lparam);

    // Set by the pager, which owns the settings window.
    void (*onOpenSettings)(void* context) = nullptr;
    void* onOpenSettingsContext = nullptr;

private:
    enum class Target { None, Row0, Row1, Row2, Open, Message, Overdue };

    void Render();
    void ToggleOverdue();
    Target TargetAt(float x, float y) const;
    void Open(const std::string& url) const;

    HWND window_ = nullptr;
    Compositor* compositor_ = nullptr;
    TaskWatch* watch_ = nullptr;
    TasksScene scene_;
    TasksPageModel model_;
    bool isShown_ = false;
    int offset_ = 0;
    // Overdue work shows only after the button asks for it, and hides again on the next showing.
    bool showOverdue_ = false;
    double scrolled_ = 0;
    int hoveredRow_ = -1;
    Target pressed_ = Target::None;
    // Highlighted for this showing; the watch forgets them as soon as the page appears.
    std::set<int64_t> newOnShow_;
};

}  // namespace noitch
