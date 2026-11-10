#pragma once

#include <windows.h>

#include "compositor.h"
#include "features/home/home_controller.h"
#include "features/settings/app_settings.h"
#include "features/usage/usage_controller.h"
#include "header_scene.h"
#include "island_page.h"
#include "painter.h"
#include "spring.h"

namespace noitch {

// Owns which page the island shows and routes the pointer to it. Only the current page is "open"
// to its feature, so a page you aren't looking at runs nothing and is released after its grace
// period, exactly as if the island had closed.
class PageController {
public:
    static constexpr UINT_PTR kSettleTimer = 6;
    static constexpr UINT_PTR kReleaseTimer = 7;

    void Initialize(HWND window, Compositor* compositor, double (*now)());
    void Apply(const AppSettings& settings);

    // `left` is the expanded pill's left edge; sizes are logical.
    void IslandWillOpen(float width, float height, float headerHeight, float left, float scale);
    void IslandWillClose();
    void DeviceLost();

    // True for a message a page owns. Call with the island's window messages.
    bool HandleMessage(UINT message, WPARAM wparam, LPARAM lparam);

    // Points are from the pill's top-left corner, in pixels.
    void PointerMoved(float x, float y);
    void PointerDown(float x, float y);
    void PointerUp(float x, float y);
    // `delta` is in points, positive toward the next page; `time` in seconds.
    void Scrolled(double delta, double time);

    bool IsInteracting() const { return home_.IsInteracting(); }

    // Set by the island, which owns the settings window.
    void (*onOpenSettings)(void* context) = nullptr;
    void* onOpenSettingsContext = nullptr;

private:
    void Select(IslandPage next);
    // Shows a page at once, for an island that is out of sight.
    void Jump(IslandPage page);
    HeaderLayout Header() const { return HeaderLayout(width_, headerHeight_, scale_, pages_.showUsage); }
    void ShowFeature(IslandPage page);
    void HideFeature(IslandPage page);
    void PaintHeader();
    void Settle();
    void Release();

    HWND window_ = nullptr;
    Compositor* compositor_ = nullptr;
    double (*now_)() = nullptr;
    HomeController home_;
    UsageController usage_;
    Painter painter_;
    HeaderScene header_;
    PageSettings pages_;
    IslandPage page_ = IslandPage::Home;
    bool isOpen_ = false;
    float width_ = 0;
    float height_ = 0;
    float headerHeight_ = 0;
    float left_ = 0;
    float scale_ = 1;
    PageSwipe swipe_;
    // Windows can't read values back from DWM, so each page's fade is known analytically.
    ContentFade opacity_[kPageCount] = {{0, 0, 0, 1, 1}, {0, 0, 0, 0, 0}};
    ContentFade offset_[kPageCount];

    HeaderControl hovered_ = HeaderControl::None;
    HeaderControl pressed_ = HeaderControl::None;
    // True while a press that began on the page, not the header, is held.
    bool pageHasPointer_ = false;
};

}  // namespace noitch
