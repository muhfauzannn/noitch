#pragma once

// Where everything on the tasks page sits, in pixels from the pill's top-left corner, and the closed
// island's alert in logical units. No Windows headers. The macOS app uses the same numbers
// (TasksLayout.swift).

#include <algorithm>

#include "features/home/home_layout.h"
#include "features/live/live_layout.h"
#include "task_model.h"

namespace noitch {

class TasksLayout {
public:
    static constexpr float kSidePadding = 22;
    static constexpr float kHeaderGap = 6;
    static constexpr float kHeroWidth = 178;
    static constexpr float kColumnGap = 22;
    static constexpr float kRunwayHeight = 34;
    static constexpr float kAxisY = 14;
    static constexpr float kDot = 9;
    static constexpr float kRowHeight = 24;
    static constexpr float kRowGap = 1;

    TasksLayout() = default;
    TasksLayout(float width, float height, float headerHeight, float scale) : width_(width), height_(height), header_(headerHeight), scale_(scale) {}

    float Width() const { return width_ * scale_; }
    float Height() const { return height_ * scale_; }
    float Scale() const { return scale_; }

    HomeRect HeroLabel() const { return Px({kSidePadding, BodyTop(), kHeroWidth, 12}); }
    HomeRect Countdown() const { return Px({kSidePadding, BodyTop() + 13, kHeroWidth, 44}); }
    HomeRect HeroTitle() const { return Px({kSidePadding, BodyTop() + 58, kHeroWidth, 17}); }
    HomeRect HeroCourse() const { return Px({kSidePadding, BodyTop() + 77, 44, 14}); }
    HomeRect HeroDue() const { return Px({kSidePadding + 50, BodyTop() + 77, kHeroWidth - 50, 14}); }
    HomeRect OpenButton() const { return Px({kSidePadding, BodyTop() + 97, 104, 20}); }

    HomeRect Runway() const { return Px(RunwayLogical()); }
    // The centre of a dot at `fraction` along the runway, raised one dot per `stack`. Pixels.
    void DotCenter(double fraction, int stack, float* x, float* y) const {
        const HomeRect runway = RunwayLogical();
        const float inset = kDot / 2 + 1;
        *x = (runway.x + inset + static_cast<float>(fraction) * (runway.width - inset * 2)) * scale_;
        *y = (runway.y + kAxisY - static_cast<float>(std::min(stack, 2)) * (kDot - 2)) * scale_;
    }
    HomeRect DayLabel(int day) const {
        const HomeRect runway = RunwayLogical();
        const float width = runway.width / TaskRule::kRunwayDays;
        return Px({runway.x + day * width, runway.y + kAxisY + 7, width, 11});
    }

    HomeRect Row(int index) const { return Px(RowLogical(index)); }
    HomeRect RowIcon(int index) const {
        const HomeRect row = RowLogical(index);
        return Px({row.x + 6, row.y + row.height / 2 - 8, 16, 16});
    }
    HomeRect RowTitle(int index) const {
        const HomeRect row = RowLogical(index);
        return Px({row.x + 30, row.y + row.height / 2 - 7.5f, row.width - 30 - 104, 15});
    }
    HomeRect RowDue(int index) const {
        const HomeRect row = RowLogical(index);
        return Px({row.Right() - 104, row.y + row.height / 2 - 7, 98, 14});
    }
    HomeRect More() const {
        const HomeRect last = RowLogical(TaskRule::kVisibleRows - 1);
        return Px({ColumnX() + 6, last.Bottom() + 2, ColumnWidth() - 12, 12});
    }

    HomeRect Message() const { return Px({kSidePadding, BodyTop() + 30, width_ - kSidePadding * 2, 20}); }
    HomeRect MessageDetail() const { return Px({kSidePadding, BodyTop() + 52, width_ - kSidePadding * 2, 15}); }
    HomeRect MessageButton() const { return Px({(width_ - 92) / 2, BodyTop() + 76, 92, 22}); }
    HomeRect Check() const { return Px({(width_ - 34) / 2, BodyTop() - 8, 34, 34}); }

    float BodyTop() const { return header_ + kHeaderGap; }

private:
    float ColumnX() const { return kSidePadding + kHeroWidth + kColumnGap; }
    float ColumnWidth() const { return width_ - ColumnX() - kSidePadding; }
    HomeRect RunwayLogical() const { return {ColumnX(), BodyTop(), ColumnWidth(), kRunwayHeight}; }
    HomeRect RowLogical(int index) const {
        return {ColumnX(), BodyTop() + kRunwayHeight + 4 + index * (kRowHeight + kRowGap), ColumnWidth(), kRowHeight};
    }
    HomeRect Px(HomeRect r) const { return {r.x * scale_, r.y * scale_, r.width * scale_, r.height * scale_}; }

    float width_ = 580;
    float height_ = 180;
    float header_ = 32;
    float scale_ = 1;
};

// What the closed island shows of the tasks.
enum class TaskAlertShape { None, Slip, Dot };

// The closed island's alert over the same wings and strip as music, with a narrow wing more on each
// side for the dot. Logical units, from the top-left of the alert's own surface, which is centred on
// the pill.
class TaskAlertLayout {
public:
    static constexpr float kDotWing = 14;
    static constexpr float kDotSide = 6;
    static constexpr double kSlipDuration = 8;

    TaskAlertLayout(float collapsedWidth, float collapsedHeight) : width_(collapsedWidth), height_(collapsedHeight) {}

    LiveLayout Live() const { return LiveLayout(width_, height_); }
    float Width() const { return Live().Width() + kDotWing * 2; }
    float Height() const { return height_ + LiveLayout::kLyricStrip; }

    HomeRect Glyph() const {
        HomeRect art = Live().Artwork();
        art.x += kDotWing;
        return art;
    }
    HomeRect Count() const {
        const LiveLayout live = Live();
        const float start = kDotWing + live.Wing() + width_ + LiveLayout::kInnerGap;
        const float width = live.Wing() - LiveLayout::kInnerGap - LiveLayout::kOuterPadding;
        return {start - 4, (height_ - 15) / 2, width + 8, 15};
    }
    HomeRect Strip() const {
        HomeRect strip = Live().Lyric();
        strip.x += kDotWing;
        return strip;
    }
    // In the outermost part of the pill's right side, whether or not the music wings are there.
    HomeRect Dot(bool besideLive) const {
        const float inner = besideLive ? Live().Width() : width_;
        return {Width() / 2 + inner / 2 + kDotWing / 2 - kDotSide / 2 - 1, (height_ - kDotSide) / 2, kDotSide, kDotSide};
    }

    // The closed pill's size, with or without music, for a shape.
    static void PillSize(float collapsedWidth, float collapsedHeight, bool live, bool liveLyric, TaskAlertShape shape, float* width, float* height) {
        const LiveLayout layout(collapsedWidth, collapsedHeight);
        *width = live ? layout.Width() : collapsedWidth;
        *height = live ? layout.Height(liveLyric) : collapsedHeight;
        if (shape == TaskAlertShape::Slip) {
            // The slip takes the music's wings and strip, so it is the same shape with or without music.
            *width = layout.Width();
            *height = layout.Height(true);
        } else if (shape == TaskAlertShape::Dot) {
            *width += kDotWing * 2;
        }
    }

private:
    float width_;
    float height_;
};

}  // namespace noitch
