#include "island_window.h"

#include <shellscalingapi.h>

namespace noitch {

namespace {

constexpr wchar_t kClassName[] = L"NoitchIsland";
constexpr UINT_PTR kSettleTimer = 1;
constexpr UINT kQuitCommand = 1;

double NowSeconds() {
    LARGE_INTEGER frequency;
    LARGE_INTEGER counter;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&counter);
    return static_cast<double>(counter.QuadPart) / static_cast<double>(frequency.QuadPart);
}

}  // namespace

HRESULT IslandWindow::Create(HINSTANCE instance) {
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = WndProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = kClassName;
    if (!RegisterClassExW(&windowClass)) return HRESULT_FROM_WIN32(GetLastError());

    // NOREDIRECTIONBITMAP: no GDI backing store. All pixels come from the composition tree.
    constexpr DWORD style = WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_NOREDIRECTIONBITMAP;
    if (!CreateWindowExW(style, kClassName, L"Noitch", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, instance, this)) {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    const HRESULT hr = compositor_.Create(hwnd_);
    if (FAILED(hr)) return hr;
    ApplyLayout();

    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    if (CursorInCollapsedHitRegion()) {
        TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd_, 0};
        trackingLeave_ = TrackMouseEvent(&track) != FALSE;
        OpenIfPointerInNotch();
    }
    return S_OK;
}

LRESULT CALLBACK IslandWindow::WndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_NCCREATE) {
        auto* self = static_cast<IslandWindow*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    auto* self = reinterpret_cast<IslandWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    return self ? self->HandleMessage(message, wparam, lparam) : DefWindowProcW(hwnd, message, wparam, lparam);
}

LRESULT IslandWindow::HandleMessage(UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case WM_MOUSEMOVE:
        case WM_MOUSEWHEEL:
            if (!trackingLeave_) {
                // One-shot: the system posts WM_MOUSELEAVE on exit, so hover costs nothing at rest.
                TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd_, 0};
                trackingLeave_ = TrackMouseEvent(&track) != FALSE;
            }
            if (CursorInCollapsedHitRegion()) OpenIfPointerInNotch();
            return 0;
        case WM_MOUSELEAVE:
            trackingLeave_ = false;
            TransitionTo(IslandState::Collapsed);
            if (CursorInCollapsedHitRegion()) OpenIfPointerInNotch();
            return 0;
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_RBUTTONUP:
            ShowMenu();
            return 0;
        case WM_TIMER:
            if (wparam == kSettleTimer) FinishTransition();
            return 0;
        case WM_DISPLAYCHANGE:
        case WM_DPICHANGED:
            ApplyLayout();
            compositor_.RecoverIfLost(geometry_.PillFor(state_));
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(hwnd_, message, wparam, lparam);
    }
}

void IslandWindow::ApplyLayout() {
    const HMONITOR monitor = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    GetMonitorInfoW(monitor, &info);
    UINT dpiX = USER_DEFAULT_SCREEN_DPI;
    UINT dpiY = USER_DEFAULT_SCREEN_DPI;
    GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);

    geometry_ = IslandGeometry(static_cast<float>(dpiX) / USER_DEFAULT_SCREEN_DPI);
    const WindowSize size = geometry_.Window();
    const RECT& screen = info.rcMonitor;
    const int x = screen.left + (screen.right - screen.left - size.width) / 2;
    SetWindowPos(hwnd_, HWND_TOPMOST, x, screen.top, size.width, size.height, SWP_NOACTIVATE);

    KillTimer(hwnd_, kSettleTimer);
    const Pill pill = geometry_.PillFor(state_);
    motion_.Jump(pill);
    UpdateHitRegion();
    compositor_.SetFlareRadius(geometry_.FlareRadius());
    compositor_.ShowPill(pill);
}

void IslandWindow::TransitionTo(IslandState next) {
    if (next == state_) return;
    state_ = next;
    motion_.Retarget(geometry_.PillFor(next), NowSeconds());
    // Grow the hit region before expanding and shrink it after collapsing.
    if (next == IslandState::Expanded) {
        UpdateHitRegion();
    }
    compositor_.AnimatePill(motion_);
    // One-shot, not periodic: it fires once, when DWM has finished the curve.
    SetTimer(hwnd_, kSettleTimer, static_cast<UINT>(motion_.Duration() * 1000) + 1, nullptr);
}

void IslandWindow::FinishTransition() {
    KillTimer(hwnd_, kSettleTimer);
    // Replacing the animations with constants lets DWM release them.
    compositor_.ShowPill(motion_.Target());
    if (state_ == IslandState::Collapsed) {
        UpdateHitRegion();
        if (CursorInCollapsedHitRegion()) OpenIfPointerInNotch();
    }
}

void IslandWindow::OpenIfPointerInNotch() {
    if (state_ == IslandState::Collapsed && CursorInCollapsedHitRegion()) {
        TransitionTo(IslandState::Expanded);
    }
}

bool IslandWindow::CursorInCollapsedHitRegion() const {
    POINT cursor{};
    if (!GetCursorPos(&cursor) || !ScreenToClient(hwnd_, &cursor)) return false;
    const Pill collapsed = geometry_.PillFor(IslandState::Collapsed);
    const int flare = geometry_.FlareRadius();
    const RECT hitRegion{
        static_cast<int>(collapsed.left) - flare,
        0,
        static_cast<int>(collapsed.right) + flare,
        static_cast<int>(collapsed.bottom),
    };
    return PtInRect(&hitRegion, cursor) != FALSE;
}

// The window has a fixed size, so the region is what keeps it from blocking clicks around the island.
void IslandWindow::UpdateHitRegion() {
    if (state_ == IslandState::Expanded) {
        SetWindowRgn(hwnd_, nullptr, FALSE);
        return;
    }
    const Pill pill = geometry_.PillFor(IslandState::Collapsed);
    const int flare = geometry_.FlareRadius();
    SetWindowRgn(hwnd_, CreateRectRgn(static_cast<int>(pill.left) - flare, 0, static_cast<int>(pill.right) + flare, static_cast<int>(pill.bottom)), FALSE);
}

void IslandWindow::ShowMenu() {
    const HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, kQuitCommand, L"Quit Noitch");
    POINT cursor;
    GetCursorPos(&cursor);
    // Without foreground status the menu would not dismiss on an outside click.
    SetForegroundWindow(hwnd_);
    const UINT command = static_cast<UINT>(TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, cursor.x, cursor.y, 0, hwnd_, nullptr));
    DestroyMenu(menu);
    if (command == kQuitCommand) DestroyWindow(hwnd_);
}

}  // namespace noitch
