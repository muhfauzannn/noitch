import CoreGraphics

/// Something in Home that reacts to the pointer.
enum HomeControl: CaseIterable {
    case lyrics
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
    static let bottomPadding: CGFloat = 16
    static let headerGap: CGFloat = 8
    static let columnGap: CGFloat = 16
    static let headerButton: CGFloat = 26
    static let transportButton: CGFloat = 34
    static let transportGap: CGFloat = 12
    static let timeWidth: CGFloat = 40
    static let sliderHeight: CGFloat = 14
    static let sliderTrackHeight: CGFloat = 4

    let size: CGSize
    /// Height of the band the notch occupies. Nothing but header buttons goes there.
    let headerHeight: CGFloat

    private var bodyTop: CGFloat { headerHeight + Self.headerGap }
    private var bodyHeight: CGFloat { size.height - bodyTop - Self.bottomPadding }
    private var columnX: CGFloat { Self.sidePadding + bodyHeight + Self.columnGap }
    private var columnWidth: CGFloat { size.width - columnX - Self.sidePadding }

    var artwork: CGRect { CGRect(x: Self.sidePadding, y: bodyTop, width: bodyHeight, height: bodyHeight) }
    var title: CGRect { CGRect(x: columnX, y: bodyTop, width: columnWidth, height: 20) }
    var artist: CGRect { CGRect(x: columnX, y: bodyTop + 20, width: columnWidth, height: 17) }
    var lyricLine: CGRect { CGRect(x: columnX, y: bodyTop + 38, width: columnWidth, height: 16) }
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
    /// Previous, current and next line in the lyrics panel.
    func panelLine(_ index: Int) -> CGRect {
        let height = (bodyHeight - Self.transportButton) / 3
        return CGRect(x: columnX, y: bodyTop + CGFloat(index) * height, width: columnWidth, height: height)
    }

    private var sliderY: CGFloat { bodyTop + 58 }

    func frame(of control: HomeControl) -> CGRect {
        switch control {
        // Slot 0 is the shared header's settings button.
        case .lyrics: headerButton(slot: 1)
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

    private func headerButton(slot: CGFloat) -> CGRect {
        let side = Self.headerButton
        return CGRect(
            x: size.width - Self.sidePadding - side - slot * (side + 4),
            y: max((headerHeight - side) / 2, 0),
            width: side,
            height: side
        )
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
