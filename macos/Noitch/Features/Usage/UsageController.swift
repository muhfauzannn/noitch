import AppKit

/// Owns what the usage page knows. The pager tells it when the page is shown and hidden; while
/// hidden, nothing is read or scheduled, and after a grace period nothing is kept.
final class UsageController {
    private static let releaseDelay: TimeInterval = 10

    var onSceneCreated: ((CALayer) -> Void)?
    var sceneRoot: CALayer? { scene?.root }

    private var layout: UsageLayout?
    private var scale: CGFloat = 2
    private var scene: UsageScene?
    private var source: UsageSource?
    private var tickTimer: Timer?
    private var releaseTimer: Timer?

    func pageWillShow(layout: UsageLayout, scale: CGFloat) {
        releaseTimer?.invalidate()
        releaseTimer = nil
        if scene == nil || layout != self.layout || scale != self.scale {
            scene?.root.removeFromSuperlayer()
            let scene = UsageScene(layout: layout, scale: scale)
            self.scene = scene
            self.layout = layout
            self.scale = scale
            onSceneCreated?(scene.root)
        }
        refresh()
    }

    func pageWillHide() {
        tickTimer?.invalidate()
        tickTimer = nil
        guard releaseTimer == nil else { return }
        let timer = Timer(timeInterval: Self.releaseDelay, repeats: false) { [unowned self] _ in
            MainActor.assumeIsolated { release() }
        }
        timer.tolerance = 2
        RunLoop.main.add(timer, forMode: .common)
        releaseTimer = timer
    }

    func reveal(begin: CFTimeInterval) { scene?.reveal(begin: begin) }

    private func release() {
        releaseTimer = nil
        scene?.root.removeFromSuperlayer()
        scene = nil
        source = nil
    }

    private func refresh() {
        guard let scene else { return }
        let date = Date()
        let now = date.timeIntervalSince1970
        let source = source ?? UsageSource()
        self.source = source
        let log = source.refresh(now: now)
        let dayStart = Calendar.current.startOfDay(for: date).timeIntervalSince1970
        scene.apply(UsageModel(summary: UsageSummary(log: log, now: now, dayStart: dayStart), now: now))
        scheduleTick(after: date)
    }

    /// One shot at the next minute, only while shown: the countdowns are in minutes, and new log
    /// lines are picked up on the same look.
    private func scheduleTick(after date: Date) {
        tickTimer?.invalidate()
        let seconds = date.timeIntervalSince1970
        let delay = 60 - seconds.truncatingRemainder(dividingBy: 60) + 0.5
        let timer = Timer(timeInterval: delay, repeats: false) { [unowned self] _ in
            MainActor.assumeIsolated { refresh() }
        }
        timer.tolerance = 5
        RunLoop.main.add(timer, forMode: .common)
        tickTimer = timer
    }
}
