import AppKit

/// Borderless, non-activating overlay. Never takes focus from the frontmost app.
final class IslandPanel: NSPanel {
    init() {
        super.init(
            contentRect: .zero,
            styleMask: [.borderless, .nonactivatingPanel],
            backing: .buffered,
            defer: true
        )
        isOpaque = false
        backgroundColor = .clear
        hasShadow = false
        isMovable = false
        animationBehavior = .none
        // Over the menu bar, which it covers on displays without a notch, and over other notch
        // apps (Boring Notch sits at mainMenu + 3), whose windows would take its mouse events.
        // Still well below open menus.
        level = NSWindow.Level(NSWindow.Level.mainMenu.rawValue + 4)
        collectionBehavior = [.canJoinAllSpaces, .stationary, .ignoresCycle, .fullScreenAuxiliary]
    }

    override var canBecomeKey: Bool { false }
    override var canBecomeMain: Bool { false }

    /// AppKit would otherwise push the panel below the menu bar.
    override func constrainFrameRect(_ frameRect: NSRect, to screen: NSScreen?) -> NSRect {
        frameRect
    }
}
