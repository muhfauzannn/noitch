import CoreGraphics

/// Where everything on the usage page sits, from the pill's top-left corner with y growing downward:
/// a column for Claude Code and one for Codex. The Windows app uses the same numbers.
struct UsageLayout: Equatable {
    static let sidePadding: CGFloat = 22
    static let columnGap: CGFloat = 24
    static let headerGap: CGFloat = 8
    static let rowHeight: CGFloat = 14
    static let trackHeight: CGFloat = 4

    let size: CGSize
    let headerHeight: CGFloat

    private var bodyTop: CGFloat { headerHeight + Self.headerGap }
    var columnWidth: CGFloat { (size.width - Self.sidePadding * 2 - Self.columnGap) / 2 }
    private func x(_ column: Int) -> CGFloat { Self.sidePadding + CGFloat(column) * (columnWidth + Self.columnGap) }

    func title(_ column: Int) -> CGRect { CGRect(x: x(column), y: bodyTop, width: columnWidth, height: 16) }
    func tokens(_ column: Int) -> CGRect { CGRect(x: x(column), y: bodyTop + 16, width: columnWidth * 0.6, height: 26) }
    func cost(_ column: Int) -> CGRect {
        CGRect(x: x(column) + columnWidth * 0.6, y: bodyTop + 16, width: columnWidth * 0.4, height: 26)
    }
    func limit(_ column: Int) -> CGRect { CGRect(x: x(column), y: bodyTop + 46, width: columnWidth, height: 14) }
    func limitTrack(_ column: Int) -> CGRect {
        CGRect(x: x(column), y: bodyTop + 63, width: columnWidth, height: Self.trackHeight)
    }
    func row(_ column: Int, _ index: Int) -> CGRect {
        CGRect(x: x(column), y: bodyTop + 74 + CGFloat(index) * Self.rowHeight, width: columnWidth, height: Self.rowHeight)
    }
}
