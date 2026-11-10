#pragma once

// Where everything on the usage page sits, in pixels from the pill's top-left corner: a column for
// Claude Code and one for Codex. No Windows headers. The macOS app uses the same numbers.

#include "features/home/home_layout.h"

namespace noitch {

class UsageLayout {
public:
    static constexpr float kTrackHeight = 4;

    UsageLayout() = default;
    UsageLayout(float width, float height, float headerHeight, float scale) : width_(width), height_(height), header_(headerHeight), scale_(scale) {}

    float Width() const { return width_ * scale_; }
    float Height() const { return height_ * scale_; }
    float ColumnWidth() const { return (width_ - kSidePadding * 2 - kColumnGap) / 2 * scale_; }

    HomeRect Title(int column) const { return Px({X(column), BodyTop(), LogicalColumn(), 16}); }
    HomeRect Tokens(int column) const { return Px({X(column), BodyTop() + 16, LogicalColumn() * 0.6f, 26}); }
    HomeRect Cost(int column) const { return Px({X(column) + LogicalColumn() * 0.6f, BodyTop() + 16, LogicalColumn() * 0.4f, 26}); }
    HomeRect Limit(int column) const { return Px({X(column), BodyTop() + 46, LogicalColumn(), 14}); }
    HomeRect LimitTrack(int column) const { return Px({X(column), BodyTop() + 63, LogicalColumn(), kTrackHeight}); }
    HomeRect Row(int column, int index) const {
        return Px({X(column), BodyTop() + 74 + static_cast<float>(index) * kRowHeight, LogicalColumn(), kRowHeight});
    }

private:
    static constexpr float kSidePadding = 22;
    static constexpr float kColumnGap = 24;
    static constexpr float kHeaderGap = 8;
    static constexpr float kRowHeight = 14;

    float BodyTop() const { return header_ + kHeaderGap; }
    float LogicalColumn() const { return (width_ - kSidePadding * 2 - kColumnGap) / 2; }
    float X(int column) const { return kSidePadding + static_cast<float>(column) * (LogicalColumn() + kColumnGap); }
    HomeRect Px(HomeRect r) const { return {r.x * scale_, r.y * scale_, r.width * scale_, r.height * scale_}; }

    float width_ = 0;
    float height_ = 0;
    float header_ = 0;
    float scale_ = 1;
};

}  // namespace noitch
