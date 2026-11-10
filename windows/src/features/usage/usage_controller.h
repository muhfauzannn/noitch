#pragma once

#include <windows.h>

#include <memory>

#include "island/compositor.h"
#include "island/painter.h"
#include "ai_limits.h"
#include "claude_limit_source.h"
#include "usage_layout.h"
#include "usage_scene.h"
#include "usage_source.h"

namespace noitch {

// Owns what the usage page knows. The pager tells it when the page is shown and hidden; while
// hidden, nothing is read or scheduled, and after a grace period nothing is kept.
class UsageController {
public:
    static constexpr UINT_PTR kTickTimer = 4;
    static constexpr UINT_PTR kReleaseTimer = 5;

    void Initialize(HWND window, Compositor* compositor) {
        window_ = window;
        compositor_ = compositor;
    }

    void PageWillShow(Painter& painter, const UsageLayout& layout, float left, float scale);
    void PageWillHide();
    void DeviceLost();
    bool Exists() const { return scene_.Exists(); }

    // True for a message the usage page owns.
    bool HandleMessage(UINT message, WPARAM wparam, LPARAM lparam);

private:
    void Release();
    void Refresh();
    void Render();

    HWND window_ = nullptr;
    Compositor* compositor_ = nullptr;
    UsageScene scene_;
    std::unique_ptr<UsageSource> source_;
    ToolLimits claudeLimits_ = ToolLimits::Failed(LimitProblem::Loading);
    // A result from an earlier request, posted after the page was hidden or asked again, is dropped.
    uintptr_t claudeGeneration_ = 0;
};

}  // namespace noitch
