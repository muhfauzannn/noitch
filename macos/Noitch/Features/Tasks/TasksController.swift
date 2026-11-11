import AppKit

/// Owns the tasks page while it shows. The list itself lives in `TaskWatch`, which runs whether or
/// not the page is open; this only draws it, keeps the countdown current and handles the pointer.
final class TasksController {
    private static let releaseDelay: TimeInterval = 10

    var onSceneCreated: ((CALayer) -> Void)?
    var onOpenSettings: (() -> Void)?
    var sceneRoot: CALayer? { scene?.root }

    private let watch: TaskWatch
    private var layout: TasksLayout?
    private var scale: CGFloat = 2
    private var scene: TasksScene?
    private var isShown = false
    private var offset = 0
    /// Overdue work shows only after the button asks for it, and hides again on the next showing.
    private var showOverdue = false
    private var scrolled: CGFloat = 0
    /// Highlighted for this showing; the watch forgets them as soon as the page appears.
    private var newOnShow: Set<Int> = []
    private var tickTimer: Timer?
    private var releaseTimer: Timer?
    private var pressed: Target?
    private var model: TasksPageModel?

    private enum Target: Equatable {
        case row(Int)
        case open
        case message
        case overdue
    }

    init(watch: TaskWatch) {
        self.watch = watch
    }

    func pageWillShow(layout: TasksLayout, scale: CGFloat) {
        releaseTimer?.invalidate()
        releaseTimer = nil
        isShown = true
        offset = 0
        showOverdue = false
        newOnShow = watch.unviewed
        if layout != self.layout || scale != self.scale || scene == nil {
            scene?.root.removeFromSuperlayer()
            self.layout = layout
            self.scale = scale
            let scene = TasksScene(layout: layout, scale: scale)
            self.scene = scene
            onSceneCreated?(scene.root)
        }
        render()
        watch.markViewed()
        watch.pageWillShow()
    }

    func pageWillHide() {
        isShown = false
        tickTimer?.invalidate()
        tickTimer = nil
        pressed = nil
        scene?.setHover(row: nil)
        guard releaseTimer == nil else { return }
        let timer = Timer(timeInterval: Self.releaseDelay, repeats: false) { [unowned self] _ in
            MainActor.assumeIsolated {
                releaseTimer = nil
                scene?.root.removeFromSuperlayer()
                scene = nil
                model = nil
            }
        }
        timer.tolerance = 2
        RunLoop.main.add(timer, forMode: .common)
        releaseTimer = timer
    }

    func reveal(begin: CFTimeInterval) { scene?.reveal(begin: begin) }

    /// The watch's list changed. Tasks announced while the page shows count as seen at once.
    func watchChanged() {
        guard isShown else { return }
        newOnShow.formUnion(watch.unviewed)
        watch.markViewed()
        render()
    }

    private func render() {
        guard let scene, isShown else { return }
        let date = Date()
        let now = date.timeIntervalSince1970
        let model = TasksPageModel.make(tasks: watch.tasks, unviewed: newOnShow, signedIn: watch.settings.isSignedIn, checking: watch.isChecking,
                                        problem: watch.problem, asOf: watch.record?.asOf, now: now, offset: offset, calendar: .current,
                                        showOverdue: showOverdue)
        self.model = model
        scene.apply(model)
        scheduleTick(after: now)
    }

    /// One shot at the next minute while shown: the countdown and due times are in minutes.
    private func scheduleTick(after now: Double) {
        tickTimer?.invalidate()
        let delay = 60 - now.truncatingRemainder(dividingBy: 60) + 0.2
        let timer = Timer(timeInterval: delay, repeats: false) { [unowned self] _ in
            MainActor.assumeIsolated { render() }
        }
        timer.tolerance = 2
        RunLoop.main.add(timer, forMode: .common)
        tickTimer = timer
    }

    // MARK: Pointer

    /// `delta` is vertical scroll in points; a row's height moves the list one row.
    func scrolledVertically(by delta: CGFloat) {
        guard isShown, let model, case let .list(_, _, _, more) = model.content, !more.isEmpty else { return }
        scrolled += delta
        let step = Int(scrolled / TasksLayout.rowHeight)
        guard step != 0 else { return }
        scrolled -= CGFloat(step) * TasksLayout.rowHeight
        let next = min(max(offset - step, 0), max(model.listCount - TaskRule.visibleRows, 0))
        guard next != offset else { return }
        offset = next
        render()
    }

    func pointerMoved(to point: CGPoint) {
        guard case let .row(index)? = target(at: point) else {
            scene?.setHover(row: nil)
            return
        }
        scene?.setHover(row: index)
    }

    func pointerDown(at point: CGPoint) {
        pressed = target(at: point)
    }

    func pointerUp(at point: CGPoint) {
        defer { pressed = nil }
        guard let pressed, target(at: point) == pressed, let model else { return }
        switch (pressed, model.content) {
        case let (.row(index), .list(_, _, rows, _)) where index < rows.count:
            open(rows[index].url)
        case let (.open, .list(hero, _, _, _)):
            open(hero.url)
        case (.message, .message(_, _, .some, _)) where model.buttonAction == .openSettings:
            onOpenSettings?()
        case (.message, .message(_, _, .some, _)), (.overdue, .list):
            toggleOverdue()
        default:
            break
        }
    }

    private func toggleOverdue() {
        showOverdue.toggle()
        // Overdue work sorts first, so the list starts over from the top.
        offset = 0
        render()
    }

    private func target(at point: CGPoint) -> Target? {
        guard let layout, let model else { return nil }
        switch model.content {
        case let .list(_, _, rows, _):
            if layout.openButton.insetBy(dx: -4, dy: -4).contains(point) { return .open }
            if !model.overdueToggle.isEmpty, layout.overdueToggle.insetBy(dx: -4, dy: -6).contains(point) { return .overdue }
            return rows.indices.first { layout.row($0).contains(point) }.map(Target.row)
        case let .message(_, _, button, _):
            return button != nil && layout.messageButton.insetBy(dx: -4, dy: -4).contains(point) ? .message : nil
        }
    }

    /// Only pages on the signed-in site open, so a task's link can't send the browser elsewhere.
    private func open(_ link: String) {
        guard let url = URL(string: link), url.scheme == "https",
              url.host == URL(string: watch.settings.site)?.host else { return }
        NSWorkspace.shared.open(url)
    }
}
