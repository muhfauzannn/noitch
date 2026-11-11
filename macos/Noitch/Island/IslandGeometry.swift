import CoreGraphics

/// Where the island sits on one screen. Pure values in AppKit screen coordinates (origin bottom-left),
/// so all layout decisions can be tested without a window.
struct IslandGeometry: Equatable {
    /// The display traits that drive layout.
    struct Display: Equatable {
        var frame: CGRect
        /// Height of the camera housing, or 0 on a display without one.
        var notchHeight: CGFloat
        /// Width of the camera housing, or 0 on a display without one.
        var notchWidth: CGFloat
        var menuBarHeight: CGFloat
    }

    static let expandedSize = CGSize(width: 580, height: 180)
    static let collapsedCornerRadius: CGFloat = 10
    static let expandedCornerRadius: CGFloat = 24
    /// The concave curve on each side, where the island meets the top of the screen.
    static let flareRadius: CGFloat = 8
    /// Used on displays without a notch, where the island is drawn over the menu bar.
    static let virtualNotchWidth: CGFloat = 180
    /// Flares and spring overshoot must stay inside the window, or they would be clipped.
    static let overshootMargin: CGFloat = 16
    /// Lets the hover target include the screen's topmost cursor position.
    static let topEdgeHitMargin: CGFloat = 1
    /// Opening is slower and softer than closing: the eye follows content in, but wants it gone fast.
    static let expandSpring = Spring(response: 0.5, bounce: 0.14)
    static let collapseSpring = Spring(response: 0.38, bounce: 0.18)

    struct Spring: Equatable {
        var response: Double
        var bounce: Double
    }

    static func spring(for state: IslandState) -> Spring {
        state == .expanded ? expandSpring : collapseSpring
    }

    let display: Display
    /// Music is playing and the closed pill has wings for it, see `LiveLayout`.
    var isLive = false

    init(display: Display, isLive: Bool = false) {
        self.display = display
        self.isLive = isLive
    }

    /// The notch, or the virtual one: the closed pill without wings.
    var collapsedSize: CGSize {
        display.notchHeight > 0
            ? CGSize(width: display.notchWidth, height: display.notchHeight)
            : CGSize(width: Self.virtualNotchWidth, height: display.menuBarHeight)
    }

    /// Widening into the wings and back is a smaller move than opening, and a gentle one.
    static let liveSpring = Spring(response: 0.42, bounce: 0.16)

    func pillSize(for state: IslandState) -> CGSize {
        if state == .expanded { return Self.expandedSize }
        return isLive ? LiveLayout(collapsed: collapsedSize).size : collapsedSize
    }

    func cornerRadius(for state: IslandState) -> CGFloat {
        state == .expanded ? Self.expandedCornerRadius : Self.collapsedCornerRadius
    }

    /// The window is the hit region, so while collapsed it must not cover more than the pill and
    /// its flares.
    /// It is sized only at the two ends of a transition, never per frame.
    func panelFrame(for state: IslandState) -> CGRect {
        let pill = pillSize(for: state)
        let expanded = state == .expanded
        let sideMargin = expanded ? Self.overshootMargin : Self.flareRadius
        let bottomMargin = expanded ? Self.overshootMargin : 0
        let size = CGSize(
            width: pill.width + sideMargin * 2,
            height: pill.height + bottomMargin + Self.topEdgeHitMargin
        )
        return CGRect(
            x: display.frame.midX - size.width / 2,
            y: display.frame.maxY - size.height + Self.topEdgeHitMargin,
            width: size.width,
            height: size.height
        )
    }
}
