#include "island_window.h"

#include <shellscalingapi.h>
#include <windowsx.h>

#include "features/settings/settings_store.h"

namespace noitch {

namespace {

constexpr wchar_t kClassName[] = L"NoitchIsland";
constexpr UINT_PTR kSettleTimer = 1;
constexpr UINT kQuitCommand = 1;
constexpr UINT kSettingsCommand = 2;
// Lets the pill open far enough that content lands inside it, not at its edge.
constexpr double kRevealDelay = 0.06;
constexpr double kRevealFade = 0.3;
constexpr double kRevealSlide = 0.42;
constexpr double kHideFade = 0.14;
// Dips, before scaling: content drops this far out of the notch while it fades in.
constexpr float kRevealLift = 8;

double NowSeconds() {
    LARGE_INTEGER frequency;
    LARGE_INTEGER counter;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&counter);
    return static_cast<double>(counter.QuadPart) / static_cast<double>(frequency.QuadPart);
}

}  // namespace

HRESULT IslandWindow::Create(HINSTANCE instance) {
    instance_ = instance;
    settings_ = LoadSettings();
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
    pages_.Initialize(hwnd_, &compositor_, NowSeconds);
    pages_.Apply(settings_);
    pages_.onOpenSettings = [](void* context) { static_cast<IslandWindow*>(context)->OpenSettings(); };
    pages_.onOpenSettingsContext = this;
    settingsWindow_.context = this;
    settingsWindow_.onChange = [](void* context, const AppSettings& settings) {
        auto* self = static_cast<IslandWindow*>(context);
        self->settings_ = settings;
        self->pages_.Apply(settings);
    };
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

float IslandWindow::PointerX(LPARAM lparam) const {
    return static_cast<float>(GET_X_LPARAM(lparam)) - geometry_.PillFor(IslandState::Expanded).left;
}

float IslandWindow::PointerY(LPARAM lparam) const {
    return static_cast<float>(GET_Y_LPARAM(lparam));
}

LRESULT IslandWindow::HandleMessage(UINT message, WPARAM wparam, LPARAM lparam) {
    if (pages_.HandleMessage(message, wparam, lparam)) return 0;
    switch (message) {
        case WM_LBUTTONDOWN:
            if (state_ == IslandState::Expanded) {
                // Keeps a slider drag ours when the pointer leaves the island.
                SetCapture(hwnd_);
                pages_.PointerDown(PointerX(lparam), PointerY(lparam));
            }
            return 0;
        case WM_LBUTTONUP:
            if (GetCapture() == hwnd_) ReleaseCapture();
            if (state_ == IslandState::Expanded) {
                pages_.PointerUp(PointerX(lparam), PointerY(lparam));
                // A slider drag can end outside the island, where no leave message will follow.
                if (!CursorInWindow()) TransitionTo(IslandState::Collapsed);
            } else if (!settings_.openOnHover) {
                TransitionTo(IslandState::Expanded);
            }
            return 0;
        case WM_MOUSEMOVE:
            if (state_ == IslandState::Expanded) pages_.PointerMoved(PointerX(lparam), PointerY(lparam));
            [[fallthrough]];
        case WM_MOUSEHWHEEL:
            if (message == WM_MOUSEHWHEEL && state_ == IslandState::Expanded) {
                // A notch is 120; a third of one is the swipe threshold's worth of points. Precision
                // touchpads send small steps that add up the same way.
                const double delta = GET_WHEEL_DELTA_WPARAM(wparam) / 3.0;
                pages_.Scrolled(delta, static_cast<double>(GetMessageTime()) / 1000.0);
            }
            [[fallthrough]];
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
            if (pages_.IsInteracting()) return 0;
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
            if (compositor_.Generation() != compositorGeneration_) {
                compositorGeneration_ = compositor_.Generation();
                pages_.DeviceLost();
            }
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
    const double now = NowSeconds();
    motion_.Retarget(geometry_.PillFor(next), now, SpringFor(next));
    // Grow the hit region before expanding and shrink it after collapsing.
    if (next == IslandState::Expanded) {
        UpdateHitRegion();
        pages_.IslandWillOpen(IslandGeometry::ExpandedWidth(), IslandGeometry::ExpandedHeight(), IslandGeometry::CollapsedHeight(),
                              geometry_.PillFor(IslandState::Expanded).left, geometry_.Scale());
        AnimateContent(true, now);
    } else {
        pages_.IslandWillClose();
        AnimateContent(false, now);
    }
    compositor_.AnimatePill(motion_);
    // One-shot, not periodic: it fires once, when DWM has finished every curve.
    const double end = std::max({motion_.Duration(), contentOpacity_.End() - now, contentOffset_.End() - now});
    SetTimer(hwnd_, kSettleTimer, static_cast<UINT>(end * 1000) + 1, nullptr);
}

// Windows cannot read values back from DWM, so a reversal starts from the analytic value.
void IslandWindow::AnimateContent(bool show, double now) {
    const float opacity = static_cast<float>(contentOpacity_.ValueAt(now));
    const float offset = static_cast<float>(contentOffset_.ValueAt(now));
    if (show) {
        // From hidden, reveal after the pill has a head start. Reopened mid-fade, continue from what
        // is on screen instead of flashing back to empty.
        const bool fromHidden = opacity < 0.05f;
        const double delay = fromHidden ? kRevealDelay : 0;
        const float lift = -kRevealLift * geometry_.Scale();
        contentOpacity_ = {now, delay, kRevealFade, fromHidden ? 0.0f : opacity, 1.0f};
        contentOffset_ = {now, delay, kRevealSlide, fromHidden ? lift : offset, 0.0f};
    } else {
        contentOpacity_ = {now, 0, kHideFade, opacity, 0.0f};
        contentOffset_ = {now, 0, kHideFade, offset, offset};
    }
    compositor_.AnimateContent(contentOpacity_, contentOffset_);
}

void IslandWindow::FinishTransition() {
    KillTimer(hwnd_, kSettleTimer);
    // Replacing the animations with constants lets DWM release them. ShowPill commits both.
    compositor_.SettleContent(contentOpacity_.to, contentOffset_.to);
    compositor_.ShowPill(motion_.Target());
    if (state_ == IslandState::Collapsed) {
        UpdateHitRegion();
        if (CursorInCollapsedHitRegion()) OpenIfPointerInNotch();
    }
}

void IslandWindow::OpenIfPointerInNotch() {
    if (state_ == IslandState::Collapsed && settings_.openOnHover && CursorInCollapsedHitRegion()) {
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

bool IslandWindow::CursorInWindow() const {
    POINT cursor{};
    RECT window{};
    return GetCursorPos(&cursor) && GetWindowRect(hwnd_, &window) && PtInRect(&window, cursor);
}

void IslandWindow::OpenSettings() {
    settingsWindow_.Show(instance_, settings_);
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
    AppendMenuW(menu, MF_STRING, kSettingsCommand, L"Settings…");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kQuitCommand, L"Quit Noitch");
    POINT cursor;
    GetCursorPos(&cursor);
    // Without foreground status the menu would not dismiss on an outside click.
    SetForegroundWindow(hwnd_);
    const UINT command = static_cast<UINT>(TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, cursor.x, cursor.y, 0, hwnd_, nullptr));
    DestroyMenu(menu);
    if (command == kQuitCommand) DestroyWindow(hwnd_);
    if (command == kSettingsCommand) OpenSettings();
}

}  // namespace noitch
