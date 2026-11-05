import AppKit

/// Owns the island's window and state. Every entry point here is an event; nothing polls.
final class IslandController: NSObject {
    private static let hoverOpenDelay: TimeInterval = 0.15

    private let panel = IslandPanel()
    private let view = IslandView(frame: .zero)
    private var geometry: IslandGeometry
    private var state = IslandState.collapsed
    private var latestTransition = 0
    private var ignoresHoverUntilExit = false
    private var windowHasExpandedHitArea = false
    private var hoverOpenTimer: Timer?

    override init() {
        geometry = IslandGeometry(display: Self.preferredScreen().islandDisplay)
        super.init()
        // Set this before attaching the view: AppKit may deliver its initial tracking event as the
        // panel is laid out, before it is ordered on screen.
        ignoresHoverUntilExit = geometry.panelFrame(for: .collapsed).contains(NSEvent.mouseLocation)
        panel.contentView = view
        view.onHoverChange = { [unowned self] isInside in
            if ignoresHoverUntilExit {
                if !isInside && !geometry.panelFrame(for: .collapsed).contains(NSEvent.mouseLocation) {
                    ignoresHoverUntilExit = false
                }
                return
            }
            if isInside {
                if !windowHasExpandedHitArea { scheduleHoverOpen() }
            } else {
                hoverOpenTimer?.invalidate()
                hoverOpenTimer = nil
                if windowHasExpandedHitArea && panel.frame.contains(NSEvent.mouseLocation) {
                    view.refreshHoverTrackingArea()
                    return
                }
                transition(to: .collapsed)
            }
        }
        view.onSecondaryClick = { [unowned self] event in showMenu(for: event) }
        applyLayout()
        panel.orderFrontRegardless()
        NotificationCenter.default.addObserver(
            self,
            selector: #selector(screenParametersChanged),
            name: NSApplication.didChangeScreenParametersNotification,
            object: nil
        )
    }

    private func scheduleHoverOpen() {
        guard state == .collapsed, !windowHasExpandedHitArea, hoverOpenTimer == nil else { return }
        let timer = Timer(
            timeInterval: Self.hoverOpenDelay,
            target: self,
            selector: #selector(openAfterHoverDelay(_:)),
            userInfo: nil,
            repeats: false
        )
        hoverOpenTimer = timer
        RunLoop.main.add(timer, forMode: .common)
    }

    @objc private func openAfterHoverDelay(_ timer: Timer) {
        guard hoverOpenTimer === timer else { return }
        hoverOpenTimer = nil
        guard !windowHasExpandedHitArea, panel.frame.contains(NSEvent.mouseLocation) else { return }
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
                ignoresHoverUntilExit = geometry.panelFrame(for: .collapsed).contains(NSEvent.mouseLocation)
                resizePanel(for: .collapsed)
            }
        }
    }

    private func resizePanel(for state: IslandState) {
        windowHasExpandedHitArea = state == .expanded
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
        hoverOpenTimer?.invalidate()
        hoverOpenTimer = nil
        geometry = updated
        if state == .collapsed {
            ignoresHoverUntilExit = geometry.panelFrame(for: .collapsed).contains(NSEvent.mouseLocation)
        }
        applyLayout()
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
