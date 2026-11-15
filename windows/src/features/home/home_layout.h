#pragma once

// Where everything in Home sits, in pixels from the pill's top-left corner. No Windows headers, so it
// is unit-testable on any host. The macOS app uses the same numbers.

#include <algorithm>
#include <cstdlib>

namespace noitch {

struct HomeRect {
    float x = 0;
    float y = 0;
    float width = 0;
    float height = 0;

    float Right() const { return x + width; }
    float Bottom() const { return y + height; }
    bool Contains(float px, float py) const { return px >= x && px < Right() && py >= y && py < Bottom(); }
    bool Intersects(const HomeRect& o) const { return x < o.Right() && o.x < Right() && y < o.Bottom() && o.y < Bottom(); }
};

// Lyrics turns Home into the lyrics stage and back.
enum class HomeControl { None, Artwork, Slider, Shuffle, Previous, PlayPause, Next, Repeat, Lyrics };

inline constexpr HomeControl kHomeControls[] = {
    HomeControl::Artwork, HomeControl::Slider, HomeControl::Shuffle,
    HomeControl::Previous, HomeControl::PlayPause, HomeControl::Next, HomeControl::Repeat, HomeControl::Lyrics,
};

// The lyrics stage's slots run from -1 (the line before) through 0 (being sung) to 2.
inline constexpr int kFirstStageSlot = -1;
inline constexpr int kLastStageSlot = 2;
inline constexpr int kNoStageSlot = -100;

class HomeLayout {
public:
    HomeLayout() = default;

    // `width`, `height` and `headerHeight` are logical. The header is the band the collapsed pill
    // occupies; nothing but header buttons goes there.
    HomeLayout(float width, float height, float headerHeight, float scale)
        : width_(width), height_(height), header_(headerHeight), scale_(scale) {}

    float Width() const { return width_ * scale_; }
    float Height() const { return height_ * scale_; }

    HomeRect Artwork() const { return Px({kSidePadding, BodyTop(), ArtworkSide(), ArtworkSide()}); }
    // The artwork's glow: an ellipse around the artwork that has faded out by the top edge, where the
    // pill meets the black flares, and well before the right side. In pixels.
    float GlowCenterX() const { return (kSidePadding + ArtworkSide() / 2) * scale_; }
    float GlowCenterY() const { return (BodyTop() + ArtworkSide() / 2) * scale_; }
    float GlowRadiusX() const { return width_ * 0.62f * scale_; }
    float GlowRadiusY() const { return (BodyTop() + ArtworkSide() / 2 + 4) * scale_; }
    // Title and artist end before the lyrics button.
    HomeRect Title() const { return Px({ColumnX(), BodyTop() - 2, HeadingWidth(), 20}); }
    HomeRect Artist() const { return Px({ColumnX(), BodyTop() + 17, HeadingWidth(), 17}); }

    // At the end of the title row; on the stage it moves to the page's top-right corner, which is
    // the same place while there is no calendar.
    HomeRect LyricsButton(bool onStage) const {
        const float right = onStage ? width_ - kSidePadding : ColumnX() + ColumnWidth();
        return Px({right - kLyricsButton, BodyTop() - 2, kLyricsButton, kLyricsButton});
    }

    // A stage slot's line: full size for the one being sung, scaled for the rest. In pixels.
    HomeRect StageLine(int slot) const {
        const float offset = slot == 0 ? 0 : (slot > 0 ? 1.0f : -1.0f) * (27 + static_cast<float>(std::abs(slot) - 1) * 23);
        const float height = kStageLineHeight * (slot == 0 ? 1 : kStageScale);
        const float x = kSidePadding + kStageMargin;
        return Px({x, StageCenterY() + offset + (slot > 0 ? 10 : 0) - height / 2, width_ - x * 2, height});
    }

    int StageSlotAt(float x, float y) const {
        for (int slot = kFirstStageSlot; slot <= kLastStageSlot; ++slot) {
            HomeRect line = StageLine(slot);
            line.y -= 2 * scale_;
            line.height += 4 * scale_;
            if (line.Contains(x, y)) return slot;
        }
        return kNoStageSlot;
    }

    // The bright line; the dim one is kLyricStep below it, drawn smaller.
    HomeRect Lyric() const { return Px({ColumnX(), BodyTop() + 38, ColumnWidth(), 19}); }
    HomeRect LyricLine(int index) const { return Px({ColumnX(), BodyTop() + 38 + index * kLyricStep, ColumnWidth(), 19}); }
    HomeRect NextLyric() const { return LyricLine(1); }
    int LyricVisibleRows() const { return std::max(2, static_cast<int>((SliderY() - 4 - BodyTop() - 38) / kLyricStep)); }
    HomeRect ElapsedTime() const { return Px({ColumnX(), SliderY(), kTimeWidth, kSliderHeight}); }
    HomeRect RemainingTime() const { return Px({ColumnX() + ColumnWidth() - kTimeWidth, SliderY(), kTimeWidth, kSliderHeight}); }
    HomeRect SliderTrack() const { return Px(LogicalTrack()); }

    HomeRect Frame(HomeControl control) const {
        switch (control) {
            case HomeControl::Artwork: return Artwork();
            case HomeControl::Slider: {
                const HomeRect track = LogicalTrack();
                return Px({track.x, track.y - 6, track.width, track.height + 12});
            }
            case HomeControl::Shuffle: return TransportButton(0);
            case HomeControl::Previous: return TransportButton(1);
            case HomeControl::PlayPause: return TransportButton(2);
            case HomeControl::Next: return TransportButton(3);
            case HomeControl::Repeat: return TransportButton(4);
            case HomeControl::Lyrics: return LyricsButton(false);
            default: return {};
        }
    }

    // On the stage only the lyrics button answers; the lines are found with StageSlotAt.
    HomeControl ControlAt(float x, float y, bool onStage = false) const {
        if (onStage) return LyricsButton(true).Contains(x, y) ? HomeControl::Lyrics : HomeControl::None;
        for (const HomeControl control : kHomeControls) {
            if (Frame(control).Contains(x, y)) return control;
        }
        return HomeControl::None;
    }

    // Where a point falls along the slider, from 0 to 1.
    double SliderFraction(float x) const {
        const HomeRect track = SliderTrack();
        return std::clamp(static_cast<double>((x - track.x) / track.width), 0.0, 1.0);
    }

private:
    static constexpr float kSidePadding = 22;
    static constexpr float kBottomPadding = 12;
    static constexpr float kHeaderGap = 8;
    static constexpr float kColumnGap = 18;
    static constexpr float kTransportButton = 30;
    static constexpr float kTransportGap = 14;
    static constexpr float kLyricStep = 19;
    static constexpr float kTimeWidth = 40;
    static constexpr float kSliderHeight = 14;
    static constexpr float kSliderTrackHeight = 4;
    static constexpr float kLyricsButton = 22;
    static constexpr float kLyricsButtonGap = 6;
    // The line being sung is this tall; the others are drawn at kStageScale of it.
    static constexpr float kStageLineHeight = 26;
    static constexpr float kStageScale = 0.7f;
    // Each side, so centred lines stay clear of the lyrics button in the corner.
    static constexpr float kStageMargin = 30;

    float BodyTop() const { return header_ + kHeaderGap; }
    float BodyHeight() const { return height_ - BodyTop() - kBottomPadding; }
    float ArtworkSide() const { return std::min(BodyHeight(), std::max(0.0f, width_ - kSidePadding * 2 - kColumnGap - 190)); }
    float ColumnX() const { return kSidePadding + ArtworkSide() + kColumnGap; }
    float ColumnWidth() const { return width_ - ColumnX() - kSidePadding; }
    float HeadingWidth() const { return ColumnWidth() - kLyricsButton - kLyricsButtonGap; }
    // A little above the middle, so two lines fit below the one being sung.
    float StageCenterY() const { return BodyTop() + BodyHeight() * 0.42f; }
    float SliderY() const { return height_ - kBottomPadding - kTransportButton - kSliderHeight - 2; }

    HomeRect LogicalTrack() const {
        return {ColumnX() + kTimeWidth, SliderY() + (kSliderHeight - kSliderTrackHeight) / 2, ColumnWidth() - kTimeWidth * 2, kSliderTrackHeight};
    }

    HomeRect TransportButton(int slot) const {
        const float total = kTransportButton * 5 + kTransportGap * 4;
        return Px({
            ColumnX() + (ColumnWidth() - total) / 2 + static_cast<float>(slot) * (kTransportButton + kTransportGap),
            height_ - kBottomPadding - kTransportButton,
            kTransportButton,
            kTransportButton,
        });
    }

    HomeRect Px(HomeRect r) const { return {r.x * scale_, r.y * scale_, r.width * scale_, r.height * scale_}; }

    float width_ = 0;
    float height_ = 0;
    float header_ = 0;
    float scale_ = 1;
};

}  // namespace noitch
