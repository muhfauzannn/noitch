#pragma once

// The pages the expanded island shows, the header they share, and the swipe between them. No
// Windows headers, so it is unit-testable on any host. The macOS app uses the same numbers.

#include <algorithm>
#include <cmath>
#include <limits>

#include "features/home/home_layout.h"

namespace noitch {

// In tab order.
enum class IslandPage { Home, Usage, Tasks };
inline constexpr int kPageCount = 3;
inline constexpr IslandPage kAllPages[kPageCount] = {IslandPage::Home, IslandPage::Usage, IslandPage::Tasks};

// Which pages have a tab. Home is always there.
struct VisiblePages {
    bool usage = true;
    bool tasks = true;

    bool Shows(IslandPage page) const {
        return page == IslandPage::Home || (page == IslandPage::Usage && usage) || (page == IslandPage::Tasks && tasks);
    }
    // The tab slot of a page that is shown.
    int Slot(IslandPage page) const {
        int slot = 0;
        for (const IslandPage candidate : kAllPages) {
            if (candidate == page) return slot;
            if (Shows(candidate)) ++slot;
        }
        return slot;
    }
    bool operator==(const VisiblePages&) const = default;
};

// The page `step` places away among the shown ones.
inline bool NeighborPage(IslandPage page, int step, VisiblePages visible, IslandPage* next) {
    IslandPage shown[kPageCount];
    int count = 0;
    int at = -1;
    for (const IslandPage candidate : kAllPages) {
        if (!visible.Shows(candidate)) continue;
        if (candidate == page) at = count;
        shown[count++] = candidate;
    }
    const int index = at + step;
    if (at < 0 || index < 0 || index >= count) return false;
    *next = shown[index];
    return true;
}

// The compositor layer that holds a page. Layer 0 is the header.
inline constexpr int PageLayer(IslandPage page) { return 1 + static_cast<int>(page); }

enum class HeaderControl { None, HomeTab, UsageTab, TasksTab, Settings };

inline constexpr HeaderControl TabFor(IslandPage page) {
    return page == IslandPage::Home ? HeaderControl::HomeTab : page == IslandPage::Usage ? HeaderControl::UsageTab : HeaderControl::TasksTab;
}

// Page tabs at the left of the band beside the notch, settings at the right. Pages keep their own
// buttons clear of these. In pixels from the pill's top-left corner. A page turned off has no tab.
class HeaderLayout {
public:
    HeaderLayout() = default;
    HeaderLayout(float width, float headerHeight, float scale, VisiblePages pages = {})
        : width_(width), header_(headerHeight), scale_(scale), pages_(pages) {}

    const VisiblePages& Pages() const { return pages_; }

    HomeRect Frame(HeaderControl control) const {
        const float y = std::max((header_ - kButton) / 2, 0.0f);
        const auto tab = [&](IslandPage page) -> HomeRect {
            if (!pages_.Shows(page)) return {};
            return Px({kSidePadding + pages_.Slot(page) * (kButton + kGap), y, kButton, kButton});
        };
        switch (control) {
            case HeaderControl::HomeTab: return tab(IslandPage::Home);
            case HeaderControl::UsageTab: return tab(IslandPage::Usage);
            case HeaderControl::TasksTab: return tab(IslandPage::Tasks);
            case HeaderControl::Settings: return Px({width_ - kSidePadding - kButton, y, kButton, kButton});
            default: return {};
        }
    }

    HeaderControl ControlAt(float x, float y) const {
        for (const HeaderControl control : {HeaderControl::HomeTab, HeaderControl::UsageTab, HeaderControl::TasksTab, HeaderControl::Settings}) {
            if (Frame(control).Contains(x, y)) return control;
        }
        return HeaderControl::None;
    }

private:
    static constexpr float kSidePadding = 22;
    static constexpr float kButton = 26;
    static constexpr float kGap = 4;

    HomeRect Px(HomeRect r) const { return {r.x * scale_, r.y * scale_, r.width * scale_, r.height * scale_}; }

    float width_ = 0;
    float header_ = 0;
    float scale_ = 1;
    VisiblePages pages_;
};

// Turns a stream of horizontal wheel deltas into at most one page step per gesture, so a long
// swipe can't skip a page. A pause longer than kGestureGap starts a new gesture. Positive is forward.
class PageSwipe {
public:
    static constexpr double kThreshold = 40;
    static constexpr double kGestureGap = 0.25;

    int Add(double delta, double time) {
        if (time - lastTime_ > kGestureGap) {
            accumulated_ = 0;
            consumed_ = false;
        }
        lastTime_ = time;
        if (consumed_) return 0;
        accumulated_ += delta;
        if (std::fabs(accumulated_) < kThreshold) return 0;
        consumed_ = true;
        return accumulated_ > 0 ? 1 : -1;
    }

private:
    double accumulated_ = 0;
    double lastTime_ = -std::numeric_limits<double>::infinity();
    bool consumed_ = false;
};

}  // namespace noitch
