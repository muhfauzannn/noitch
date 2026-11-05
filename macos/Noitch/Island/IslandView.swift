import AppKit
import QuartzCore

/// Hosts the pill layer. Nothing here draws: the pill is a solid-color layer, so the window has no
/// backing store to fill and every frame of a transition is produced by the render server.
final class IslandView: NSView {
    var onHoverChange: ((Bool) -> Void)?
    var onSecondaryClick: ((NSEvent) -> Void)?

    private let pill = CALayer()

    override init(frame: NSRect) {
        super.init(frame: frame)
        wantsLayer = true
        layerContentsRedrawPolicy = .never
        pill.backgroundColor = NSColor.black.cgColor
        pill.anchorPoint = CGPoint(x: 0.5, y: 1)
        pill.cornerCurve = .continuous
        // Layers are y-up here: MinY is the bottom edge. The top edge stays flush with the screen.
        pill.maskedCorners = [.layerMinXMinYCorner, .layerMaxXMinYCorner]
        layer?.addSublayer(pill)
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) {
        fatalError("IslandView is created in code")
    }

    override func updateTrackingAreas() {
        super.updateTrackingAreas()
        if trackingAreas.isEmpty {
            // Delivered by the window server only on crossing the edge: no polling, no global monitor.
            addTrackingArea(NSTrackingArea(
                rect: .zero,
                options: [.mouseEnteredAndExited, .activeAlways, .inVisibleRect],
                owner: self
            ))
        }
    }

    override func mouseEntered(with event: NSEvent) { onHoverChange?(true) }
    override func mouseExited(with event: NSEvent) { onHoverChange?(false) }
    override func rightMouseDown(with event: NSEvent) { onSecondaryClick?(event) }

    /// Keeps the pill pinned to the top center of the view after the window changes size.
    func pinPill() {
        withoutImplicitAnimations {
            pill.position = CGPoint(x: bounds.midX, y: bounds.maxY)
        }
    }

    func setPill(size: CGSize, cornerRadius: CGFloat) {
        withoutImplicitAnimations {
            pill.bounds = CGRect(origin: .zero, size: size)
            pill.cornerRadius = cornerRadius
        }
    }

    /// Hands the transition to the render server. This process does no work until `completion`.
    /// Starts from the on-screen values, so reversing mid-flight does not jump.
    func animatePill(to size: CGSize, cornerRadius: CGFloat, completion: @escaping () -> Void) {
        let current = pill.presentation() ?? pill
        CATransaction.begin()
        CATransaction.setCompletionBlock(completion)
        addSpring(keyPath: "bounds", from: NSValue(rect: current.bounds), to: NSValue(rect: CGRect(origin: .zero, size: size)))
        addSpring(keyPath: "cornerRadius", from: current.cornerRadius, to: cornerRadius)
        setPill(size: size, cornerRadius: cornerRadius)
        CATransaction.commit()
    }

    private func addSpring(keyPath: String, from: Any, to: Any) {
        let spring = CASpringAnimation(perceptualDuration: 0.38, bounce: 0.18)
        spring.keyPath = keyPath
        spring.fromValue = from
        spring.toValue = to
        spring.duration = spring.settlingDuration
        pill.add(spring, forKey: keyPath)
    }

    private func withoutImplicitAnimations(_ body: () -> Void) {
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        body()
        CATransaction.commit()
    }
}
