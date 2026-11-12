import CoreGraphics

/// Where everything on the tasks page sits, from the pill's top-left corner with y growing downward.
/// The next task as a countdown on the left; on the right the next seven days as bars of how much is
/// due, over a short list. The Windows app uses the same numbers (tasks_layout.h).
struct TasksLayout: Equatable {
    static let sidePadding: CGFloat = 22
    static let headerGap: CGFloat = 6
    static let heroWidth: CGFloat = 178
    static let columnGap: CGFloat = 22
    static let loadHeight: CGFloat = 36
    static let rowHeight: CGFloat = 24
    static let rowGap: CGFloat = 1
    static let visibleRows = TaskRule.visibleRows

    let size: CGSize
    let headerHeight: CGFloat

    var bodyTop: CGFloat { headerHeight + Self.headerGap }

    // MARK: Hero

    var heroLabel: CGRect { CGRect(x: Self.sidePadding, y: bodyTop, width: Self.heroWidth, height: 12) }
    var countdown: CGRect { CGRect(x: Self.sidePadding, y: bodyTop + 13, width: Self.heroWidth, height: 44) }
    var heroTitle: CGRect { CGRect(x: Self.sidePadding, y: bodyTop + 58, width: Self.heroWidth, height: 17) }
    var heroCourse: CGRect { CGRect(x: Self.sidePadding, y: bodyTop + 77, width: 44, height: 14) }
    var heroDue: CGRect { CGRect(x: Self.sidePadding + 50, y: bodyTop + 77, width: Self.heroWidth - 50, height: 14) }
    var openButton: CGRect { CGRect(x: Self.sidePadding, y: bodyTop + 97, width: 104, height: 20) }

    // MARK: Runway and list

    var columnX: CGFloat { Self.sidePadding + Self.heroWidth + Self.columnGap }
    var columnWidth: CGFloat { size.width - columnX - Self.sidePadding }

    // MARK: The week's load

    static let barWidth: CGFloat = 14
    static let barGap: CGFloat = 10
    /// The tallest a bar grows; an empty day keeps a short stub.
    static let barMax: CGFloat = 21
    static let barStub: CGFloat = 3

    /// The bars stand on this line.
    var barBase: CGFloat { bodyTop + 23 }
    func bar(_ day: Int, share: CGFloat) -> CGRect {
        let height = Self.barStub + (Self.barMax - Self.barStub) * min(max(share, 0), 1)
        return CGRect(x: columnX + CGFloat(day) * (Self.barWidth + Self.barGap), y: barBase - height, width: Self.barWidth, height: height)
    }
    func barLetter(_ day: Int) -> CGRect {
        CGRect(x: columnX + CGFloat(day) * (Self.barWidth + Self.barGap) - 4, y: barBase + 2, width: Self.barWidth + 8, height: 11)
    }
    var loadCaption: CGRect {
        let x = columnX + CGFloat(TaskRule.loadDays) * (Self.barWidth + Self.barGap) + 8
        return CGRect(x: x, y: bodyTop + 3, width: columnX + columnWidth - x, height: 14)
    }
    var loadDetail: CGRect { loadCaption.offsetBy(dx: 0, dy: 15) }
    var load: CGRect { CGRect(x: columnX, y: bodyTop, width: columnWidth, height: Self.loadHeight) }

    func row(_ index: Int) -> CGRect {
        CGRect(x: columnX, y: bodyTop + Self.loadHeight + 4 + CGFloat(index) * (Self.rowHeight + Self.rowGap), width: columnWidth, height: Self.rowHeight)
    }

    func rowIcon(_ index: Int) -> CGRect {
        let row = row(index)
        return CGRect(x: row.minX + 6, y: row.midY - 8, width: 16, height: 16)
    }

    func rowTitle(_ index: Int) -> CGRect {
        let row = row(index)
        return CGRect(x: row.minX + 30, y: row.midY - 7.5, width: row.width - 30 - 104, height: 15)
    }

    func rowDue(_ index: Int) -> CGRect {
        let row = row(index)
        return CGRect(x: row.maxX - 104, y: row.midY - 7, width: 98, height: 14)
    }

    var more: CGRect {
        let last = row(Self.visibleRows - 1)
        return CGRect(x: columnX + 6, y: last.maxY + 2, width: columnWidth - 12 - Self.overdueWidth, height: 12)
    }

    static let overdueWidth: CGFloat = 104
    /// "Show 2 overdue", at the right end of the line under the list.
    var overdueToggle: CGRect {
        let last = row(Self.visibleRows - 1)
        return CGRect(x: columnX + columnWidth - 6 - Self.overdueWidth, y: last.maxY + 2, width: Self.overdueWidth, height: 12)
    }

    /// The signed-out, empty and failed states use the whole body, centred. Under the all-clear tick
    /// everything moves down, so the tick stays clear of the header band and the notch.
    func message(withCheck: Bool) -> CGRect {
        CGRect(x: Self.sidePadding, y: bodyTop + 22 + messageShift(withCheck), width: size.width - Self.sidePadding * 2, height: 20)
    }
    func messageDetail(withCheck: Bool) -> CGRect {
        CGRect(x: Self.sidePadding, y: bodyTop + 44 + messageShift(withCheck), width: size.width - Self.sidePadding * 2, height: 15)
    }
    func messageButton(withCheck: Bool) -> CGRect {
        CGRect(x: (size.width - 116) / 2, y: bodyTop + 66 + messageShift(withCheck), width: 116, height: 22)
    }
    var check: CGRect { CGRect(x: (size.width - 30) / 2, y: bodyTop + 2, width: 30, height: 30) }
    private func messageShift(_ withCheck: Bool) -> CGFloat { withCheck ? 16 : 0 }
}

/// What the closed island shows of the tasks.
enum TaskAlertShape: Equatable {
    case none
    /// The slip: wings and a strip under the notch, for a few seconds.
    case slip
    /// A dot in a narrow right wing, until the tasks page has been seen.
    case dot
}

/// The closed island's alert, laid out over the same wings and strip as music (`LiveLayout`), with
/// a narrow wing more on each side for the dot. From the top-left of the alert's own layer, which is
/// centred on the pill. The Windows app uses the same numbers (tasks_layout.h).
struct TaskAlertLayout: Equatable {
    /// The extra wing on each side that holds the dot.
    static let dotWing: CGFloat = 14
    static let dotSide: CGFloat = 6
    static let slipDuration: Double = 8

    let collapsed: CGSize

    var live: LiveLayout { LiveLayout(collapsed: collapsed) }
    var size: CGSize { CGSize(width: live.size.width + Self.dotWing * 2, height: collapsed.height + LiveLayout.lyricStrip) }

    var glyph: CGRect { live.artwork.offsetBy(dx: Self.dotWing, dy: 0) }
    var count: CGRect {
        let start = Self.dotWing + live.wing + collapsed.width + LiveLayout.innerGap
        let width = live.wing - LiveLayout.innerGap - LiveLayout.outerPadding
        return CGRect(x: start - 4, y: (collapsed.height - 15) / 2, width: width + 8, height: 15)
    }
    var strip: CGRect { live.lyric.offsetBy(dx: Self.dotWing, dy: 0) }

    /// In the outermost part of the pill's right side, whether or not the music wings are there.
    func dot(beside live: Bool) -> CGRect {
        let inner = live ? self.live.size.width : collapsed.width
        let x = size.width / 2 + inner / 2 + Self.dotWing / 2 - Self.dotSide / 2 - 1
        return CGRect(x: x, y: (collapsed.height - Self.dotSide) / 2, width: Self.dotSide, height: Self.dotSide)
    }
}
