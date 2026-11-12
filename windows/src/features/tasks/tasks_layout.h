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
    static constexpr float kLoadHeight = 36;
    static constexpr float kBarWidth = 14;
    static constexpr float kBarGap = 10;
    // The tallest a bar grows; an empty day keeps a short stub.
    static constexpr float kBarMax = 21;
    static constexpr float kBarStub = 3;
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

    HomeRect Load() const { return Px({ColumnX(), BodyTop(), ColumnWidth(), kLoadHeight}); }
    // A day's bar, `share` of the busiest day's height, standing on the bars' line.
    HomeRect Bar(int day, float share) const {
        const float height = kBarStub + (kBarMax - kBarStub) * std::clamp(share, 0.0f, 1.0f);
        return Px({ColumnX() + day * (kBarWidth + kBarGap), BarBase() - height, kBarWidth, height});
    }
    HomeRect BarLetter(int day) const { return Px({ColumnX() + day * (kBarWidth + kBarGap) - 4, BarBase() + 2, kBarWidth + 8, 11}); }
    HomeRect LoadCaption() const { return Px(CaptionLogical()); }
    HomeRect LoadDetail() const {
        HomeRect caption = CaptionLogical();
        caption.y += 15;
        return Px(caption);
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
    static constexpr float kOverdueWidth = 104;
    HomeRect More() const {
        const HomeRect last = RowLogical(TaskRule::kVisibleRows - 1);
        return Px({ColumnX() + 6, last.Bottom() + 2, ColumnWidth() - 12 - kOverdueWidth, 12});
    }
    // "Show 2 overdue", at the right end of the line under the list.
    HomeRect OverdueToggle() const {
        const HomeRect last = RowLogical(TaskRule::kVisibleRows - 1);
        return Px({ColumnX() + ColumnWidth() - 6 - kOverdueWidth, last.Bottom() + 2, kOverdueWidth, 12});
    }

    // Under the all-clear tick everything moves down, so the tick stays clear of the header band and
    // the notch.
    HomeRect Message(bool withCheck) const { return Px({kSidePadding, BodyTop() + 22 + Shift(withCheck), width_ - kSidePadding * 2, 20}); }
    HomeRect MessageDetail(bool withCheck) const { return Px({kSidePadding, BodyTop() + 44 + Shift(withCheck), width_ - kSidePadding * 2, 15}); }
    HomeRect MessageButton(bool withCheck) const { return Px({(width_ - 116) / 2, BodyTop() + 66 + Shift(withCheck), 116, 22}); }
    HomeRect Check() const { return Px({(width_ - 30) / 2, BodyTop() + 2, 30, 30}); }

    float BodyTop() const { return header_ + kHeaderGap; }

private:
    float ColumnX() const { return kSidePadding + kHeroWidth + kColumnGap; }
    float ColumnWidth() const { return width_ - ColumnX() - kSidePadding; }
    float BarBase() const { return BodyTop() + 23; }
    HomeRect CaptionLogical() const {
        const float x = ColumnX() + TaskRule::kLoadDays * (kBarWidth + kBarGap) + 8;
        return {x, BodyTop() + 3, ColumnX() + ColumnWidth() - x, 14};
    }
    static float Shift(bool withCheck) { return withCheck ? 16.0f : 0.0f; }
    HomeRect RowLogical(int index) const {
        return {ColumnX(), BodyTop() + kLoadHeight + 4 + index * (kRowHeight + kRowGap), ColumnWidth(), kRowHeight};
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
