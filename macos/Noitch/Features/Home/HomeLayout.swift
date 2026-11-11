import CoreGraphics

/// Something in Home that reacts to the pointer.
enum HomeControl: CaseIterable {
    case artwork
    case slider
    case shuffle
    case previous
    case playPause
    case next
    case repeatMode
}

/// Where everything in Home sits, measured from the pill's top-left corner with y growing downward.
/// The Windows app uses the same numbers.
struct HomeLayout: Equatable {
    static let sidePadding: CGFloat = 22
    static let bottomPadding: CGFloat = 12
    static let headerGap: CGFloat = 8
    static let columnGap: CGFloat = 18
    static let transportButton: CGFloat = 30
    static let transportGap: CGFloat = 14
    static let timeWidth: CGFloat = 40
    static let sliderHeight: CGFloat = 14
    static let sliderTrackHeight: CGFloat = 4
    /// How far apart the bright lyric and the dim one below it sit; also how far a line slides.
    static let lyricStep: CGFloat = 19
    /// The dim line is drawn at the bright line's size and scaled down, so a line can grow into
    /// place by transform alone, without redrawing its text.
    static let nextLyricScale: CGFloat = 0.84

    let size: CGSize
    /// Height of the band the notch occupies. Nothing but header buttons goes there.
    let headerHeight: CGFloat

    private var bodyTop: CGFloat { headerHeight + Self.headerGap }
    private var bodyHeight: CGFloat { size.height - bodyTop - Self.bottomPadding }
    private var columnX: CGFloat { Self.sidePadding + bodyHeight + Self.columnGap }
    private var columnWidth: CGFloat { size.width - columnX - Self.sidePadding }

    var artwork: CGRect { CGRect(x: Self.sidePadding, y: bodyTop, width: bodyHeight, height: bodyHeight) }

    /// The artwork's glow: an ellipse around the artwork that has faded out by the top edge, where the
    /// pill meets the black flares, and well before the right side.
    var glowCenter: CGPoint { CGPoint(x: artwork.midX, y: artwork.midY) }
    var glowRadii: CGSize { CGSize(width: size.width * 0.62, height: artwork.midY + 4) }
    var title: CGRect { CGRect(x: columnX, y: bodyTop - 2, width: columnWidth, height: 20) }
    var artist: CGRect { CGRect(x: columnX, y: bodyTop + 17, width: columnWidth, height: 17) }
    /// The bright line. The dim one is `lyricStep` below it.
    var lyric: CGRect { CGRect(x: columnX, y: bodyTop + 38, width: columnWidth, height: 19) }
    var nextLyric: CGRect { lyric.offsetBy(dx: 0, dy: Self.lyricStep) }
    var elapsedTime: CGRect { CGRect(x: columnX, y: sliderY, width: Self.timeWidth, height: Self.sliderHeight) }
    var remainingTime: CGRect {
        CGRect(x: columnX + columnWidth - Self.timeWidth, y: sliderY, width: Self.timeWidth, height: Self.sliderHeight)
    }
    var sliderTrack: CGRect {
        CGRect(
            x: columnX + Self.timeWidth,
            y: sliderY + (Self.sliderHeight - Self.sliderTrackHeight) / 2,
            width: columnWidth - Self.timeWidth * 2,
            height: Self.sliderTrackHeight
        )
    }

    private var sliderY: CGFloat { size.height - Self.bottomPadding - Self.transportButton - Self.sliderHeight - 2 }

    func frame(of control: HomeControl) -> CGRect {
        switch control {
        case .artwork: artwork
        case .slider: sliderTrack.insetBy(dx: 0, dy: -6)
        case .shuffle: transportButton(slot: 0)
        case .previous: transportButton(slot: 1)
        case .playPause: transportButton(slot: 2)
        case .next: transportButton(slot: 3)
        case .repeatMode: transportButton(slot: 4)
        }
    }

    func control(at point: CGPoint) -> HomeControl? {
        HomeControl.allCases.first { frame(of: $0).contains(point) }
    }

    /// Where a point falls along the slider, from 0 to 1.
    func sliderFraction(at x: CGFloat) -> Double {
        min(max(Double((x - sliderTrack.minX) / sliderTrack.width), 0), 1)
    }

    private func transportButton(slot: CGFloat) -> CGRect {
        let side = Self.transportButton
        let total = side * 5 + Self.transportGap * 4
        return CGRect(
            x: columnX + (columnWidth - total) / 2 + slot * (side + Self.transportGap),
            y: size.height - Self.bottomPadding - side,
            width: side,
            height: side
        )
    }
}
