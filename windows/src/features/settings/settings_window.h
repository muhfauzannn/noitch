#pragma once

#include <windows.h>

#include <string>
#include <vector>

#include "app_settings.h"
#include "settings_pane.h"

namespace noitch {

// A plain Win32 window with a sidebar of pages, one per feature, created on first use and destroyed
// on close. Only the page on screen has controls.
class SettingsWindow {
public:
    void (*onChange)(void* context, const AppSettings& settings) = nullptr;
    void* context = nullptr;

    void Show(HINSTANCE instance, const AppSettings& settings);

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
    LRESULT HandleMessage(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
    void BuildSidebar();
    void ShowPage(int index);
    void Changed(int row, HWND control);
    void UpdateEnabled();
    int Scaled(int value) const;
    // The Moodle account block of the tasks page: sign-in fields, or who is signed in.
    int BuildAccount(int left, int width, int y);
    void StartSignIn(bool withKey);
    void FinishSignIn(LPARAM result);
    void SignOut();
    void Report(const std::wstring& text);

    HWND hwnd_ = nullptr;
    HWND sidebar_ = nullptr;
    HFONT font_ = nullptr;
    HFONT headingFont_ = nullptr;
    HFONT titleFont_ = nullptr;
    HFONT smallFont_ = nullptr;
    UINT dpi_ = USER_DEFAULT_SCREEN_DPI;
    AppSettings settings_;
    std::vector<SettingsPane> panes_;
    int page_ = 0;
    // The page on screen: every control it made, the rows in order, and each row's control.
    std::vector<HWND> pageControls_;
    std::vector<HWND> secondary_;
    std::vector<const SettingsRow*> rows_;
    std::vector<HWND> rowControls_;
    HWND siteEdit_ = nullptr;
    HWND userEdit_ = nullptr;
    HWND passwordEdit_ = nullptr;
    HWND keyEdit_ = nullptr;
    HWND signInButton_ = nullptr;
    HWND useKeyButton_ = nullptr;
    HWND statusLabel_ = nullptr;
    // A sign-in answer that arrives after the window or page changed is dropped.
    uintptr_t signInGeneration_ = 0;
    std::string pendingSite_;
};

}  // namespace noitch
