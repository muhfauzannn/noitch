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
    /// The calendar panel takes the right side, and the music column narrows for it.
    var showsCalendar = false

    private var bodyTop: CGFloat { headerHeight + Self.headerGap }
    private var bodyHeight: CGFloat { size.height - bodyTop - Self.bottomPadding }
    private var columnX: CGFloat { Self.sidePadding + bodyHeight + Self.columnGap }
    /// The music column ends before the calendar when there is one.
    private var columnWidth: CGFloat {
        (showsCalendar ? calendarPanel.minX - Self.calendarGap * 2 : size.width - Self.sidePadding) - columnX
    }

    // MARK: Calendar

    static let calendarWidth: CGFloat = 150
    /// Each side of the divider between music and calendar.
    static let calendarGap: CGFloat = 14
    static let calendarRowHeight: CGFloat = 18

    var calendarPanel: CGRect {
        CGRect(x: size.width - Self.sidePadding - Self.calendarWidth, y: bodyTop - 2, width: Self.calendarWidth, height: size.height - bodyTop + 2 - Self.bottomPadding)
    }
    var calendarDivider: CGRect { CGRect(x: calendarPanel.minX - Self.calendarGap, y: bodyTop, width: 1, height: calendarPanel.height - 2) }

    /// A weekday letter over its date, one of seven across the panel.
    func calendarDay(_ index: Int) -> CGRect {
        let width = Self.calendarWidth / 7
        return CGRect(x: calendarPanel.minX + CGFloat(index) * width, y: calendarPanel.minY, width: width, height: 34)
    }
    func calendarDayIndex(at point: CGPoint) -> Int? {
        (0..<7).first { calendarDay($0).contains(point) }
    }
    var calendarHeading: CGRect { CGRect(x: calendarPanel.minX, y: calendarPanel.minY + 38, width: Self.calendarWidth, height: 14) }
    func calendarRow(_ index: Int) -> CGRect {
        CGRect(x: calendarPanel.minX, y: calendarHeading.maxY + 3 + CGFloat(index) * Self.calendarRowHeight, width: Self.calendarWidth, height: Self.calendarRowHeight)
    }
    /// "+2 more", right-aligned on the heading's line.
    var calendarMore: CGRect { calendarHeading }
    var calendarMessage: CGRect { calendarRow(0) }

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
        // Beside the calendar the column can be narrower than the usual row; the gaps give first.
        let gap = min(Self.transportGap, max((columnWidth - side * 5) / 4, 2))
        let total = side * 5 + gap * 4
        return CGRect(
            x: columnX + (columnWidth - total) / 2 + slot * (side + gap),
            y: size.height - Self.bottomPadding - side,
            width: side,
            height: side
        )
    }
}
