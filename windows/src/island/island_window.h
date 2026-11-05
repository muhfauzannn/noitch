#pragma once

#include <windows.h>

#include "compositor.h"
#include "features/home/home_controller.h"
#include "features/settings/app_settings.h"
#include "features/settings/settings_window.h"
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
    void OpenIfPointerInNotch();
    bool CursorInCollapsedHitRegion() const;
    bool CursorInWindow() const;
    void OpenSettings();
    float PointerX(LPARAM lparam) const;
    float PointerY(LPARAM lparam) const;

    HWND hwnd_ = nullptr;
    HINSTANCE instance_ = nullptr;
    Compositor compositor_;
    IslandGeometry geometry_;
    PillMotion motion_;
    IslandState state_ = IslandState::Collapsed;
    bool trackingLeave_ = false;
    int compositorGeneration_ = 0;
    AppSettings settings_;
    HomeController home_;
    SettingsWindow settingsWindow_;
};

}  // namespace noitch
