import AppKit

/// Owns which page the island shows and routes the pointer to it. Only the current page is "open"
/// to its feature, so a page you aren't looking at runs nothing and is released after its grace
/// period, exactly as if the island had closed.
final class PageController {
    private static let releaseDelay: TimeInterval = 10

    /// Set by the island, which owns the panel and adds the layers to its view.
    var onSceneCreated: ((CALayer) -> Void)?
    var onOpenSettings: (() -> Void)?

    private let home: HomeController
    private let usage = UsageController()
    private var scene: PagerScene?
    private var page = IslandPage.home
    private var isOpen = false
    private var size = CGSize.zero
    private var headerHeight: CGFloat = 0
    private var scale: CGFloat = 2
    private var swipe = PageSwipe()
    private var releaseTimer: Timer?

    private var hovered: HeaderControl?
    private var pressed: HeaderControl?
    /// True while a press that began on the page, not the header, is held.
    private var pageHasPointer = false

    init(settings: AppSettings) {
        home = HomeController(settings: settings)
        home.onSceneCreated = { [unowned self] layer in scene?.attach(layer, for: .home, visible: page == .home) }
        usage.onSceneCreated = { [unowned self] layer in scene?.attach(layer, for: .usage, visible: page == .usage) }
    }

    var isInteracting: Bool { home.isInteracting }

    func apply(_ settings: AppSettings) { home.apply(settings) }

    func stop() { home.stop() }

    // MARK: Island lifecycle

    func islandWillOpen(size: CGSize, headerHeight: CGFloat, scale: CGFloat) {
        releaseTimer?.invalidate()
        releaseTimer = nil
        isOpen = true
        if scene == nil || size != self.size || headerHeight != self.headerHeight || scale != self.scale {
            scene?.root.removeFromSuperlayer()
            let scene = PagerScene(layout: HeaderLayout(size: size, headerHeight: headerHeight), scale: scale)
            self.scene = scene
            self.size = size
            self.headerHeight = headerHeight
            self.scale = scale
            scene.select(page)
            // A feature still in its grace period keeps its layers; take them back.
            if let layer = home.sceneRoot { scene.attach(layer, for: .home, visible: page == .home) }
            if let layer = usage.sceneRoot { scene.attach(layer, for: .usage, visible: page == .usage) }
            onSceneCreated?(scene.root)
        }
        showFeature(page)
        let begin = scene?.reveal() ?? 0
        if begin > 0 { revealFeature(page, begin: begin) }
    }

    func islandWillClose() {
        isOpen = false
        hideFeature(page)
        setHover(nil, isPressed: false)
        pressed = nil
        pageHasPointer = false
        scene?.conceal()
        let timer = Timer(timeInterval: Self.releaseDelay + 1, repeats: false) { [unowned self] _ in
            MainActor.assumeIsolated {
                releaseTimer = nil
                scene?.root.removeFromSuperlayer()
                scene = nil
            }
        }
        timer.tolerance = 2
        RunLoop.main.add(timer, forMode: .common)
        releaseTimer = timer
    }

    // MARK: Switching

    func select(_ next: IslandPage) {
        guard isOpen, next != page else { return }
        let old = page
        hideFeature(old)
        page = next
        showFeature(next)
        scene?.show(next, from: old)
    }

    /// `delta` is horizontal scroll in points, positive toward the next page.
    func scrolled(by delta: Double, at time: Double) {
        guard isOpen, !home.isInteracting else { return }
        let step = swipe.add(delta, at: time)
        if step != 0, let next = page.neighbor(step) { select(next) }
    }

    private func showFeature(_ page: IslandPage) {
        switch page {
        case .home:
            home.islandWillOpen(layout: HomeLayout(size: size, headerHeight: headerHeight), scale: scale)
        case .usage:
            usage.pageWillShow(layout: UsageLayout(size: size, headerHeight: headerHeight), scale: scale)
        }
    }

    private func hideFeature(_ page: IslandPage) {
        switch page {
        case .home: home.islandWillClose()
        case .usage: usage.pageWillHide()
        }
    }

    private func revealFeature(_ page: IslandPage, begin: CFTimeInterval) {
        switch page {
        case .home: home.reveal(begin: begin)
        case .usage: usage.reveal(begin: begin)
        }
    }

    // MARK: Pointer

    private var headerLayout: HeaderLayout { HeaderLayout(size: size, headerHeight: headerHeight) }

    func pointerMoved(to point: CGPoint) {
        guard pressed == nil else { return }
        let control = headerLayout.control(at: point)
        if control != hovered { setHover(control, isPressed: false) }
        if page == .home { home.pointerMoved(to: point) }
    }

    func pointerDown(at point: CGPoint) {
        if let control = headerLayout.control(at: point) {
            pressed = control
            setHover(control, isPressed: true)
        } else {
            pageHasPointer = true
            if page == .home { home.pointerDown(at: point) }
        }
    }

    func pointerDragged(to point: CGPoint) {
        if pageHasPointer, page == .home { home.pointerDragged(to: point) }
    }

    func pointerUp(at point: CGPoint) {
        if let control = pressed {
            pressed = nil
            if headerLayout.control(at: point) == control { activate(control) }
            setHover(headerLayout.control(at: point), isPressed: false)
        } else if pageHasPointer {
            pageHasPointer = false
            if page == .home { home.pointerUp(at: point) }
        }
    }

    private func activate(_ control: HeaderControl) {
        switch control {
        case .tab(let next): select(next)
        case .settings: onOpenSettings?()
        }
    }

    private func setHover(_ control: HeaderControl?, isPressed: Bool) {
        hovered = control
        scene?.setHover(control, isPressed: isPressed)
    }
}
