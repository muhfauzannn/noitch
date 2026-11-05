#pragma once

#include <windows.h>

#include "compositor.h"
#include "island_geometry.h"
#include "spring.h"

namespace noitch {

// Owns the island's window and state. Every entry point here is a window message; nothing polls.
class IslandWindow {
public:
    HRESULT Create(HINSTANCE instance);

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
    LRESULT HandleMessage(UINT message, WPARAM wparam, LPARAM lparam);

    void ApplyLayout();
    void TransitionTo(IslandState next);
    void FinishTransition();
    void UpdateHitRegion();
    void ShowMenu();
    bool CursorInCollapsedHitRegion() const;

    HWND hwnd_ = nullptr;
    Compositor compositor_;
    IslandGeometry geometry_;
    PillMotion motion_;
    IslandState state_ = IslandState::Collapsed;
    bool trackingLeave_ = false;
    bool ignoreHoverUntilLeave_ = false;
    bool hoverOpenPending_ = false;
    bool windowHasExpandedHitRegion_ = false;
};

}  // namespace noitch
