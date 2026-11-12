import AppKit
import QuartzCore

/// The usage page's layers: per tool, a ring gauge (five hours inside, the week outside, a dot on
/// each marking how much of its time has passed) and beside it the numbers and the one thing worth
/// knowing. It draws what it is given and decides nothing. Ring paths are set once; only their
/// stroke ends move, in the render server.
final class UsageScene {
    /// Placed by the pager, which owns its fade and slide.
    let root = CALayer()

    private struct Ring {
        let track = CAShapeLayer()
        let fill = CAShapeLayer()
        let tick = CALayer()
        let radius: CGFloat
    }

    private struct Column {
        let group = CALayer()
        let logo = CAShapeLayer()
        let outer: Ring
        let inner: Ring
        let center = UsageScene.makeText(size: 20, weight: .semibold, opacity: 1, alignment: .center)
        let caption = UsageScene.makeText(size: 10, weight: .medium, opacity: 0.5, alignment: .center)
        let name = UsageScene.makeText(size: 13, weight: .semibold, opacity: 1)
        let today = UsageScene.makeText(size: 11, weight: .regular, opacity: 0.45)
        let labels = (0..<2).map { _ in UsageScene.makeText(size: 11, weight: .regular, opacity: 0.55) }
        let values = (0..<2).map { _ in UsageScene.makeText(size: 11, weight: .semibold, opacity: 1) }
        let paces = (0..<2).map { _ in UsageScene.makeText(size: 9, weight: .bold, opacity: 1, alignment: .center) }
        let resets = (0..<2).map { _ in UsageScene.makeText(size: 11, weight: .regular, opacity: 0.55, alignment: .right) }
        let headline = UsageScene.makeText(size: 12, weight: .semibold, opacity: 1)
        let detail = UsageScene.makeText(size: 11, weight: .regular, opacity: 0.55)
        let accent: NSColor
    }

    private let layout: UsageLayout
    private let tools: [UsageTool]
    private let columns: [Column]
    private var shown: UsageModel?

    /// One column per entry of `tools`, in that order; `layout` must have as many columns.
    init(layout: UsageLayout, tools: [UsageTool], scale: CGFloat) {
        self.layout = layout
        self.tools = tools
        root.bounds = CGRect(origin: .zero, size: layout.size)
        columns = tools.map { tool in
            Column(outer: Ring(radius: UsageLayout.outerRadius), inner: Ring(radius: UsageLayout.innerRadius), accent: Self.accent(of: tool))
        }
        for (index, column) in columns.enumerated() {
            column.group.frame = root.bounds
            root.addSublayer(column.group)
            let gauge = layout.gauge(index)
            for ring in [column.outer, column.inner] { Self.build(ring, in: gauge, group: column.group, scale: scale) }
            column.logo.frame = layout.logo(index)
            column.logo.path = Self.brand(of: tools[index]).path(side: UsageLayout.logo)
            column.logo.fillColor = (tools[index] == .claude ? column.accent : .white).cgColor
            column.logo.contentsScale = scale
            column.group.addSublayer(column.logo)
            var texts: [(CATextLayer, CGRect)] = [
                (column.center, layout.center(index)), (column.caption, layout.caption(index)),
                (column.name, layout.name(index)), (column.today, layout.today(index)),
                (column.headline, layout.headline(index)), (column.detail, layout.detail(index)),
            ]
            for row in 0..<2 {
                texts += [(column.labels[row], layout.rowLabel(index, row)), (column.values[row], layout.rowValue(index, row)),
                          (column.paces[row], layout.rowPace(index, row)), (column.resets[row], layout.rowReset(index, row))]
            }
            for (layer, frame) in texts {
                layer.frame = frame
                layer.contentsScale = scale
                column.group.addSublayer(layer)
            }
        }
    }

    func apply(_ model: UsageModel) {
        guard model != shown else { return }
        let previous = shown
        shown = model
        let now = CACurrentMediaTime()
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        for (column, kind) in zip(columns, tools) {
            let tool = model.tool(kind)
            let old = previous?.tool(kind)
            column.center.string = tool.center
            column.caption.string = tool.caption
            column.name.string = tool.name
            column.today.string = tool.today
            column.headline.string = tool.headline
            column.headline.foregroundColor = color(tool.severity, accent: nil).cgColor
            column.detail.string = tool.detail
            for (row, data) in tool.rows.enumerated() {
                column.labels[row].string = data.label
                column.values[row].string = data.value
                column.values[row].foregroundColor = (data.severity == .calm ? NSColor.white : color(data.severity, accent: nil)).cgColor
                column.paces[row].string = Self.symbol(data.pace)
                column.paces[row].foregroundColor = Self.paceColor(data.pace).cgColor
                column.resets[row].string = data.reset
            }
            set(column.inner, tool.inner, previous: old?.inner, accent: column.accent, now: now)
            // The week is the same hue, lighter, so the two rings read as one tool's two windows.
            set(column.outer, tool.outer, previous: old?.outer, accent: column.accent.blended(withFraction: 0.45, of: .white) ?? column.accent, now: now)
        }
        CATransaction.commit()
    }

    /// Staggers the columns in from `begin`, and sweeps the rings up from empty with them.
    func reveal(begin: CFTimeInterval) {
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        Motion.reveal(columns.map(\.group), begin: begin)
        for (index, column) in columns.enumerated() {
            for ring in [column.inner, column.outer] where !ring.fill.isHidden {
                Motion.ease(ring.fill, "strokeEnd", from: 0, to: ring.fill.strokeEnd, duration: 0.9,
                            begin: begin + Double(index) * Motion.revealStagger + 0.05)
            }
        }
        CATransaction.commit()
    }

    private func set(_ ring: Ring, _ data: UsageModel.Ring?, previous: UsageModel.Ring?, accent: NSColor, now: CFTimeInterval) {
        ring.fill.isHidden = data == nil
        ring.tick.isHidden = data?.tick == nil
        guard let data else { return }
        ring.fill.strokeColor = color(data.severity, accent: accent).cgColor
        let end = CGFloat(data.fraction)
        if let previous, previous.fraction != data.fraction {
            let from = ring.fill.presentation()?.strokeEnd ?? CGFloat(previous.fraction)
            Motion.ease(ring.fill, "strokeEnd", from: from, to: end, duration: 0.6, begin: now)
        }
        ring.fill.strokeEnd = end
        if let tick = data.tick {
            // Zero is the top; the arcs run clockwise from there.
            let angle = tick * 2 * .pi
            let center = CGPoint(x: ring.track.bounds.midX, y: ring.track.bounds.midY)
            ring.tick.position = CGPoint(x: center.x + ring.radius * sin(angle) + ring.track.frame.minX,
                                         y: center.y - ring.radius * cos(angle) + ring.track.frame.minY)
        }
    }

    private static func brand(of tool: UsageTool) -> BrandLogo {
        switch tool {
        case .claude: .claude
        case .codex: .openAI
        }
    }

    /// Claude's coral and a calm blue for Codex, so the two read apart at a glance.
    private static func accent(of tool: UsageTool) -> NSColor {
        switch tool {
        case .claude: NSColor(srgbRed: 0.85, green: 0.47, blue: 0.34, alpha: 1)
        case .codex: NSColor(srgbRed: 0.48, green: 0.64, blue: 1, alpha: 1)
        }
    }

    private func color(_ severity: Severity, accent: NSColor?) -> NSColor {
        switch severity {
        case .calm: accent ?? NSColor.white
        case .watch: NSColor(srgbRed: 1, green: 0.8, blue: 0.2, alpha: 1)
        case .critical: NSColor(srgbRed: 1, green: 0.33, blue: 0.3, alpha: 1)
        }
    }

    private static func symbol(_ pace: Pace?) -> String {
        switch pace {
        case .ahead: "▲"
        case .under: "▼"
        case .even: "●"
        case nil: ""
        }
    }

    /// Ahead of the clock is the warning; behind it is room to spare.
    private static func paceColor(_ pace: Pace?) -> NSColor {
        switch pace {
        case .ahead: NSColor(srgbRed: 1, green: 0.8, blue: 0.2, alpha: 1)
        case .under: NSColor(srgbRed: 0.45, green: 0.85, blue: 0.6, alpha: 1)
        default: NSColor.white.withAlphaComponent(0.4)
        }
    }

    private static func build(_ ring: Ring, in gauge: CGRect, group: CALayer, scale: CGFloat) {
        let circle = CGMutablePath()
        let center = CGPoint(x: gauge.width / 2, y: gauge.height / 2)
        // Starts at the top. The page's geometry is y-down, so increasing angles run clockwise.
        circle.addArc(center: center, radius: ring.radius, startAngle: -.pi / 2, endAngle: 1.5 * .pi, clockwise: false)
        for (layer, color) in [(ring.track, NSColor.white.withAlphaComponent(0.1)), (ring.fill, NSColor.white)] {
            layer.frame = gauge
            layer.path = circle
            layer.fillColor = nil
            layer.strokeColor = color.cgColor
            layer.lineWidth = UsageLayout.ringWidth
            layer.lineCap = .round
            layer.contentsScale = scale
            group.addSublayer(layer)
        }
        ring.fill.strokeEnd = 0
        let dot: CGFloat = 3
        ring.tick.bounds = CGRect(x: 0, y: 0, width: dot, height: dot)
        ring.tick.cornerRadius = dot / 2
        ring.tick.backgroundColor = NSColor.white.cgColor
        group.addSublayer(ring.tick)
    }

    private static func makeText(size: CGFloat, weight: NSFont.Weight, opacity: Float, alignment: CATextLayerAlignmentMode = .left) -> CATextLayer {
        let layer = CATextLayer()
        let font = size >= 20 ? NSFont.monospacedDigitSystemFont(ofSize: size, weight: weight) : NSFont.systemFont(ofSize: size, weight: weight)
        layer.font = font
        layer.fontSize = size
        layer.foregroundColor = NSColor.white.cgColor
        layer.opacity = opacity
        layer.truncationMode = .end
        layer.alignmentMode = alignment
        return layer
    }
}
