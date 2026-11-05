import AppKit
import QuartzCore

/// Hosts the pill and its flares. Nothing here draws per frame: the pill is a solid-color layer and
/// the flares are fixed shapes that only move, so every frame comes from the render server.
final class IslandView: NSView {
    var onHoverChange: ((Bool) -> Void)?
    var onSecondaryClick: ((NSEvent) -> Void)?

    /// Flares reach this far into the pill, so no seam shows at a fractional position.
    private static let flareOverlap: CGFloat = 1

    private let pill = CALayer()
    private let leftFlare = IslandView.makeFlare(mirrored: false)
    private let rightFlare = IslandView.makeFlare(mirrored: true)
    private var hoverTrackingArea: NSTrackingArea?

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
        layer?.addSublayer(leftFlare)
        layer?.addSublayer(rightFlare)
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) {
        fatalError("IslandView is created in code")
    }

    override func viewDidChangeBackingProperties() {
        super.viewDidChangeBackingProperties()
        // Layers that no view owns default to 1x, which would blur the curve on Retina.
        let scale = window?.backingScaleFactor ?? 2
        leftFlare.contentsScale = scale
        rightFlare.contentsScale = scale
    }

    override func updateTrackingAreas() {
        super.updateTrackingAreas()
        refreshHoverTrackingArea()
    }

    /// Delivered by the window server only on crossing the edge: no polling or global monitor.
    func refreshHoverTrackingArea() {
        if let hoverTrackingArea, hoverTrackingArea.rect == bounds { return }
        if let hoverTrackingArea { removeTrackingArea(hoverTrackingArea) }
        guard !bounds.isEmpty else {
            hoverTrackingArea = nil
            return
        }
        let area = NSTrackingArea(
            rect: bounds,
            options: [.mouseEnteredAndExited, .activeAlways],
            owner: self
        )
        addTrackingArea(area)
        hoverTrackingArea = area
    }

    override func mouseEntered(with event: NSEvent) { onHoverChange?(true) }
    override func mouseExited(with event: NSEvent) { onHoverChange?(false) }
    override func rightMouseDown(with event: NSEvent) { onSecondaryClick?(event) }

    /// Keeps the island pinned to the top center of the view after the window changes size.
    func pinPill() {
        withoutImplicitAnimations {
            pill.position = CGPoint(x: bounds.midX, y: bounds.maxY)
            let flares = flarePositions(pillWidth: pill.bounds.width)
            leftFlare.position = CGPoint(x: flares.left, y: bounds.maxY)
            rightFlare.position = CGPoint(x: flares.right, y: bounds.maxY)
        }
    }

    func setPill(size: CGSize, cornerRadius: CGFloat) {
        withoutImplicitAnimations {
            pill.bounds = CGRect(origin: .zero, size: size)
            pill.cornerRadius = cornerRadius
            let flares = flarePositions(pillWidth: size.width)
            leftFlare.position.x = flares.left
            rightFlare.position.x = flares.right
        }
    }

    /// Hands the transition to the render server. This process does no work until `completion`.
    /// Starts from the on-screen values, so reversing mid-flight does not jump.
    func animatePill(to size: CGSize, cornerRadius: CGFloat, completion: @escaping () -> Void) {
        let current = pill.presentation() ?? pill
        // Derived from the pill, not read from the flares: the window may have just resized, and
        // their on-screen positions would still be in the old coordinates.
        let flaresFrom = flarePositions(pillWidth: current.bounds.width)
        let flaresTo = flarePositions(pillWidth: size.width)
        CATransaction.begin()
        CATransaction.setCompletionBlock(completion)
        addSpring(to: pill, keyPath: "bounds", from: NSValue(rect: current.bounds), to: NSValue(rect: CGRect(origin: .zero, size: size)))
        addSpring(to: pill, keyPath: "cornerRadius", from: current.cornerRadius, to: cornerRadius)
        // The same spring over half the distance keeps each flare on the pill's edge throughout.
        addSpring(to: leftFlare, keyPath: "position.x", from: flaresFrom.left, to: flaresTo.left)
        addSpring(to: rightFlare, keyPath: "position.x", from: flaresFrom.right, to: flaresTo.right)
        setPill(size: size, cornerRadius: cornerRadius)
        CATransaction.commit()
    }

    private func flarePositions(pillWidth: CGFloat) -> (left: CGFloat, right: CGFloat) {
        let halfWidth = pillWidth / 2 - Self.flareOverlap
        return (bounds.midX - halfWidth, bounds.midX + halfWidth)
    }

    private func addSpring(to layer: CALayer, keyPath: String, from: Any, to: Any) {
        let spring = CASpringAnimation(perceptualDuration: 0.38, bounce: 0.18)
        spring.keyPath = keyPath
        spring.fromValue = from
        spring.toValue = to
        spring.duration = spring.settlingDuration
        layer.add(spring, forKey: keyPath)
    }

    /// A square with a quarter circle removed, so the top edge tapers out from the pill. The path is
    /// set once and never animated: animating a path is the expensive end of Core Animation.
    private static func makeFlare(mirrored: Bool) -> CAShapeLayer {
        let radius = IslandGeometry.flareRadius
        let width = radius + flareOverlap
        let path = CGMutablePath()
        path.move(to: CGPoint(x: 0, y: radius))
        path.addLine(to: CGPoint(x: width, y: radius))
        path.addLine(to: CGPoint(x: width, y: 0))
        path.addLine(to: CGPoint(x: radius, y: 0))
        path.addArc(center: .zero, radius: radius, startAngle: 0, endAngle: .pi / 2, clockwise: false)
        path.closeSubpath()

        var mirror = CGAffineTransform(translationX: width, y: 0).scaledBy(x: -1, y: 1)
        let flare = CAShapeLayer()
        flare.path = mirrored ? path.copy(using: &mirror) : path
        flare.fillColor = NSColor.black.cgColor
        flare.bounds = CGRect(x: 0, y: 0, width: width, height: radius)
        flare.anchorPoint = CGPoint(x: mirrored ? 0 : 1, y: 1)
        return flare
    }

    private func withoutImplicitAnimations(_ body: () -> Void) {
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        body()
        CATransaction.commit()
    }
}
