import AppKit

/// Owns the island's window and state. Every entry point here is an event; nothing polls.
final class IslandController: NSObject {
    private let panel = IslandPanel()
    private let view = IslandView(frame: .zero)
    private var geometry: IslandGeometry
    private var state = IslandState.collapsed
    private var latestTransition = 0

    override init() {
        geometry = IslandGeometry(display: Self.preferredScreen().islandDisplay)
        super.init()
        panel.contentView = view
        view.onHoverChange = { [unowned self] isInside, eventLocation in
            if isInside {
                if geometry.panelFrame(for: .collapsed).contains(eventLocation) { openIfPointerInNotch() }
            } else {
                transition(to: .collapsed)
            }
        }
        view.onSecondaryClick = { [unowned self] event in showMenu(for: event) }
        // Reconcile after queued boundary events: a fast return may overtake an older exit.
        panel.onPointerEvent = { [unowned self] in
            if geometry.panelFrame(for: .collapsed).contains(NSEvent.mouseLocation) {
                openIfPointerInNotch()
            }
        }
        applyLayout()
        panel.orderFrontRegardless()
        if geometry.panelFrame(for: .collapsed).contains(NSEvent.mouseLocation) { openIfPointerInNotch() }
        NotificationCenter.default.addObserver(
            self,
            selector: #selector(screenParametersChanged),
            name: NSApplication.didChangeScreenParametersNotification,
            object: nil
        )
    }

    private func openIfPointerInNotch() {
        guard state == .collapsed,
              geometry.panelFrame(for: .collapsed).contains(NSEvent.mouseLocation) else { return }
        transition(to: .expanded)
    }

    private func transition(to newState: IslandState) {
        guard newState != state else { return }
        state = newState
        latestTransition += 1
        let transition = latestTransition
        // Grow before expanding and shrink after collapsing: the window changes size twice per
        // round trip instead of on every frame.
        if newState == .expanded { resizePanel(for: .expanded) }
        view.animatePill(
            to: geometry.pillSize(for: newState),
            cornerRadius: geometry.cornerRadius(for: newState)
        ) { [unowned self] in
            // Replacing a running animation fires its completion at once, while the new one is
            // still on screen. Shrinking then would cut it off, so only the latest may finish.
            if transition == latestTransition, state == .collapsed {
                resizePanel(for: .collapsed)
                if geometry.panelFrame(for: .collapsed).contains(NSEvent.mouseLocation) { openIfPointerInNotch() }
            }
        }
    }

    private func resizePanel(for state: IslandState) {
        panel.setFrame(geometry.panelFrame(for: state), display: false)
        view.pinPill()
        view.refreshHoverTrackingArea()
    }

    private func applyLayout() {
        resizePanel(for: state)
        view.setPill(size: geometry.pillSize(for: state), cornerRadius: geometry.cornerRadius(for: state))
    }

    @objc private func screenParametersChanged() {
        let updated = IslandGeometry(display: Self.preferredScreen().islandDisplay)
        guard updated != geometry else { return }
        geometry = updated
        applyLayout()
        if geometry.panelFrame(for: .collapsed).contains(NSEvent.mouseLocation) { openIfPointerInNotch() }
    }

    private func showMenu(for event: NSEvent) {
        let menu = NSMenu()
        menu.addItem(withTitle: "Quit Noitch", action: #selector(NSApplication.terminate(_:)), keyEquivalent: "")
        NSMenu.popUpContextMenu(menu, with: event, for: view)
    }

    private static func preferredScreen() -> NSScreen {
        guard let screen = NSScreen.screens.first(where: { $0.safeAreaInsets.top > 0 }) ?? NSScreen.main else {
            preconditionFailure("A running GUI session always has a screen")
        }
        return screen
    }
}

private extension NSScreen {
    var islandDisplay: IslandGeometry.Display {
        let left = auxiliaryTopLeftArea?.width ?? 0
        let right = auxiliaryTopRightArea?.width ?? 0
        let notchHeight = safeAreaInsets.top
        return IslandGeometry.Display(
            frame: frame,
            notchHeight: notchHeight,
            notchWidth: notchHeight > 0 ? frame.width - left - right : 0,
            menuBarHeight: frame.maxY - visibleFrame.maxY
        )
    }
}
