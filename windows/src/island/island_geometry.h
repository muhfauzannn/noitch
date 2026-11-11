#pragma once

// Pure layout. No Windows headers, so it is unit-testable on any host.

#include "features/live/live_layout.h"
#include "features/tasks/tasks_layout.h"

namespace noitch {

enum class IslandState { Collapsed, Expanded };

// The visible pill, in client-area pixels. Its top edge is always the top of the window.
struct Pill {
    float left;
    float right;
    float bottom;
    float radius;
};

struct WindowSize {
    int width;
    int height;
};

class IslandGeometry {
public:
    explicit IslandGeometry(float scale = 1.0f) : scale_(scale) {}

    // The window never changes size during a transition, so its coordinate space stays fixed.
    // It is the expanded pill plus room for its flares and spring overshoot.
    WindowSize Window() const {
        return {Px(kExpandedWidth + kOvershootMargin * 2), Px(kExpandedHeight + kOvershootMargin)};
    }

    int FlareRadius() const { return Px(kFlareRadius); }

    float Scale() const { return scale_; }

    // Music is playing and the closed pill has wings for it, see LiveLayout.
    void SetLive(bool live) { live_ = live; }
    bool IsLive() const { return live_; }
    // The live pill also shows the line being sung, under the notch.
    void SetLiveLyric(bool lyric) { liveLyric_ = lyric; }
    // A task alert on the closed pill, see TaskAlertLayout.
    void SetTaskAlert(TaskAlertShape shape) { taskAlert_ = shape; }
    static constexpr float CollapsedWidth() { return kCollapsedWidth; }

    // Logical sizes for laying out what sits on the expanded pill.
    static constexpr float ExpandedWidth() { return kExpandedWidth; }
    static constexpr float ExpandedHeight() { return kExpandedHeight; }
    static constexpr float CollapsedHeight() { return kCollapsedHeight; }

    Pill PillFor(IslandState state) const {
        const bool expanded = state == IslandState::Expanded;
        float collapsed = 0;
        float collapsedHeight = 0;
        TaskAlertLayout::PillSize(kCollapsedWidth, kCollapsedHeight, live_, liveLyric_, taskAlert_, &collapsed, &collapsedHeight);
        const float halfWidth = (expanded ? kExpandedWidth : collapsed) * scale_ / 2;
        const float center = Window().width / 2.0f;
        return {
            center - halfWidth,
            center + halfWidth,
            (expanded ? kExpandedHeight : collapsedHeight) * scale_,
            (expanded ? kExpandedRadius : kCollapsedRadius) * scale_,
        };
    }

private:
    // Logical units (1/96 inch). Keep in step with docs/architecture.md, "Shared behavior".
    static constexpr float kCollapsedWidth = 180;
    static constexpr float kCollapsedHeight = 32;
    static constexpr float kCollapsedRadius = 10;
    static constexpr float kExpandedWidth = 580;
    static constexpr float kExpandedHeight = 180;
    static constexpr float kExpandedRadius = 24;
    static constexpr float kFlareRadius = 8;
    static constexpr float kOvershootMargin = 16;

    int Px(float logical) const { return static_cast<int>(logical * scale_ + 0.5f); }

    float scale_;
    bool live_ = false;
    bool liveLyric_ = false;
    TaskAlertShape taskAlert_ = TaskAlertShape::None;
};

}  // namespace noitch
