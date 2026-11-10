import AppKit
import QuartzCore

/// Holds the pages and the header they share: page tabs, a selection plate that slides between
/// them, and settings. It decides nothing: the page controller says what to show. Every movement is
/// one Core Animation handed to the render server.
final class PagerScene {
    /// How far a page slides while it crossfades. Small, so the move reads as direction, not travel.
    static let pageShift: CGFloat = 24
    private static let hiddenScale: CGFloat = 0.94
    private static let idleTabOpacity: Float = 0.45

    /// Pinned to the pill's top edge by the view.
    let root = CALayer()

    private let layout: HeaderLayout
    private let header = CALayer()
    private let selection = CALayer()
    private let hoverPlate = CALayer()
    private var icons: [HeaderControl: CAShapeLayer] = [:]
    private var pages: [IslandPage: CALayer] = [:]

    init(layout: HeaderLayout, scale: CGFloat) {
        self.layout = layout
        root.bounds = CGRect(origin: .zero, size: layout.size)
        root.anchorPoint = CGPoint(x: 0.5, y: 1)
        // Keeps the shared y-down layout numbers as they are.
        root.isGeometryFlipped = true
        root.opacity = 0
        root.transform = CATransform3DMakeScale(Self.hiddenScale, Self.hiddenScale, 1)

        header.frame = root.bounds
        header.zPosition = 1
        root.addSublayer(header)
        hoverPlate.backgroundColor = NSColor.white.withAlphaComponent(0.22).cgColor
        hoverPlate.isHidden = true
        header.addSublayer(hoverPlate)
        selection.backgroundColor = NSColor.white.withAlphaComponent(0.16).cgColor
        selection.frame = layout.frame(of: .tab(.home))
        selection.cornerRadius = selection.frame.height / 2
        header.addSublayer(selection)

        let controls: [(HeaderControl, HomeIcon)] = [(.tab(.home), .music), (.tab(.usage), .usage), (.settings, .settings)]
        for (control, icon) in controls {
            let frame = layout.frame(of: control)
            let inset = frame.width * 0.22
            let layer = CAShapeLayer()
            layer.frame = frame.insetBy(dx: inset, dy: inset)
            layer.path = icon.path(side: layer.bounds.width)
            layer.fillRule = icon.hasHole ? .evenOdd : .nonZero
            layer.fillColor = NSColor.white.cgColor
            layer.contentsScale = scale
            header.addSublayer(layer)
            icons[control] = layer
        }
    }

    /// Adds a page's layers, shown at once if it is the current page and hidden otherwise.
    func attach(_ layer: CALayer, for page: IslandPage, visible: Bool) {
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        pages[page]?.removeFromSuperlayer()
        pages[page] = layer
        layer.anchorPoint = .zero
        layer.position = .zero
        layer.opacity = visible ? 1 : 0
        layer.transform = CATransform3DIdentity
        root.insertSublayer(layer, below: header)
        CATransaction.commit()
    }

    /// Marks the current tab without animating, for a scene that was just built.
    func select(_ page: IslandPage) {
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        selection.frame = layout.frame(of: .tab(page))
        for candidate in IslandPage.allCases {
            icons[.tab(candidate)]?.opacity = candidate == page ? 1 : Self.idleTabOpacity
        }
        CATransaction.commit()
    }

    /// The old page slides a little toward where it lives in tab order and fades; the new one comes
    /// from the other side. Starting from the on-screen values lets a quick second switch reverse it.
    func show(_ page: IslandPage, from old: IslandPage) {
        let now = CACurrentMediaTime()
        let direction: CGFloat = page.rawValue > old.rawValue ? 1 : -1
        let shift = Motion.reduceMotion ? 0 : Self.pageShift
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        if let outgoing = pages[old] {
            let presentation = outgoing.presentation()
            Motion.ease(outgoing, "opacity", from: presentation?.opacity ?? outgoing.opacity, to: 0, duration: 0.2, begin: now)
            Motion.ease(outgoing, "transform.translation.x", from: Self.translation(of: outgoing), to: -direction * shift, duration: 0.32, begin: now)
            outgoing.opacity = 0
            outgoing.transform = CATransform3DMakeTranslation(-direction * shift, 0, 0)
        }
        if let incoming = pages[page] {
            let visible = incoming.presentation()?.opacity ?? 0
            // A page that is still fading out continues from where it is; a hidden one starts aside.
            let from = visible > 0.01 ? Self.translation(of: incoming) : direction * shift
            Motion.ease(incoming, "opacity", from: visible, to: 1, duration: 0.3, begin: now + 0.04)
            Motion.ease(incoming, "transform.translation.x", from: from, to: 0, duration: 0.42, begin: now)
            incoming.opacity = 1
            incoming.transform = CATransform3DIdentity
        }
        let spring = CASpringAnimation(perceptualDuration: 0.35, bounce: 0.12)
        spring.keyPath = "position.x"
        spring.fromValue = (selection.presentation() ?? selection).position.x
        let target = layout.frame(of: .tab(page)).midX
        spring.toValue = target
        spring.duration = spring.settlingDuration
        selection.position.x = target
        selection.add(spring, forKey: "position.x")
        for candidate in IslandPage.allCases {
            guard let icon = icons[.tab(candidate)] else { continue }
            let opacity: Float = candidate == page ? 1 : Self.idleTabOpacity
            Motion.ease(icon, "opacity", from: icon.presentation()?.opacity ?? icon.opacity, to: opacity, duration: 0.25, begin: now)
            icon.opacity = opacity
        }
        CATransaction.commit()
    }

    /// Opening: everything grows out of the notch with the pill, and the header settles in first.
    /// Returns when the page's own groups should start, so they follow the header.
    func reveal() -> CFTimeInterval {
        let now = CACurrentMediaTime()
        let visible = root.presentation()?.opacity ?? root.opacity
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        let spring = IslandGeometry.expandSpring
        let grow = CASpringAnimation(perceptualDuration: spring.response, bounce: spring.bounce)
        grow.keyPath = "transform.scale"
        grow.fromValue = (root.presentation()?.value(forKeyPath: "transform.scale") as? CGFloat) ?? Self.hiddenScale
        grow.toValue = 1
        grow.duration = grow.settlingDuration
        root.transform = CATransform3DIdentity
        root.add(grow, forKey: "transform.scale")
        root.opacity = 1
        let pageBegin: CFTimeInterval
        if visible > 0.05 {
            // Reopened while still fading out: continue from what is on screen instead of flashing
            // the groups back to empty.
            Motion.ease(root, "opacity", from: visible, to: 1, duration: 0.2, begin: now)
            pageBegin = 0
        } else {
            root.removeAnimation(forKey: "opacity")
            Motion.reveal([header], begin: now + Motion.firstRevealDelay)
            pageBegin = now + Motion.firstRevealDelay + Motion.revealStagger
        }
        CATransaction.commit()
        return pageBegin
    }

    func conceal() {
        let now = CACurrentMediaTime()
        let presentation = root.presentation()
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        Motion.ease(root, "opacity", from: presentation?.opacity ?? root.opacity, to: 0, duration: 0.14, begin: now)
        Motion.ease(root, "transform.scale", from: (presentation?.value(forKeyPath: "transform.scale") as? CGFloat) ?? 1, to: Self.hiddenScale, duration: 0.2, begin: now)
        root.opacity = 0
        root.transform = CATransform3DMakeScale(Self.hiddenScale, Self.hiddenScale, 1)
        CATransaction.commit()
    }

    func setHover(_ control: HeaderControl?, isPressed: Bool) {
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        if let control {
            let frame = layout.frame(of: control)
            hoverPlate.frame = frame
            hoverPlate.cornerRadius = frame.width / 2
            hoverPlate.opacity = isPressed ? 1 : 0.55
            hoverPlate.isHidden = false
        } else {
            hoverPlate.isHidden = true
        }
        CATransaction.commit()
    }

    private static func translation(of layer: CALayer) -> CGFloat {
        ((layer.presentation() ?? layer).value(forKeyPath: "transform.translation.x") as? CGFloat) ?? 0
    }
}
