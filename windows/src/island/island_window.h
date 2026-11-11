#pragma once

#include <windows.h>

#include "compositor.h"
#include "features/settings/app_settings.h"
#include "features/live/live_controller.h"
#include "features/now_playing/now_playing_monitor.h"
#include "features/settings/settings_window.h"
#include "island_geometry.h"
#include "page_controller.h"
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
    void AnimateContent(bool show, double now);
    void UpdateHitRegion();
    void ShowMenu();
    void OpenIfPointerInNotch();
    bool CursorInCollapsedHitRegion() const;
    bool CursorInWindow() const;
    void OpenSettings();
    void LiveChanged(bool active);
    float PointerX(LPARAM lparam) const;
    float PointerY(LPARAM lparam) const;

    HWND hwnd_ = nullptr;
    HINSTANCE instance_ = nullptr;
    Compositor compositor_;
    IslandGeometry geometry_;
    PillMotion motion_;
    // Content starts hidden; the open reveal fades it in and drops it out of the notch.
    ContentFade contentOpacity_;
    ContentFade contentOffset_;
    IslandState state_ = IslandState::Collapsed;
    bool trackingLeave_ = false;
    bool hoverPending_ = false;
    int compositorGeneration_ = 0;
    AppSettings settings_;
    NowPlayingMonitor monitor_;
    PageController pages_;
    LiveController live_;
    SettingsWindow settingsWindow_;
};

}  // namespace noitch
