import CoreGraphics

/// Home's icons as paths on a 24 × 24 grid with y growing downward. SF Symbols would load a symbol
/// catalog that keeps about 2 MB after Home is released, and the footprint budget can't spare that.
enum HomeIcon {
    case play
    case pause
    case previous
    case next
    case shuffle
    case repeatAll
    case repeatOne
    case settings
    case music
    case usage
    case lyrics

    static let all: [HomeIcon] = [.play, .pause, .previous, .next, .shuffle, .repeatAll, .repeatOne, .settings, .music, .usage, .lyrics]

    /// Only the gear and the lyrics bubble rely on the even-odd rule. Others have overlapping parts
    /// that it would punch out.
    var hasHole: Bool {
        switch self {
        case .settings, .lyrics: true
        default: false
        }
    }

    /// Outlines are drawn with a stroke, and everything else is filled.
    var isOutline: Bool {
        switch self {
        case .shuffle, .repeatAll, .repeatOne: true
        default: false
        }
    }

    func path(side: CGFloat) -> CGPath {
        let path = CGMutablePath()
        draw(into: path)
        var scale = CGAffineTransform(scaleX: side / 24, y: side / 24)
        return path.copy(using: &scale) ?? path
    }

    private func draw(into path: CGMutablePath) {
        switch self {
        case .play:
            path.addLines(between: [CGPoint(x: 7, y: 4), CGPoint(x: 20, y: 12), CGPoint(x: 7, y: 20)])
            path.closeSubpath()
        case .pause:
            path.addRoundedRect(in: CGRect(x: 6, y: 4, width: 4.5, height: 16), cornerWidth: 1, cornerHeight: 1)
            path.addRoundedRect(in: CGRect(x: 13.5, y: 4, width: 4.5, height: 16), cornerWidth: 1, cornerHeight: 1)
        case .next:
            Self.addSkip(to: path, tips: [12, 21], direction: 1)
        case .previous:
            Self.addSkip(to: path, tips: [12, 3], direction: -1)
        case .shuffle:
            path.move(to: CGPoint(x: 3, y: 7))
            path.addCurve(to: CGPoint(x: 20, y: 17), control1: CGPoint(x: 12, y: 7), control2: CGPoint(x: 11, y: 17))
            path.move(to: CGPoint(x: 3, y: 17))
            path.addCurve(to: CGPoint(x: 20, y: 7), control1: CGPoint(x: 12, y: 17), control2: CGPoint(x: 11, y: 7))
            Self.addArrowhead(to: path, tip: CGPoint(x: 21, y: 7), direction: 1)
            Self.addArrowhead(to: path, tip: CGPoint(x: 21, y: 17), direction: 1)
        case .repeatAll:
            Self.addRepeatLoop(to: path)
        case .repeatOne:
            Self.addRepeatLoop(to: path)
            path.move(to: CGPoint(x: 11, y: 11))
            path.addLine(to: CGPoint(x: 12, y: 10.5))
            path.addLine(to: CGPoint(x: 12, y: 14))
        case .settings:
            Self.addGear(to: path)
        case .usage:
            // Three bars, a usage chart.
            path.addRoundedRect(in: CGRect(x: 4, y: 12, width: 4, height: 8), cornerWidth: 1, cornerHeight: 1)
            path.addRoundedRect(in: CGRect(x: 10, y: 5, width: 4, height: 15), cornerWidth: 1, cornerHeight: 1)
            path.addRoundedRect(in: CGRect(x: 16, y: 9, width: 4, height: 11), cornerWidth: 1, cornerHeight: 1)
        case .lyrics:
            Self.addBubble(to: path)
        case .music:
            path.addEllipse(in: CGRect(x: 4, y: 15, width: 8, height: 6.5))
            path.addRect(CGRect(x: 10, y: 4, width: 2, height: 14))
            path.addLines(between: [CGPoint(x: 10, y: 3), CGPoint(x: 20, y: 5), CGPoint(x: 20, y: 9), CGPoint(x: 10, y: 7)])
            path.closeSubpath()
        }
    }

    /// A speech bubble with two lines of text cut out of it. One outline, so the tail adds no seam.
    private static func addBubble(to path: CGMutablePath) {
        path.move(to: CGPoint(x: 7, y: 4))
        path.addArc(tangent1End: CGPoint(x: 21, y: 4), tangent2End: CGPoint(x: 21, y: 17), radius: 4)
        path.addArc(tangent1End: CGPoint(x: 21, y: 17), tangent2End: CGPoint(x: 3, y: 17), radius: 4)
        path.addLine(to: CGPoint(x: 11.5, y: 17))
        path.addLine(to: CGPoint(x: 6, y: 21.5))
        path.addLine(to: CGPoint(x: 7, y: 17))
        path.addArc(tangent1End: CGPoint(x: 3, y: 17), tangent2End: CGPoint(x: 3, y: 4), radius: 4)
        path.addArc(tangent1End: CGPoint(x: 3, y: 4), tangent2End: CGPoint(x: 21, y: 4), radius: 4)
        path.closeSubpath()
        path.addRoundedRect(in: CGRect(x: 7, y: 8, width: 10, height: 2.2), cornerWidth: 1.1, cornerHeight: 1.1)
        path.addRoundedRect(in: CGRect(x: 7, y: 11.8, width: 6.5, height: 2.2), cornerWidth: 1.1, cornerHeight: 1.1)
    }

    private static func addSkip(to path: CGMutablePath, tips: [CGFloat], direction: CGFloat) {
        for tip in tips {
            let base = tip - 9 * direction
            path.addLines(between: [CGPoint(x: base, y: 5), CGPoint(x: tip, y: 12), CGPoint(x: base, y: 19)])
            path.closeSubpath()
        }
    }

    private static func addArrowhead(to path: CGMutablePath, tip: CGPoint, direction: CGFloat) {
        path.move(to: CGPoint(x: tip.x - 3 * direction, y: tip.y - 3))
        path.addLine(to: tip)
        path.addLine(to: CGPoint(x: tip.x - 3 * direction, y: tip.y + 3))
    }

    private static func addRepeatLoop(to path: CGMutablePath) {
        path.move(to: CGPoint(x: 4, y: 11))
        path.addArc(tangent1End: CGPoint(x: 4, y: 7), tangent2End: CGPoint(x: 8, y: 7), radius: 3)
        path.addLine(to: CGPoint(x: 20, y: 7))
        addArrowhead(to: path, tip: CGPoint(x: 20, y: 7), direction: 1)
        path.move(to: CGPoint(x: 20, y: 13))
        path.addArc(tangent1End: CGPoint(x: 20, y: 17), tangent2End: CGPoint(x: 16, y: 17), radius: 3)
        path.addLine(to: CGPoint(x: 4, y: 17))
        addArrowhead(to: path, tip: CGPoint(x: 4, y: 17), direction: -1)
    }

    /// Eight teeth around a ring. The hole relies on the even-odd fill rule.
    private static func addGear(to path: CGMutablePath) {
        let center = CGPoint(x: 12, y: 12)
        let teeth = 8
        var points: [CGPoint] = []
        for tooth in 0..<teeth {
            let middle = CGFloat(tooth) / CGFloat(teeth) * 2 * .pi
            for (offset, radius) in [(-0.27, 7.2), (-0.17, 10.0), (0.17, 10.0), (0.27, 7.2)] as [(CGFloat, CGFloat)] {
                let angle = middle + offset
                points.append(CGPoint(x: center.x + cos(angle) * radius, y: center.y + sin(angle) * radius))
            }
        }
        path.addLines(between: points)
        path.closeSubpath()
        path.addEllipse(in: CGRect(x: 8.5, y: 8.5, width: 7, height: 7))
    }
}
