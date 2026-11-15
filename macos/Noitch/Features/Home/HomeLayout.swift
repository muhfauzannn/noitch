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
    /// Turns Home into the lyrics stage and back.
    case lyrics
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
    static let lyricsButton: CGFloat = 22
    static let lyricsButtonGap: CGFloat = 6

    // MARK: Lyrics stage

    /// The line being sung is drawn at this height and the others are scaled down from it, so a line
    /// grows into the middle by transform alone.
    static let stageLineHeight: CGFloat = 26
    static let stageScale: CGFloat = 0.7
    /// Each side, so centred lines stay clear of the lyrics button in the corner.
    static let stageMargin: CGFloat = 30
    /// Slots run from -2 (gone above) through 0 (being sung) to 3 (about to rise in below).
    static let stageSlots = -1...2

    let size: CGSize
    /// Height of the band the notch occupies. Nothing but header buttons goes there.
    let headerHeight: CGFloat
    /// The calendar panel takes the right side, and the music column narrows for it.
    var showsCalendar = false

    private var bodyTop: CGFloat { headerHeight + Self.headerGap }
    private var bodyHeight: CGFloat { size.height - bodyTop - Self.bottomPadding }
    private var artworkSide: CGFloat {
        let right = showsCalendar ? size.width - Self.sidePadding - Self.calendarWidth - Self.calendarGap * 2 : size.width - Self.sidePadding
        return min(bodyHeight, max(0, right - Self.sidePadding - Self.columnGap - 190))
    }
    private var columnX: CGFloat { Self.sidePadding + artworkSide + Self.columnGap }
    /// The music column ends before the calendar when there is one.
    private var columnWidth: CGFloat {
        (showsCalendar ? calendarPanel.minX - Self.calendarGap * 2 : size.width - Self.sidePadding) - columnX
    }

    // MARK: Calendar

    static let calendarWidth: CGFloat = 172
    /// Each side of the divider between music and calendar.
    static let calendarGap: CGFloat = 14
    static let calendarRowHeight: CGFloat = 18

    var calendarPanel: CGRect {
        CGRect(x: size.width - Self.sidePadding - Self.calendarWidth, y: bodyTop - 2, width: Self.calendarWidth, height: size.height - bodyTop + 2 - Self.bottomPadding)
    }
    var calendarDivider: CGRect { CGRect(x: calendarPanel.minX - Self.calendarGap, y: bodyTop, width: 1, height: calendarPanel.height - 2) }

    /// A weekday letter over its date, one of seven across the panel.
    /// The chevrons either side of the days.
    static let calendarArrow: CGFloat = 16
    static let calendarTileGap: CGFloat = 6
    /// A day: the chosen day's line, its weekday, its date, then markers for events and deadlines.
    static let calendarTileHeight: CGFloat = 44
    /// One day's column, set in type with no box.
    func calendarDay(_ index: Int) -> CGRect {
        let count = CGFloat(CalendarModel.dayCount)
        let inner = Self.calendarWidth - (Self.calendarArrow + Self.calendarTileGap) * 2
        let width = (inner - Self.calendarTileGap * (count - 1)) / count
        let x = calendarPanel.minX + Self.calendarArrow + Self.calendarTileGap + CGFloat(index) * (width + Self.calendarTileGap)
        return CGRect(x: x, y: calendarPanel.minY, width: width, height: Self.calendarTileHeight)
    }
    func calendarDayIndex(at point: CGPoint) -> Int? {
        (0..<CalendarModel.dayCount).first { calendarDay($0).contains(point) }
    }
    /// Level with the dates.
    var calendarPrevious: CGRect {
        CGRect(x: calendarPanel.minX, y: calendarPanel.minY + 18, width: Self.calendarArrow, height: 20)
    }
    var calendarNext: CGRect { calendarPrevious.offsetBy(dx: Self.calendarWidth - Self.calendarArrow, dy: 0) }
    var calendarHeading: CGRect { CGRect(x: calendarPanel.minX, y: calendarPanel.minY + Self.calendarTileHeight + 5, width: Self.calendarWidth, height: 14) }
    func calendarRow(_ index: Int) -> CGRect {
        CGRect(x: calendarPanel.minX, y: calendarHeading.maxY + 3 + CGFloat(index) * Self.calendarRowHeight, width: Self.calendarWidth, height: Self.calendarRowHeight)
    }
    /// "+2 more", right-aligned on the heading's line.
    var calendarMore: CGRect { calendarHeading }
    var calendarVisibleRows: Int {
        max(1, Int((size.height - Self.bottomPadding - calendarRow(0).minY) / Self.calendarRowHeight))
    }
    var calendarMessage: CGRect { calendarRow(0) }

    var artwork: CGRect { CGRect(x: Self.sidePadding, y: bodyTop, width: artworkSide, height: artworkSide) }

    /// The artwork's glow: an ellipse around the artwork that has faded out by the top edge, where the
    /// pill meets the black flares, and well before the right side.
    var glowCenter: CGPoint { CGPoint(x: artwork.midX, y: artwork.midY) }
    var glowRadii: CGSize { CGSize(width: size.width * 0.62, height: artwork.midY + 4) }
    /// Title and artist end before the lyrics button.
    private var headingWidth: CGFloat { columnWidth - Self.lyricsButton - Self.lyricsButtonGap }
    var title: CGRect { CGRect(x: columnX, y: bodyTop - 2, width: headingWidth, height: 20) }
    var artist: CGRect { CGRect(x: columnX, y: bodyTop + 17, width: headingWidth, height: 17) }

    /// At the end of the title row; on the stage it glides to the page's top-right corner, which is
    /// the same place unless the calendar is there.
    func lyricsButton(onStage: Bool) -> CGRect {
        let right = onStage ? size.width - Self.sidePadding : columnX + columnWidth
        return CGRect(x: right - Self.lyricsButton, y: bodyTop - 2, width: Self.lyricsButton, height: Self.lyricsButton)
    }

    /// Where the line being sung sits on the stage: a little above the middle, so two lines fit below.
    var stageCenterY: CGFloat { bodyTop + bodyHeight * 0.42 }

    /// A stage slot's line, full size for the one being sung and scaled for the rest.
    func stageLine(_ slot: Int) -> CGRect {
        let offset: CGFloat = slot == 0 ? 0 : CGFloat(slot.signum()) * (27 + CGFloat(abs(slot) - 1) * 23) + (slot > 0 ? 10 : 0)
        let height = Self.stageLineHeight * (slot == 0 ? 1 : Self.stageScale)
        let x = Self.sidePadding + Self.stageMargin
        return CGRect(x: x, y: stageCenterY + offset - height / 2, width: size.width - x * 2, height: height)
    }

    func stageSlot(at point: CGPoint) -> Int? {
        Self.stageSlots.first { stageLine($0).insetBy(dx: 0, dy: -2).contains(point) }
    }

    /// The bright line. The dim one is `lyricStep` below it.
    var lyric: CGRect { CGRect(x: columnX, y: bodyTop + 38, width: columnWidth, height: 19) }
    var lyricVisibleRows: Int {
        max(2, Int((sliderY - 4 - lyric.minY) / Self.lyricStep))
    }
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
        case .lyrics: lyricsButton(onStage: false)
        }
    }

    /// On the stage only the lyrics button answers; the lines are found with `stageSlot`.
    func control(at point: CGPoint, onStage: Bool = false) -> HomeControl? {
        if onStage { return lyricsButton(onStage: true).contains(point) ? .lyrics : nil }
        return HomeControl.allCases.first { frame(of: $0).contains(point) }
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
