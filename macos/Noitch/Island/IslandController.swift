import AppKit

/// Owns the island's window and state. Every entry point here is an event; nothing polls.
final class IslandController: NSObject {
    private let panel = IslandPanel()
    private let view = IslandView(frame: .zero)
    private var geometry: IslandGeometry
    private var state = IslandState.collapsed
    private var latestTransition = 0
    private var settings = AppSettings.load()
    private let monitor = NowPlayingMonitor()
    private let pages: PageController
    private let live: LiveController
    private var settingsWindow: SettingsWindowController?
    /// A one-shot wait while the pointer rests in the notch, when a hover delay is set.
    private var hoverTimer: Timer?

    override init() {
        geometry = IslandGeometry(display: Self.preferredScreen(settings.general.display).islandDisplay)
        pages = PageController(settings: settings, monitor: monitor)
        live = LiveController(monitor: monitor, settings: settings.home)
        super.init()
        panel.contentView = view
        live.onSceneCreated = { [unowned self] layer in view.setLive(layer) }
        live.onShapeChange = { [unowned self] in liveChanged() }
        live.setCollapsedSize(geometry.collapsedSize, scale: panel.backingScaleFactor)
        pages.onSceneCreated = { [unowned self] layer in view.setContent(layer, size: geometry.expandedSize) }
        pages.onOpenSettings = { [unowned self] in openSettings() }
        view.onHorizontalScroll = { [unowned self] delta, time in if state == .expanded { pages.scrolled(by: delta, at: time) } }
        view.onPointerMoved = { [unowned self] point in if state == .expanded { pages.pointerMoved(to: point) } }
        view.onPrimaryDown = { [unowned self] point in
            if state == .expanded {
                pages.pointerDown(at: point)
            } else {
                // A click opens at once, whatever the hover settings.
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
            } else {
                cancelHoverTimer()
                if !pages.isInteracting { transition(to: .collapsed) }
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
        #if DEBUG
        // `--settings` opens the settings window at launch, for checking its layout.
        if ProcessInfo.processInfo.arguments.contains("--settings") { openSettings() }
        // `--open` opens the island at launch, for screenshots without moving the cursor.
        if ProcessInfo.processInfo.arguments.contains("--open") { transition(to: .expanded) }
        #endif
    }

    private func openIfPointerInNotch() {
        guard state == .collapsed, settings.general.openOnHover,
              geometry.panelFrame(for: .collapsed).contains(NSEvent.mouseLocation) else { return }
        let delay = settings.general.hoverDelay
        guard delay > 0 else {
            transition(to: .expanded)
            return
        }
        guard hoverTimer == nil else { return }
        let timer = Timer(timeInterval: delay, target: self, selector: #selector(hoverDelayEnded), userInfo: nil, repeats: false)
        timer.tolerance = 0.02
        RunLoop.main.add(timer, forMode: .common)
        hoverTimer = timer
    }

    @objc private func hoverDelayEnded() {
        hoverTimer = nil
        // Only a pointer that stayed opens it; one that left has already cancelled this.
        if state == .collapsed, geometry.panelFrame(for: .collapsed).contains(NSEvent.mouseLocation) {
            transition(to: .expanded)
        }
    }

    private func cancelHoverTimer() {
        hoverTimer?.invalidate()
        hoverTimer = nil
    }

    private func transition(to newState: IslandState) {
        guard newState != state else { return }
        state = newState
        latestTransition += 1
        let transition = latestTransition
        // Grow before expanding and shrink after collapsing: the window changes size twice per
        // round trip instead of on every frame.
        cancelHoverTimer()
        if newState == .expanded { live.islandWillOpen() } else { live.islandWillClose() }
        if newState == .expanded {
            if settings.general.haptics {
                NSHapticFeedbackManager.defaultPerformer.perform(.levelChange, performanceTime: .now)
            }
            resizePanel(for: .expanded)
            pages.islandWillOpen(size: geometry.expandedSize, headerHeight: geometry.collapsedSize.height, scale: panel.backingScaleFactor)
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

    /// The closed pill widens into its wings when music starts, and narrows when it ends; it grows
    /// down for the lyric line and back. Like opening, the window grows before the pill does and
    /// shrinks after, never per frame.
    private func liveChanged() {
        let before = geometry.pillSize(for: .collapsed)
        geometry.isLive = live.isActive
        geometry.showsLiveLyric = live.showsLyric
        let after = geometry.pillSize(for: .collapsed)
        guard state == .collapsed, after != before else { return }
        latestTransition += 1
        let transition = latestTransition
        // Grows first in any direction it grows in; the completion trims it to the final size.
        if after.width > before.width || after.height > before.height {
            panel.setFrame(geometry.panelFrame(for: .collapsed).union(panel.frame), display: false)
            view.pinPill()
            view.refreshHoverTrackingArea()
        }
        view.animatePill(
            to: geometry.pillSize(for: .collapsed),
            cornerRadius: geometry.cornerRadius(for: .collapsed),
            spring: IslandGeometry.liveSpring
        ) { [unowned self] in
            if transition == latestTransition, state == .collapsed { resizePanel(for: .collapsed) }
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
        let updated = IslandGeometry(display: Self.preferredScreen(settings.general.display).islandDisplay, isLive: live.isActive, showsLiveLyric: live.showsLyric)
        guard updated != geometry else { return }
        geometry = updated
        live.setCollapsedSize(geometry.collapsedSize, scale: panel.backingScaleFactor)
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
                let old = settings
                settings = updated
                pages.apply(updated)
                live.apply(updated.home)
                if updated.general.display != old.general.display { screenParametersChanged() }
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

    /// The first screen is the one with the menu bar.
    private static func preferredScreen(_ choice: DisplayChoice) -> NSScreen {
        let notched = choice == .notch ? NSScreen.screens.first(where: { $0.safeAreaInsets.top > 0 }) : nil
        guard let screen = notched ?? NSScreen.screens.first ?? NSScreen.main else {
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
