#pragma once

// The pages the expanded island shows, the header they share, and the swipe between them. No
// Windows headers, so it is unit-testable on any host. The macOS app uses the same numbers.

#include <algorithm>
#include <cmath>
#include <limits>

#include "features/home/home_layout.h"

namespace noitch {

// In tab order.
enum class IslandPage { Home, Usage };
inline constexpr int kPageCount = 2;

inline bool NeighborPage(IslandPage page, int step, IslandPage* next) {
    const int index = static_cast<int>(page) + step;
    if (index < 0 || index >= kPageCount) return false;
    *next = static_cast<IslandPage>(index);
    return true;
}

// The compositor layer that holds a page. Layer 0 is the header.
inline constexpr int PageLayer(IslandPage page) { return 1 + static_cast<int>(page); }

enum class HeaderControl { None, HomeTab, UsageTab, Settings };

inline constexpr HeaderControl TabFor(IslandPage page) {
    return page == IslandPage::Home ? HeaderControl::HomeTab : HeaderControl::UsageTab;
}

// Page tabs at the left of the band beside the notch, settings at the right. Pages keep their own
// buttons clear of these. In pixels from the pill's top-left corner.
class HeaderLayout {
public:
    HeaderLayout() = default;
    HeaderLayout(float width, float headerHeight, float scale) : width_(width), header_(headerHeight), scale_(scale) {}

    HomeRect Frame(HeaderControl control) const {
        const float y = std::max((header_ - kButton) / 2, 0.0f);
        switch (control) {
            case HeaderControl::HomeTab: return Px({kSidePadding, y, kButton, kButton});
            case HeaderControl::UsageTab: return Px({kSidePadding + kButton + kGap, y, kButton, kButton});
            case HeaderControl::Settings: return Px({width_ - kSidePadding - kButton, y, kButton, kButton});
            default: return {};
        }
    }

    HeaderControl ControlAt(float x, float y) const {
        for (const HeaderControl control : {HeaderControl::HomeTab, HeaderControl::UsageTab, HeaderControl::Settings}) {
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
