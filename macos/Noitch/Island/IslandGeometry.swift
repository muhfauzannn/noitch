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

    static let expandedSize = CGSize(width: 440, height: 140)
    static let collapsedCornerRadius: CGFloat = 10
    static let expandedCornerRadius: CGFloat = 24
    /// The concave curve on each side, where the island meets the top of the screen.
    static let flareRadius: CGFloat = 8
    /// Used on displays without a notch, where the island is drawn over the menu bar.
    static let virtualNotchWidth: CGFloat = 180
    /// Flares and spring overshoot must stay inside the window, or they would be clipped.
    static let overshootMargin: CGFloat = 16

    let display: Display

    var collapsedSize: CGSize {
        display.notchHeight > 0
            ? CGSize(width: display.notchWidth, height: display.notchHeight)
            : CGSize(width: Self.virtualNotchWidth, height: display.menuBarHeight)
    }

    func pillSize(for state: IslandState) -> CGSize {
        state == .expanded ? Self.expandedSize : collapsedSize
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
        let size = CGSize(width: pill.width + sideMargin * 2, height: pill.height + bottomMargin)
        return CGRect(
            x: display.frame.midX - size.width / 2,
            y: display.frame.maxY - size.height,
            width: size.width,
            height: size.height
        )
    }
}
