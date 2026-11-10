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
    private let claudeSource = ClaudeLimitSource()
    private var claudeLimits = ToolLimits.failed(.loading)
    private var log = UsageLog()
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
        claudeSource.cancel()
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
        log = UsageLog()
        claudeLimits = .failed(.loading)
    }

    private func refresh() {
        guard scene != nil else { return }
        let date = Date()
        let now = date.timeIntervalSince1970
        let source = source ?? UsageSource()
        self.source = source
        log = source.refresh(now: now)
        render()
        claudeSource.fetch(now: now) { [weak self] limits in
            guard let self else { return }
            claudeLimits = limits
            render()
        }
        scheduleTick(after: date)
    }

    /// Combines the latest limits with the local logs. Cheap: no reading, no request.
    private func render() {
        guard let scene else { return }
        let date = Date()
        let now = date.timeIntervalSince1970
        var claude = claudeLimits
        if let five = claude.fiveHour { claude.recentRate = LimitRates.claude(log.entries, window: five, now: now) }
        let codex: ToolLimits
        if let latest = log.codexSamples.last {
            codex = ToolLimits(fiveHour: latest.fiveHour, week: latest.week, recentRate: LimitRates.codex(log.codexSamples, now: now))
        } else {
            codex = .failed(.noData)
        }
        let dayStart = Calendar.current.startOfDay(for: date).timeIntervalSince1970
        let summary = UsageSummary(log: log, now: now, dayStart: dayStart)
        scene.apply(UsageModel(claude: claude, codex: codex, summary: summary, now: now) { time in
            Self.clock.string(from: Date(timeIntervalSince1970: time))
        })
    }

    private static let clock: DateFormatter = {
        let formatter = DateFormatter()
        formatter.setLocalizedDateFormatFromTemplate("EEE HH:mm")
        return formatter
    }()

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
