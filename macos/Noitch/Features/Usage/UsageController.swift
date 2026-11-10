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
    /// The last good numbers per tool, from this run or the cache file, with when they were current.
    private var remembered: [String: ToolLimits]?
    private var log = UsageLog()
    private var tickTimer: Timer?
    private var releaseTimer: Timer?

    func pageWillShow(layout: UsageLayout, scale: CGFloat) {
        releaseTimer?.invalidate()
        releaseTimer = nil
        // Show what was last known at once, rather than an empty page while asking.
        if claudeLimits.problem == .loading, let known = rememberedLimits()["claude"] { claudeLimits = known }
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
        remembered = nil
    }

    private static let cacheFile = FileManager.default
        .urls(for: .cachesDirectory, in: .userDomainMask).first?
        .appendingPathComponent("Noitch/limits", isDirectory: false)

    private func rememberedLimits() -> [String: ToolLimits] {
        if let remembered { return remembered }
        let text = Self.cacheFile.flatMap { try? String(contentsOf: $0, encoding: .utf8) } ?? ""
        var found: [String: ToolLimits] = [:]
        for tool in ["claude", "codex"] { found[tool] = LimitCache.decode(text, tool: tool) }
        remembered = found
        return found
    }

    /// Written only when the numbers change or have aged, and only while the page is shown.
    private func remember(_ tool: String, _ limits: ToolLimits, asOf: Double) {
        var all = rememberedLimits()
        var stored = limits
        stored.asOf = asOf
        stored.recentRate = nil
        // Same numbers confirmed again only move the "as of" time, so that waits ten minutes.
        guard all[tool].map({ $0.fiveHour != stored.fiveHour || $0.week != stored.week || asOf - ($0.asOf ?? 0) > 600 }) ?? true else { return }
        all[tool] = stored
        remembered = all
        guard let file = Self.cacheFile else { return }
        let text = all.compactMap { tool, limits in limits.asOf.map { LimitCache.encode(tool, limits, asOf: $0) } }.sorted().joined(separator: "\n")
        try? FileManager.default.createDirectory(at: file.deletingLastPathComponent(), withIntermediateDirectories: true)
        try? text.write(to: file, atomically: true, encoding: .utf8)
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
            let now = Date().timeIntervalSince1970
            if limits.problem == nil {
                claudeLimits = limits
                remember("claude", limits, asOf: now)
            } else if limits.problem == .unavailable || limits.problem == .expired, var known = rememberedLimits()["claude"] {
                // Offline, refused or logged out for now: the last good numbers beat a blank page. An
                // expired login still says so, because only the user can renew it.
                if limits.problem == .expired { known.problem = .expired }
                claudeLimits = known
            } else {
                claudeLimits = limits
            }
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
            remember("codex", codex, asOf: latest.time)
        } else {
            // The logs that held them are over a day old; the week may still be running.
            codex = rememberedLimits()["codex"] ?? .failed(.noData)
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
