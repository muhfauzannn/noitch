import AppKit

/// Owns the island's window and state. Every entry point here is an event; nothing polls.
final class IslandController: NSObject {
    private let panel = IslandPanel()
    private let view = IslandView(frame: .zero)
    private var geometry: IslandGeometry
    private var state = IslandState.collapsed
    private var latestTransition = 0
    private var settings = AppSettings.load()
    private let pages: PageController
    private var settingsWindow: SettingsWindowController?

    override init() {
        geometry = IslandGeometry(display: Self.preferredScreen().islandDisplay)
        pages = PageController(settings: settings)
        super.init()
        panel.contentView = view
        pages.onSceneCreated = { [unowned self] layer in view.setContent(layer, size: IslandGeometry.expandedSize) }
        pages.onOpenSettings = { [unowned self] in openSettings() }
        view.onHorizontalScroll = { [unowned self] delta, time in if state == .expanded { pages.scrolled(by: delta, at: time) } }
        view.onPointerMoved = { [unowned self] point in if state == .expanded { pages.pointerMoved(to: point) } }
        view.onPrimaryDown = { [unowned self] point in
            if state == .expanded {
                pages.pointerDown(at: point)
            } else if !settings.openOnHover {
                transition(to: .expanded)
            }
        }
        view.onPrimaryDragged = { [unowned self] point in if state == .expanded { pages.pointerDragged(to: point) } }
        view.onPrimaryUp = { [unowned self] point in
            guard state == .expanded else { return }
            pages.pointerUp(at: point)
            // A slider drag can end outside the island, where no exit event will follow.
            if !panel.frame.contains(NSEvent.mouseLocation) { transition(to: .collapsed) }
        }
        view.onHoverChange = { [unowned self] isInside, eventLocation in
            if isInside {
                if geometry.panelFrame(for: .collapsed).contains(eventLocation) { openIfPointerInNotch() }
            } else if !pages.isInteracting {
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
        guard state == .collapsed, settings.openOnHover,
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
        if newState == .expanded {
            resizePanel(for: .expanded)
            pages.islandWillOpen(size: IslandGeometry.expandedSize, headerHeight: geometry.collapsedSize.height, scale: panel.backingScaleFactor)
        } else {
            pages.islandWillClose()
        }
        view.animatePill(
            to: geometry.pillSize(for: newState),
            cornerRadius: geometry.cornerRadius(for: newState),
            spring: IslandGeometry.spring(for: newState)
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
        menu.addItem(withTitle: "Settings…", action: #selector(openSettings), keyEquivalent: "").target = self
        menu.addItem(.separator())
        menu.addItem(withTitle: "Quit Noitch", action: #selector(NSApplication.terminate(_:)), keyEquivalent: "")
        NSMenu.popUpContextMenu(menu, with: event, for: view)
    }

    @objc private func openSettings() {
        if settingsWindow == nil {
            let window = SettingsWindowController(settings: settings)
            window.onChange = { [unowned self] updated in
                settings = updated
                pages.apply(updated)
            }
            window.onClose = { [unowned self] in settingsWindow = nil }
            settingsWindow = window
        }
        settingsWindow?.show()
    }

    /// The adapter would otherwise outlive the app until its next write.
    func stop() {
        pages.stop()
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
