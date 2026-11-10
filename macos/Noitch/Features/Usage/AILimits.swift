import Foundation

/// The two limits that matter, how fast they are being used, and what that means. Pure values in,
/// values out: the sources hand it numbers and the time. The Windows app uses the same rules
/// (ai_limits.h).

/// One usage window, such as five hours or a week, as the provider reports it.
nonisolated struct LimitWindow: Equatable, Sendable {
    static let fiveHours: Double = 5 * 3600
    static let week: Double = 7 * 86400

    /// 0 to 100, and can pass 100 when a provider allows overage.
    var usedPercent: Double
    /// Seconds since 1970, or nil when the provider gave none.
    var resetsAt: Double?
    var length: Double

    /// How much of the window's time has gone, from 0 to 1.
    func elapsed(at now: Double) -> Double? {
        guard let resetsAt else { return nil }
        return min(max(1 - (resetsAt - now) / length, 0), 1)
    }

    func remaining(at now: Double) -> Double? { resetsAt.map { max($0 - now, 0) } }

    /// A window whose reset has passed starts again from nothing.
    func current(at now: Double) -> LimitWindow {
        guard let resetsAt, resetsAt <= now else { return self }
        return LimitWindow(usedPercent: 0, resetsAt: nil, length: length)
    }

    /// Percent per second, averaged over the window so far. Too early in a window says nothing.
    func averageRate(at now: Double) -> Double? {
        guard let elapsed = elapsed(at: now), elapsed * length >= 600 else { return nil }
        return usedPercent / (elapsed * length)
    }
}

/// Whether usage is running ahead of the clock. Using 60% of a window that is 30% through is ahead:
/// the same pace would hit the limit before the reset.
nonisolated enum Pace: Equatable, Sendable {
    case under
    case even
    case ahead

    init?(_ window: LimitWindow, at now: Double) {
        guard let elapsed = window.elapsed(at: now), elapsed >= 0.05 else { return nil }
        let used = window.usedPercent / 100
        self = used > elapsed + 0.1 ? .ahead : used < elapsed - 0.1 ? .under : .even
    }
}

/// A snapshot of one provider's limits at a moment, from Codex's logs.
nonisolated struct LimitSample: Equatable, Sendable {
    var time: Double
    var fiveHour: LimitWindow?
    var week: LimitWindow?
}

/// Why a tool has no numbers, when it hasn't.
nonisolated enum LimitProblem: Equatable, Sendable {
    case loading
    case signedOut
    case expired
    case keychainDenied
    case unavailable
    case noData
}

nonisolated struct ToolLimits: Equatable, Sendable {
    var fiveHour: LimitWindow?
    var week: LimitWindow?
    /// Five-hour percent per second over the last half hour, when known.
    var recentRate: Double?
    var problem: LimitProblem?
    /// Set when these are remembered numbers rather than fresh ones: when they were current.
    var asOf: Double?

    static func failed(_ problem: LimitProblem) -> ToolLimits { ToolLimits(problem: problem) }
}

nonisolated enum Severity: Int, Comparable, Sendable {
    case calm
    case watch
    case critical

    static func < (a: Severity, b: Severity) -> Bool { a.rawValue < b.rawValue }
}

/// The one thing worth saying about a tool's limits right now, most urgent first.
nonisolated enum LimitInsight: Equatable, Sendable {
    case reached(window: Double, resetsIn: Double?)
    case runsOut(window: Double, in: Double, resetsIn: Double)
    case onTrack(projected: Double)
    case idle(used: Double)
    case problem(LimitProblem)

    var severity: Severity {
        switch self {
        case .reached: .critical
        case let .runsOut(window, time, _): window == LimitWindow.fiveHours && time < 1800 ? .critical : .watch
        case let .onTrack(projected): projected >= 85 ? .watch : .calm
        case .idle, .problem: .calm
        }
    }

    init(_ limits: ToolLimits, at now: Double) {
        if let problem = limits.problem {
            self = .problem(problem)
            return
        }
        let five = limits.fiveHour?.current(at: now)
        let week = limits.week?.current(at: now)
        for window in [five, week].compactMap({ $0 }) where window.usedPercent >= 100 {
            self = .reached(window: window.length, resetsIn: window.remaining(at: now))
            return
        }
        // The five-hour window moves fast, so its recent pace says more than its average.
        if let five, let remaining = five.remaining(at: now),
           let rate = limits.recentRate ?? five.averageRate(at: now), rate > 0 {
            let time = (100 - five.usedPercent) / rate
            if time < remaining {
                self = .runsOut(window: five.length, in: time, resetsIn: remaining)
                return
            }
        }
        if let week, let remaining = week.remaining(at: now), let rate = week.averageRate(at: now), rate > 0 {
            let time = (100 - week.usedPercent) / rate
            if time < remaining {
                self = .runsOut(window: week.length, in: time, resetsIn: remaining)
                return
            }
        }
        if let five, let remaining = five.remaining(at: now), let rate = limits.recentRate ?? five.averageRate(at: now) {
            self = .onTrack(projected: min(five.usedPercent + rate * remaining, 100))
            return
        }
        self = .idle(used: five?.usedPercent ?? week?.usedPercent ?? 0)
    }
}

nonisolated enum LimitRates {
    static let recentSpan: Double = 1800

    /// Codex's five-hour pace over the last half hour, from its own reported percentages. No samples
    /// in that time means no use, which is a pace of zero.
    static func codex(_ samples: [LimitSample], now: Double) -> Double? {
        guard let latest = samples.last, let window = latest.fiveHour, let resets = window.resetsAt, resets > now else { return nil }
        let start = now - recentSpan
        let windowStart = resets - window.length
        // The value at the start of the span: the last sample before it in the same window, or none
        // used yet if the window began inside the span.
        let before = samples.last { $0.time <= start && $0.fiveHour?.resetsAt == resets }?.fiveHour?.usedPercent
        guard let base = before ?? (windowStart >= start ? 0 : nil) else { return nil }
        let span = now - max(start, windowStart)
        guard span >= 300 else { return nil }
        return max(window.usedPercent - base, 0) / span
    }

    /// Claude's five-hour pace, from the share of the window's cost spent in the last half hour of
    /// local logs. The provider reports only the percentage, and cost tracks what the limit counts
    /// better than raw tokens do.
    static func claude(_ entries: [UsageEntry], window: LimitWindow, now: Double) -> Double? {
        guard let resets = window.resetsAt, resets > now else { return nil }
        let windowStart = resets - window.length
        var total = 0.0
        var recent = 0.0
        for entry in entries where entry.tool == .claude && entry.time >= windowStart && entry.time <= now {
            let weight = entry.cost ?? Double(entry.tokens) / 1_000_000
            total += weight
            if entry.time >= now - recentSpan { recent += weight }
        }
        let span = min(recentSpan, now - windowStart)
        guard total > 0, span >= 300 else { return nil }
        return window.usedPercent * (recent / total) / span
    }
}

/// Reads Anthropic's usage response and Claude Code's stored login. Kept pure, so the shapes are
/// tested without a network or a Keychain.
nonisolated enum ClaudeUsageFormat {
    static func limits(fromResponse bytes: ArraySlice<UInt8>) -> ToolLimits? {
        func window(_ key: String, length: Double) -> LimitWindow? {
            guard let object = JSONScan.value(key, in: bytes),
                  let used = JSONScan.value("utilization", in: object).flatMap(JSONScan.number) else { return nil }
            let resets = JSONScan.value("resets_at", in: object).flatMap { raw in
                JSONScan.string(raw).flatMap(UsageLog.parseTime) ?? JSONScan.number(raw)
            }
            return LimitWindow(usedPercent: used, resetsAt: resets, length: length)
        }
        let five = window("five_hour", length: LimitWindow.fiveHours)
        let week = window("seven_day", length: LimitWindow.week)
        guard five != nil || week != nil else { return nil }
        return ToolLimits(fiveHour: five, week: week)
    }

    struct Login: Equatable {
        var accessToken: String
        /// Seconds since 1970.
        var expiresAt: Double?
    }

    static func login(fromCredentials bytes: ArraySlice<UInt8>) -> Login? {
        guard let oauth = JSONScan.value("claudeAiOauth", in: bytes),
              let token = JSONScan.value("accessToken", in: oauth).flatMap(JSONScan.string), !token.isEmpty else { return nil }
        let expires = JSONScan.value("expiresAt", in: oauth).flatMap(JSONScan.number).map { $0 / 1000 }
        return Login(accessToken: token, expiresAt: expires)
    }
}

/// The last good limits per tool, kept on disk so the page isn't empty while offline or after the
/// logs that held them have aged out. One line per tool; the Windows app writes the same format.
nonisolated enum LimitCache {
    /// "claude 1794322800 72 1794330000 18000 41 - 604800": the time they were current, then for
    /// five hours and the week the percent, the reset or "-", and the length. A missing window is "- - -".
    static func encode(_ tool: String, _ limits: ToolLimits, asOf: Double) -> String {
        func window(_ window: LimitWindow?) -> String {
            guard let window else { return "- - -" }
            return "\(window.usedPercent) \(window.resetsAt.map { String($0) } ?? "-") \(window.length)"
        }
        return "\(tool) \(asOf) \(window(limits.fiveHour)) \(window(limits.week))"
    }

    static func decode(_ text: String, tool: String) -> ToolLimits? {
        for line in text.split(separator: "\n") {
            let parts = line.split(separator: " ").map(String.init)
            guard parts.count == 8, parts[0] == tool, let asOf = Double(parts[1]) else { continue }
            func window(_ at: Int) -> LimitWindow? {
                guard let used = Double(parts[at]), let length = Double(parts[at + 2]), length > 0 else { return nil }
                return LimitWindow(usedPercent: used, resetsAt: Double(parts[at + 1]), length: length)
            }
            let limits = ToolLimits(fiveHour: window(2), week: window(5), asOf: asOf)
            return limits.fiveHour == nil && limits.week == nil ? nil : limits
        }
        return nil
    }
}
