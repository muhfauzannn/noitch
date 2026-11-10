#pragma once

// Where everything in Home sits, in pixels from the pill's top-left corner. No Windows headers, so it
// is unit-testable on any host. The macOS app uses the same numbers.

#include <algorithm>

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

enum class HomeControl { None, Lyrics, Artwork, Slider, Shuffle, Previous, PlayPause, Next, Repeat };

inline constexpr HomeControl kHomeControls[] = {
    HomeControl::Lyrics, HomeControl::Artwork, HomeControl::Slider, HomeControl::Shuffle,
    HomeControl::Previous, HomeControl::PlayPause, HomeControl::Next, HomeControl::Repeat,
};

class HomeLayout {
public:
    HomeLayout() = default;

    // `width`, `height` and `headerHeight` are logical. The header is the band the collapsed pill
    // occupies; nothing but header buttons goes there.
    HomeLayout(float width, float height, float headerHeight, float scale)
        : width_(width), height_(height), header_(headerHeight), scale_(scale) {}

    float Width() const { return width_ * scale_; }
    float Height() const { return height_ * scale_; }

    HomeRect Artwork() const { return Px({kSidePadding, BodyTop(), BodyHeight(), BodyHeight()}); }
    HomeRect Title() const { return Px({ColumnX(), BodyTop(), ColumnWidth(), 20}); }
    HomeRect Artist() const { return Px({ColumnX(), BodyTop() + 20, ColumnWidth(), 17}); }
    HomeRect LyricLine() const { return Px({ColumnX(), BodyTop() + 38, ColumnWidth(), 16}); }
    HomeRect ElapsedTime() const { return Px({ColumnX(), SliderY(), kTimeWidth, kSliderHeight}); }
    HomeRect RemainingTime() const { return Px({ColumnX() + ColumnWidth() - kTimeWidth, SliderY(), kTimeWidth, kSliderHeight}); }
    HomeRect SliderTrack() const { return Px(LogicalTrack()); }

    // Previous, current and next line in the lyrics panel.
    HomeRect PanelLine(int index) const {
        const float height = (BodyHeight() - kTransportButton) / 3;
        return Px({ColumnX(), BodyTop() + static_cast<float>(index) * height, ColumnWidth(), height});
    }

    HomeRect Frame(HomeControl control) const {
        switch (control) {
            // Slot 0 is the shared header's settings button.
            case HomeControl::Lyrics: return HeaderButton(1);
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
            default: return {};
        }
    }

    HomeControl ControlAt(float x, float y) const {
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
    static constexpr float kBottomPadding = 16;
    static constexpr float kHeaderGap = 8;
    static constexpr float kColumnGap = 16;
    static constexpr float kHeaderButton = 26;
    static constexpr float kTransportButton = 34;
    static constexpr float kTransportGap = 12;
    static constexpr float kTimeWidth = 40;
    static constexpr float kSliderHeight = 14;
    static constexpr float kSliderTrackHeight = 4;

    float BodyTop() const { return header_ + kHeaderGap; }
    float BodyHeight() const { return height_ - BodyTop() - kBottomPadding; }
    float ColumnX() const { return kSidePadding + BodyHeight() + kColumnGap; }
    float ColumnWidth() const { return width_ - ColumnX() - kSidePadding; }
    float SliderY() const { return BodyTop() + 58; }

    HomeRect LogicalTrack() const {
        return {ColumnX() + kTimeWidth, SliderY() + (kSliderHeight - kSliderTrackHeight) / 2, ColumnWidth() - kTimeWidth * 2, kSliderTrackHeight};
    }

    HomeRect HeaderButton(int slot) const {
        return Px({
            width_ - kSidePadding - kHeaderButton - static_cast<float>(slot) * (kHeaderButton + 4),
            std::max((header_ - kHeaderButton) / 2, 0.0f),
            kHeaderButton,
            kHeaderButton,
        });
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
