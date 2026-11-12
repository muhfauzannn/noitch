import AppKit
import QuartzCore

/// The closed island's task alert: the slip (an icon in the left wing, a count in the right and a
/// line under the notch) and the dot that stays after it. It decides nothing.
final class TaskAlertScene {
    /// Hung from the top centre of the pill by the view, like the music wings.
    let root = CALayer()

    private let layout: TaskAlertLayout
    private let slip = CALayer()
    private let wings = CALayer()
    private let glyphPlate = CALayer()
    private let glyph = CAShapeLayer()
    private let count = CATextLayer()
    private let strip = CATextLayer()
    private let dot = CALayer()

    init(layout: TaskAlertLayout, scale: CGFloat) {
        self.layout = layout
        root.bounds = CGRect(origin: .zero, size: layout.size)
        root.isGeometryFlipped = true

        for group in [slip, wings] {
            group.frame = root.bounds
            group.opacity = 0
        }
        root.addSublayer(slip)
        slip.addSublayer(wings)
        glyphPlate.frame = layout.glyph
        glyphPlate.cornerRadius = 5
        glyphPlate.cornerCurve = .continuous
        wings.addSublayer(glyphPlate)
        let inset = layout.glyph.width * 0.2
        glyph.frame = glyphPlate.bounds.insetBy(dx: inset, dy: inset)
        glyph.fillColor = NSColor.black.cgColor
        glyph.fillRule = .evenOdd
        glyph.contentsScale = scale
        glyphPlate.addSublayer(glyph)
        count.frame = layout.count
        count.font = NSFont.monospacedDigitSystemFont(ofSize: 12, weight: .semibold)
        count.fontSize = 12
        count.alignmentMode = .center
        count.contentsScale = scale
        wings.addSublayer(count)
        strip.frame = layout.strip
        strip.alignmentMode = .center
        strip.truncationMode = .end
        strip.contentsScale = scale
        slip.addSublayer(strip)

        dot.frame = layout.dot(beside: false)
        dot.cornerRadius = TaskAlertLayout.dotSide / 2
        dot.backgroundColor = TasksScene.fresh.cgColor
        dot.opacity = 0
        root.addSublayer(dot)
    }

    struct Slip: Equatable {
        var module: TaskModule
        var count: String
        var lead: String
        var text: String
        var detail: String
        var urgent: Bool
    }

    /// `wingsShown` is false while the music's wings hold the artwork and bars instead.
    func showSlip(_ content: Slip, wingsShown: Bool) {
        let now = CACurrentMediaTime()
        let move = !Motion.reduceMotion
        let accent = content.urgent ? TasksScene.critical : TasksScene.fresh
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        glyphPlate.backgroundColor = (content.urgent ? TasksScene.critical : TasksScene.accent).cgColor
        glyph.path = TasksScene.icon(content.module).path(side: glyph.bounds.width)
        count.string = content.count
        count.foregroundColor = accent.cgColor
        let text = NSMutableAttributedString(string: content.lead + "  ", attributes: [
            .font: NSFont.systemFont(ofSize: LiveLayout.lyricFontSize, weight: .semibold), .foregroundColor: accent,
        ])
        // The detail (when, or which course) goes before the title, so cutting a long title short
        // never cuts it.
        if !content.detail.isEmpty {
            text.append(NSAttributedString(string: content.detail + "  ", attributes: [
                .font: NSFont.systemFont(ofSize: LiveLayout.lyricFontSize, weight: .medium), .foregroundColor: NSColor.white.withAlphaComponent(0.55),
            ]))
        }
        text.append(NSAttributedString(string: content.text, attributes: [
            .font: NSFont.systemFont(ofSize: LiveLayout.lyricFontSize, weight: .semibold), .foregroundColor: NSColor.white,
        ]))
        strip.string = TasksScene.fit(text, width: strip.bounds.width - 2)
        fade(wings, to: wingsShown ? 1 : 0, duration: 0.3, begin: now + 0.12)
        let visible = slip.presentation()?.opacity ?? slip.opacity
        fade(slip, to: 1, duration: 0.3, begin: now + (visible > 0.5 ? 0 : 0.12))
        if move {
            // Waits for the pill to grow down to it, then rises the last few points into place.
            Motion.ease(strip, "transform.translation.y", from: -8, to: 0, duration: 0.42, begin: now + 0.1)
        }
        fade(dot, to: 0, duration: 0.12, begin: now)
        CATransaction.commit()
    }

    /// After the slip, or on its own: just the dot.
    func showDot(_ visible: Bool, beside live: Bool) {
        let now = CACurrentMediaTime()
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        fade(slip, to: 0, duration: 0.15, begin: now)
        dot.frame = layout.dot(beside: live)
        // Appears once the pill has narrowed back to its new width.
        fade(dot, to: visible ? 1 : 0, duration: visible ? 0.3 : 0.12, begin: now + (visible ? 0.25 : 0))
        CATransaction.commit()
    }

    /// Hidden while the island is open, back after it closes.
    func setVisible(_ visible: Bool, delay: CFTimeInterval = 0) {
        let now = CACurrentMediaTime()
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        fade(root, to: visible ? 1 : 0, duration: visible ? 0.3 : 0.12, begin: now + (visible ? delay : 0))
        CATransaction.commit()
    }

    private func fade(_ layer: CALayer, to target: Float, duration: CFTimeInterval, begin: CFTimeInterval) {
        let from = layer.presentation()?.opacity ?? layer.opacity
        guard from != target || layer.opacity != target else { return }
        Motion.ease(layer, "opacity", from: from, to: target, duration: duration, begin: begin)
        layer.opacity = target
    }
}
