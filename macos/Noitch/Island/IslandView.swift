import AppKit
import QuartzCore

/// Hosts the pill and its flares. Nothing here draws per frame: the pill is a solid-color layer and
/// the flares are fixed shapes that only move, so every frame comes from the render server.
final class IslandView: NSView {
    var onHoverChange: ((Bool, CGPoint) -> Void)?
    var onSecondaryClick: ((NSEvent) -> Void)?
    /// Points are from the pill's top-left corner with y growing downward, as `HomeLayout` measures.
    var onPointerMoved: ((CGPoint) -> Void)?
    var onPrimaryDown: ((CGPoint) -> Void)?
    var onPrimaryDragged: ((CGPoint) -> Void)?
    var onPrimaryUp: ((CGPoint) -> Void)?
    /// Horizontal trackpad scroll in points, positive toward the next page, and the event time.
    var onHorizontalScroll: ((Double, Double) -> Void)?

    /// Flares reach this far into the pill, so no seam shows at a fractional position.
    private static let flareOverlap: CGFloat = 1

    private let pill = CALayer()
    /// Holds the content and clips it to the pill's shape as the pill moves, so nothing shows
    /// outside the black while it opens or closes. Its bounds follow the pill with the same spring;
    /// the bounds origin keeps the content still while the visible window grows around it.
    private let clip = CALayer()
    private let leftFlare = IslandView.makeFlare(mirrored: false)
    private let rightFlare = IslandView.makeFlare(mirrored: true)
    private var hoverTrackingArea: NSTrackingArea?
    private weak var content: CALayer?
    private var contentSize = CGSize.zero

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
        clip.anchorPoint = CGPoint(x: 0.5, y: 1)
        clip.masksToBounds = true
        clip.cornerCurve = .continuous
        clip.maskedCorners = pill.maskedCorners
        layer?.addSublayer(clip)
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
        guard !bounds.isEmpty else {
            if let hoverTrackingArea { removeTrackingArea(hoverTrackingArea) }
            hoverTrackingArea = nil
            return
        }
        if let hoverTrackingArea, hoverTrackingArea.rect == bounds { return }
        if let hoverTrackingArea { removeTrackingArea(hoverTrackingArea) }
        var options: NSTrackingArea.Options = [.mouseEnteredAndExited, .mouseMoved, .activeAlways]
        // A resized area can appear under a stationary pointer. Seed its inside state so the next
        // real exit is delivered even though there was no crossing into the replacement area.
        if let window, bounds.contains(convert(window.convertPoint(fromScreen: NSEvent.mouseLocation), from: nil)) {
            options.insert(.assumeInside)
        }
        let area = NSTrackingArea(
            rect: bounds,
            options: options,
            owner: self
        )
        addTrackingArea(area)
        hoverTrackingArea = area
    }

    override func mouseEntered(with event: NSEvent) {
        guard event.trackingArea === hoverTrackingArea else { return }
        onHoverChange?(true, screenLocation(of: event))
    }

    override func mouseExited(with event: NSEvent) {
        guard event.trackingArea === hoverTrackingArea else { return }
        onHoverChange?(false, screenLocation(of: event))
    }

    override func mouseMoved(with event: NSEvent) {
        onHoverChange?(true, screenLocation(of: event))
        onPointerMoved?(contentLocation(of: event))
    }

    /// The panel never becomes key, so without this the first click would only be swallowed.
    override func acceptsFirstMouse(for event: NSEvent?) -> Bool { true }

    override func mouseDown(with event: NSEvent) { onPrimaryDown?(contentLocation(of: event)) }

    override func mouseDragged(with event: NSEvent) { onPrimaryDragged?(contentLocation(of: event)) }

    override func mouseUp(with event: NSEvent) { onPrimaryUp?(contentLocation(of: event)) }

    private func contentLocation(of event: NSEvent) -> CGPoint {
        let point = convert(event.locationInWindow, from: nil)
        return CGPoint(
            x: point.x - (bounds.midX - contentSize.width / 2),
            y: bounds.maxY - IslandGeometry.topEdgeHitMargin - point.y
        )
    }

    /// Shows the closed island's music layers, hung from the top center of the pill below any page.
    func setLive(_ layer: CALayer) {
        live = layer
        withoutImplicitAnimations {
            layer.anchorPoint = CGPoint(x: 0.5, y: 1)
            layer.zPosition = -1
            clip.addSublayer(layer)
            placeLive()
        }
    }

    private weak var live: CALayer?

    /// Top center of the content area, which moves when the open width changes.
    private func placeLive() {
        let full = contentSize == .zero ? IslandGeometry.expandedSize : contentSize
        live?.position = CGPoint(x: full.width / 2, y: full.height)
    }

    /// Shows a feature's layers on the pill. `size` is the area they cover when expanded.
    func setContent(_ layer: CALayer, size: CGSize) {
        contentSize = size
        content = layer
        // Top-center of the content sits at the top-center of the full-size area clip shows part of.
        layer.anchorPoint = CGPoint(x: 0.5, y: 1)
        layer.position = CGPoint(x: size.width / 2, y: size.height)
        clip.addSublayer(layer)
        withoutImplicitAnimations { placeLive() }
        pinPill()
    }

    override func scrollWheel(with event: NSEvent) {
        onHoverChange?(true, screenLocation(of: event))
        // Momentum after the fingers lift would carry on to the page after next.
        if event.momentumPhase.isEmpty, abs(event.scrollingDeltaX) > abs(event.scrollingDeltaY) {
            let points = event.scrollingDeltaX * (event.hasPreciseScrollingDeltas ? 1 : 10)
            // Fingers moving left bring the next page in, whichever scroll direction is set.
            onHorizontalScroll?(Double(event.isDirectionInvertedFromDevice ? -points : points), event.timestamp)
        }
        super.scrollWheel(with: event)
    }

    private func screenLocation(of event: NSEvent) -> CGPoint {
        // Window coordinates become ambiguous when a queued event spans a panel resize.
        if let point = event.cgEvent?.location, let primary = NSScreen.screens.first {
            return CGPoint(x: point.x, y: primary.frame.maxY - point.y)
        }
        return window?.convertPoint(toScreen: event.locationInWindow) ?? NSEvent.mouseLocation
    }
    override func rightMouseDown(with event: NSEvent) { onSecondaryClick?(event) }

    /// Keeps the island pinned to the top center of the view after the window changes size.
    func pinPill() {
        withoutImplicitAnimations {
            let top = bounds.maxY - IslandGeometry.topEdgeHitMargin
            pill.position = CGPoint(x: bounds.midX, y: top)
            clip.position = pill.position
            let flares = flarePositions(pillWidth: pill.bounds.width)
            leftFlare.position = CGPoint(x: flares.left, y: top)
            rightFlare.position = CGPoint(x: flares.right, y: top)
        }
    }

    func setPill(size: CGSize, cornerRadius: CGFloat) {
        withoutImplicitAnimations {
            pill.bounds = CGRect(origin: .zero, size: size)
            pill.cornerRadius = cornerRadius
            clip.bounds = clipBounds(for: size)
            clip.cornerRadius = cornerRadius
            let flares = flarePositions(pillWidth: size.width)
            leftFlare.position.x = flares.left
            rightFlare.position.x = flares.right
        }
    }

    /// Hands the transition to the render server. This process does no work until `completion`.
    /// Starts from the on-screen values, so reversing mid-flight does not jump.
    func animatePill(to size: CGSize, cornerRadius: CGFloat, spring: IslandGeometry.Spring, completion: @escaping () -> Void) {
        let current = pill.presentation() ?? pill
        // Derived from the pill, not read from the flares: the window may have just resized, and
        // their on-screen positions would still be in the old coordinates.
        let flaresFrom = flarePositions(pillWidth: current.bounds.width)
        let flaresTo = flarePositions(pillWidth: size.width)
        CATransaction.begin()
        CATransaction.setCompletionBlock(completion)
        addSpring(spring, to: pill, keyPath: "bounds", from: NSValue(rect: current.bounds), to: NSValue(rect: CGRect(origin: .zero, size: size)))
        addSpring(spring, to: pill, keyPath: "cornerRadius", from: current.cornerRadius, to: cornerRadius)
        // The same spring on the same numbers keeps the clip exactly on the pill.
        addSpring(spring, to: clip, keyPath: "bounds", from: NSValue(rect: clipBounds(for: current.bounds.size)), to: NSValue(rect: clipBounds(for: size)))
        addSpring(spring, to: clip, keyPath: "cornerRadius", from: current.cornerRadius, to: cornerRadius)
        // The same spring over half the distance keeps each flare on the pill's edge throughout.
        addSpring(spring, to: leftFlare, keyPath: "position.x", from: flaresFrom.left, to: flaresTo.left)
        addSpring(spring, to: rightFlare, keyPath: "position.x", from: flaresFrom.right, to: flaresTo.right)
        setPill(size: size, cornerRadius: cornerRadius)
        CATransaction.commit()
    }

    /// The visible part of the full-size content area: centered horizontally and hanging from the top.
    /// Both origin terms are linear in the size, so springing the rect keeps the content still.
    private func clipBounds(for size: CGSize) -> CGRect {
        let full = contentSize == .zero ? IslandGeometry.expandedSize : contentSize
        return CGRect(x: (full.width - size.width) / 2, y: full.height - size.height, width: size.width, height: size.height)
    }

    private func flarePositions(pillWidth: CGFloat) -> (left: CGFloat, right: CGFloat) {
        let halfWidth = pillWidth / 2 - Self.flareOverlap
        return (bounds.midX - halfWidth, bounds.midX + halfWidth)
    }

    private func addSpring(_ parameters: IslandGeometry.Spring, to layer: CALayer, keyPath: String, from: Any, to: Any) {
        let spring = CASpringAnimation(perceptualDuration: parameters.response, bounce: parameters.bounce)
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
