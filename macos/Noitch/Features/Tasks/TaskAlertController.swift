import AppKit

/// Owns the closed island's task alert: the slip for a few seconds when a task appears or a
/// deadline nears, then a dot until the tasks page is seen. Driven by the watch's events and one
/// one-shot timer for the slip's end; nothing runs while the dot shows.
final class TaskAlertController {
    /// Set by the island, which resizes the pill to `shape`.
    var onShapeChange: (() -> Void)?
    var onSceneCreated: ((CALayer) -> Void)?

    private(set) var shape = TaskAlertShape.none
    private var enabled: Bool
    private var layout: TaskAlertLayout?
    private var scale: CGFloat = 2
    private var scene: TaskAlertScene?
    private var slip: TaskAlertScene.Slip?
    /// A reminder that came while another slip showed, or while the island was open.
    private var waiting: TaskAlertScene.Slip?
    private var hasUnviewed = false
    private var isIslandOpen = false
    private var liveShown = false
    private var slipTimer: Timer?
    private var releaseTimer: Timer?

    init(enabled: Bool) {
        self.enabled = enabled
    }

    func setEnabled(_ enabled: Bool) {
        guard enabled != self.enabled else { return }
        self.enabled = enabled
        if !enabled {
            slip = nil
            waiting = nil
            slipTimer?.invalidate()
            slipTimer = nil
        }
        evaluate()
    }

    func setCollapsedSize(_ size: CGSize, scale: CGFloat) {
        let layout = TaskAlertLayout(collapsed: size)
        guard layout != self.layout || scale != self.scale else { return }
        self.layout = layout
        self.scale = scale
        scene?.root.removeFromSuperlayer()
        scene = nil
        evaluate(rebuild: true)
    }

    /// Whether the music's wings show, so the slip leaves them their artwork and bars.
    func setLiveShown(_ shown: Bool) {
        guard shown != liveShown else { return }
        liveShown = shown
        evaluate(rebuild: true)
    }

    func setUnviewed(_ unviewed: Bool) {
        guard unviewed != hasUnviewed else { return }
        hasUnviewed = unviewed
        evaluate()
    }

    func announce(_ tasks: [MoodleTask]) {
        // The page shows them anyway; the dot waits for the island to close.
        guard enabled, !isIslandOpen, let first = tasks.first else { return }
        let courses = Array(NSOrderedSet(array: tasks.map(\.course))) as? [String] ?? []
        let content = tasks.count == 1
            ? TaskAlertScene.Slip(module: first.module, count: "+1", lead: "New", text: "\(first.course) · \(first.title)",
                                  detail: TaskRule.dueText(first.due, now: Date().timeIntervalSince1970, calendar: .current), urgent: false)
            : TaskAlertScene.Slip(module: first.module, count: "+\(tasks.count)", lead: "\(tasks.count) new tasks",
                                  text: courses.prefix(3).joined(separator: ", "), detail: "", urgent: false)
        present(content)
    }

    func remind(_ task: MoodleTask, lead: Double) {
        guard enabled else { return }
        let content = TaskAlertScene.Slip(module: task.module, count: lead >= 86400 ? "1d" : "1h",
                                          lead: lead >= 86400 ? "Due tomorrow" : "Due in 1 hour", text: task.title, detail: task.course, urgent: true)
        if isIslandOpen || slip != nil {
            waiting = content
        } else {
            present(content)
        }
    }

    /// The tasks page is where the island should open while anything of this shows.
    var wantsTasksPage: Bool { shape != .none }

    func islandWillOpen() {
        isIslandOpen = true
        // Opening is the look the slip asked for; it doesn't come back afterwards.
        slip = nil
        slipTimer?.invalidate()
        slipTimer = nil
        scene?.setVisible(false)
        evaluate()
    }

    func islandWillClose() {
        isIslandOpen = false
        if let waiting {
            self.waiting = nil
            present(waiting)
        } else {
            evaluate()
        }
        // After the pill has narrowed most of the way back.
        scene?.setVisible(true, delay: 0.22)
    }

    private func present(_ content: TaskAlertScene.Slip) {
        slip = content
        slipTimer?.invalidate()
        let timer = Timer(timeInterval: TaskAlertLayout.slipDuration, repeats: false) { [unowned self] _ in
            MainActor.assumeIsolated { slipEnded() }
        }
        timer.tolerance = 0.3
        RunLoop.main.add(timer, forMode: .common)
        slipTimer = timer
        evaluate(rebuild: true)
    }

    private func slipEnded() {
        slipTimer = nil
        slip = nil
        if let waiting {
            self.waiting = nil
            present(waiting)
        } else {
            evaluate()
        }
    }

    private func evaluate(rebuild: Bool = false) {
        let target: TaskAlertShape = !enabled || layout == nil ? .none
            : slip != nil && !isIslandOpen ? .slip
            : hasUnviewed ? .dot : .none
        let changed = target != shape
        shape = target
        if target != .none {
            releaseTimer?.invalidate()
            releaseTimer = nil
            if scene == nil, let layout {
                let scene = TaskAlertScene(layout: layout, scale: scale)
                self.scene = scene
                onSceneCreated?(scene.root)
                scene.setVisible(!isIslandOpen)
            }
        }
        if changed || rebuild {
            switch target {
            case .slip: if let slip { scene?.showSlip(slip, wingsShown: !liveShown) }
            case .dot: scene?.showDot(true, beside: liveShown)
            case .none: scene?.showDot(false, beside: liveShown)
            }
        }
        if target == .none, scene != nil, releaseTimer == nil {
            let timer = Timer(timeInterval: 2, repeats: false) { [unowned self] _ in
                MainActor.assumeIsolated {
                    releaseTimer = nil
                    guard shape == .none else { return }
                    scene?.root.removeFromSuperlayer()
                    scene = nil
                }
            }
            timer.tolerance = 1
            RunLoop.main.add(timer, forMode: .common)
            releaseTimer = timer
        }
        if changed { onShapeChange?() }
    }
}
