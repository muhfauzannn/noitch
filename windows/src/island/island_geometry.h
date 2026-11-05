#pragma once

// Pure layout. No Windows headers, so it is unit-testable on any host.

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

    // Logical sizes for laying out what sits on the expanded pill.
    static constexpr float ExpandedWidth() { return kExpandedWidth; }
    static constexpr float ExpandedHeight() { return kExpandedHeight; }
    static constexpr float CollapsedHeight() { return kCollapsedHeight; }

    Pill PillFor(IslandState state) const {
        const bool expanded = state == IslandState::Expanded;
        const float halfWidth = (expanded ? kExpandedWidth : kCollapsedWidth) * scale_ / 2;
        const float center = Window().width / 2.0f;
        return {
            center - halfWidth,
            center + halfWidth,
            (expanded ? kExpandedHeight : kCollapsedHeight) * scale_,
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
};

}  // namespace noitch
