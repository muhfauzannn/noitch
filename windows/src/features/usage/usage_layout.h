#pragma once

// Where everything on the usage page sits, in pixels from the pill's top-left corner. One column per
// tool: a ring gauge, and beside it the numbers and what they mean. No Windows headers. The macOS
// app uses the same numbers (UsageLayout.swift).

#include <algorithm>

#include "features/home/home_layout.h"

namespace noitch {

class UsageLayout {
public:
    static constexpr float kGauge = 88;
    static constexpr float kRingWidth = 7;
    // Radii to the middle of each stroke: the week outside, five hours inside.
    static constexpr float kOuterRadius = (kGauge - kRingWidth) / 2;
    static constexpr float kInnerRadius = kOuterRadius - kRingWidth - 3;

    UsageLayout() = default;
    UsageLayout(float width, float height, float headerHeight, float scale) : width_(width), height_(height), header_(headerHeight), scale_(scale) {}

    float Width() const { return width_ * scale_; }
    float Height() const { return height_ * scale_; }
    float Scale() const { return scale_; }

    HomeRect Gauge(int column) const { return Px({X(column), BodyTop() + (BodyHeight() - kGauge) / 2, kGauge, kGauge}); }
    HomeRect Center(int column) const { return Px({X(column), GaugeMidY() - 14, kGauge, 22}); }
    HomeRect Caption(int column) const { return Px({X(column), GaugeMidY() + 7, kGauge, 12}); }
    HomeRect Name(int column) const { return Px({TextX(column), TextTop(), TextWidth(), 16}); }
    HomeRect Today(int column) const { return Px({TextX(column), TextTop() + 17, TextWidth(), 14}); }
    // Row 0 is five hours, row 1 the week.
    HomeRect RowLabel(int column, int row) const { return Px({TextX(column), RowY(row), 34, 15}); }
    HomeRect RowValue(int column, int row) const { return Px({TextX(column) + 34, RowY(row), 36, 15}); }
    HomeRect RowPace(int column, int row) const { return Px({TextX(column) + 70, RowY(row), 14, 15}); }
    HomeRect RowReset(int column, int row) const { return Px({TextX(column) + 84, RowY(row), TextWidth() - 84, 15}); }
    HomeRect Headline(int column) const { return Px({TextX(column), TextTop() + 72, TextWidth(), 16}); }
    HomeRect Detail(int column) const { return Px({TextX(column), TextTop() + 88, TextWidth(), 14}); }

private:
    static constexpr float kSidePadding = 22;
    static constexpr float kColumnGap = 24;
    static constexpr float kHeaderGap = 8;
    static constexpr float kBottomPadding = 12;
    static constexpr float kGaugeGap = 14;

    float BodyTop() const { return header_ + kHeaderGap; }
    float BodyHeight() const { return height_ - BodyTop() - kBottomPadding; }
    float ColumnWidth() const { return (width_ - kSidePadding * 2 - kColumnGap) / 2; }
    float X(int column) const { return kSidePadding + static_cast<float>(column) * (ColumnWidth() + kColumnGap); }
    float TextX(int column) const { return X(column) + kGauge + kGaugeGap; }
    float TextWidth() const { return ColumnWidth() - kGauge - kGaugeGap; }
    // The text block is 102 points tall; it and the gauge share the body's vertical center.
    float TextTop() const { return BodyTop() + std::max((BodyHeight() - 102) / 2, 0.0f); }
    float GaugeMidY() const { return BodyTop() + BodyHeight() / 2; }
    float RowY(int row) const { return TextTop() + 36 + static_cast<float>(row) * 15; }
    HomeRect Px(HomeRect r) const { return {r.x * scale_, r.y * scale_, r.width * scale_, r.height * scale_}; }

    float width_ = 0;
    float height_ = 0;
    float header_ = 0;
    float scale_ = 1;
};

}  // namespace noitch
