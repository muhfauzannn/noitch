#pragma once

#include <windows.h>

#include "app_settings.h"

namespace noitch {

// A plain Win32 window, created on first use and destroyed on close.
class SettingsWindow {
public:
    void (*onChange)(void* context, const AppSettings& settings) = nullptr;
    void* context = nullptr;

    void Show(HINSTANCE instance, const AppSettings& settings);

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
    LRESULT HandleMessage(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
    void BuildControls();

    HWND hwnd_ = nullptr;
    HFONT font_ = nullptr;
    AppSettings settings_;
};

}  // namespace noitch
