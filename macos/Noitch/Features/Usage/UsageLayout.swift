import CoreGraphics

/// Where everything on the usage page sits, from the pill's top-left corner with y growing downward.
/// One column per tool: a ring gauge, and beside it the numbers and what they mean. The Windows app
/// uses the same numbers (usage_layout.h).
struct UsageLayout: Equatable {
    static let sidePadding: CGFloat = 22
    static let columnGap: CGFloat = 24
    static let headerGap: CGFloat = 8
    static let bottomPadding: CGFloat = 12
    static let gauge: CGFloat = 88
    static let gaugeGap: CGFloat = 14
    static let ringWidth: CGFloat = 7
    /// Radii to the middle of each stroke: the week outside, five hours inside.
    static let outerRadius: CGFloat = (gauge - ringWidth) / 2
    static let innerRadius: CGFloat = outerRadius - ringWidth - 3

    let size: CGSize
    let headerHeight: CGFloat

    private var bodyTop: CGFloat { headerHeight + Self.headerGap }
    private var bodyHeight: CGFloat { size.height - bodyTop - Self.bottomPadding }
    var columnWidth: CGFloat { (size.width - Self.sidePadding * 2 - Self.columnGap) / 2 }
    private func x(_ column: Int) -> CGFloat { Self.sidePadding + CGFloat(column) * (columnWidth + Self.columnGap) }
    private func textX(_ column: Int) -> CGFloat { x(column) + Self.gauge + Self.gaugeGap }
    var textWidth: CGFloat { columnWidth - Self.gauge - Self.gaugeGap }
    /// The text block is 102 points tall; it and the gauge share the body's vertical center.
    private var textTop: CGFloat { bodyTop + max((bodyHeight - 102) / 2, 0) }

    func gauge(_ column: Int) -> CGRect {
        CGRect(x: x(column), y: bodyTop + (bodyHeight - Self.gauge) / 2, width: Self.gauge, height: Self.gauge)
    }
    func center(_ column: Int) -> CGRect {
        let gauge = gauge(column)
        return CGRect(x: gauge.minX, y: gauge.midY - 14, width: gauge.width, height: 22)
    }
    func caption(_ column: Int) -> CGRect {
        let gauge = gauge(column)
        return CGRect(x: gauge.minX, y: gauge.midY + 7, width: gauge.width, height: 12)
    }
    func name(_ column: Int) -> CGRect { CGRect(x: textX(column), y: textTop, width: textWidth, height: 16) }
    func today(_ column: Int) -> CGRect { CGRect(x: textX(column), y: textTop + 17, width: textWidth, height: 14) }

    /// Row 0 is five hours, row 1 the week.
    func rowLabel(_ column: Int, _ row: Int) -> CGRect { CGRect(x: textX(column), y: rowY(row), width: 34, height: 15) }
    func rowValue(_ column: Int, _ row: Int) -> CGRect { CGRect(x: textX(column) + 34, y: rowY(row), width: 36, height: 15) }
    func rowPace(_ column: Int, _ row: Int) -> CGRect { CGRect(x: textX(column) + 70, y: rowY(row), width: 14, height: 15) }
    func rowReset(_ column: Int, _ row: Int) -> CGRect {
        CGRect(x: textX(column) + 84, y: rowY(row), width: textWidth - 84, height: 15)
    }
    private func rowY(_ row: Int) -> CGFloat { textTop + 36 + CGFloat(row) * 15 }

    func headline(_ column: Int) -> CGRect { CGRect(x: textX(column), y: textTop + 72, width: textWidth, height: 16) }
    func detail(_ column: Int) -> CGRect { CGRect(x: textX(column), y: textTop + 88, width: textWidth, height: 14) }
}
