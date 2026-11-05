#include "settings_window.h"

#include "settings_store.h"

namespace noitch {

namespace {

constexpr wchar_t kClassName[] = L"NoitchSettings";
constexpr int kWidth = 420;
constexpr int kPadding = 20;
constexpr int kRowHeight = 26;
constexpr int kHeadingHeight = 24;

enum Control {
    kLaunchAtLogin = 100,
    kOpenOnHover,
    kShowLyrics,
    kShowShuffleAndRepeat,
    kTintWithArtwork,
};

}  // namespace

void SettingsWindow::Show(HINSTANCE instance, const AppSettings& settings) {
    settings_ = settings;
    if (hwnd_) {
        SetForegroundWindow(hwnd_);
        return;
    }
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.lpfnWndProc = WndProc;
        windowClass.hInstance = instance;
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        windowClass.lpszClassName = kClassName;
        registered = RegisterClassExW(&windowClass) != 0;
    }
    constexpr DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
    if (!CreateWindowExW(0, kClassName, L"Noitch Settings", style, CW_USEDEFAULT, CW_USEDEFAULT, 0, 0, nullptr, nullptr, instance, this)) return;
    BuildControls();
    ShowWindow(hwnd_, SW_SHOW);
    SetForegroundWindow(hwnd_);
}

void SettingsWindow::BuildControls() {
    const UINT dpi = GetDpiForWindow(hwnd_);
    const auto scaled = [dpi](int value) { return MulDiv(value, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI); };

    NONCLIENTMETRICSW metrics{};
    metrics.cbSize = sizeof(metrics);
    SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0, dpi);
    font_ = CreateFontIndirectW(&metrics.lfMessageFont);

    const HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd_, GWLP_HINSTANCE));
    int y = kPadding;
    const auto add = [&](const wchar_t* type, const wchar_t* text, DWORD style, int id, int height) {
        const HWND control = CreateWindowExW(0, type, text, WS_CHILD | WS_VISIBLE | style, scaled(kPadding), scaled(y),
                                             scaled(kWidth - kPadding * 2), scaled(height), hwnd_,
                                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
        y += height;
        return control;
    };
    const auto heading = [&](const wchar_t* text) { add(L"STATIC", text, 0, 0, kHeadingHeight); };
    const auto toggle = [&](const wchar_t* text, int id, bool on) {
        const HWND box = add(L"BUTTON", text, BS_AUTOCHECKBOX | WS_TABSTOP, id, kRowHeight);
        SendMessageW(box, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
    };

    heading(L"General");
    toggle(L"Launch at login", kLaunchAtLogin, LaunchesAtLogin());
    toggle(L"Open on hover (when off, click the island to open it)", kOpenOnHover, settings_.openOnHover);
    y += 8;
    heading(L"Media");
    toggle(L"Show lyrics (sends the track and artist name to lrclib.net)", kShowLyrics, settings_.showLyrics);
    toggle(L"Show shuffle and repeat", kShowShuffleAndRepeat, settings_.showShuffleAndRepeat);
    toggle(L"Tint progress bar with artwork", kTintWithArtwork, settings_.tintWithArtwork);
    y += 8;
    heading(L"About");
    add(L"STATIC", L"Noitch. Lyrics come from LRCLIB.", 0, 0, kRowHeight);

    RECT frame{0, 0, scaled(kWidth), scaled(y + kPadding)};
    AdjustWindowRectExForDpi(&frame, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE, 0, dpi);
    SetWindowPos(hwnd_, nullptr, 0, 0, frame.right - frame.left, frame.bottom - frame.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

LRESULT CALLBACK SettingsWindow::WndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_NCCREATE) {
        auto* self = static_cast<SettingsWindow*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    auto* self = reinterpret_cast<SettingsWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    return self ? self->HandleMessage(hwnd, message, wparam, lparam) : DefWindowProcW(hwnd, message, wparam, lparam);
}

LRESULT SettingsWindow::HandleMessage(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case WM_COMMAND: {
            const bool on = SendMessageW(reinterpret_cast<HWND>(lparam), BM_GETCHECK, 0, 0) == BST_CHECKED;
            switch (LOWORD(wparam)) {
                case kLaunchAtLogin:
                    if (!SetLaunchesAtLogin(on)) SendMessageW(reinterpret_cast<HWND>(lparam), BM_SETCHECK, on ? BST_UNCHECKED : BST_CHECKED, 0);
                    return 0;
                case kOpenOnHover: settings_.openOnHover = on; break;
                case kShowLyrics: settings_.showLyrics = on; break;
                case kShowShuffleAndRepeat: settings_.showShuffleAndRepeat = on; break;
                case kTintWithArtwork: settings_.tintWithArtwork = on; break;
                default: return 0;
            }
            SaveSettings(settings_);
            if (onChange) onChange(context, settings_);
            return 0;
        }
        case WM_DESTROY:
            if (font_) DeleteObject(font_);
            font_ = nullptr;
            hwnd_ = nullptr;
            return 0;
        default:
            return DefWindowProcW(hwnd, message, wparam, lparam);
    }
}

}  // namespace noitch
