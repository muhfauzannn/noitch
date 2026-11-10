import Foundation

/// What the usage page shows, as values and text. Pure, so the wording is tested; the Windows app
/// matches it (usage_model.h).
struct UsageModel: Equatable {
    /// One ring of the gauge: how full the window is, and where the clock is in it.
    struct Ring: Equatable {
        var fraction: Double
        /// How much of the window's time has passed, from 0 to 1. Fill past the tick is ahead of pace.
        var tick: Double?
        var severity: Severity
    }

    struct Row: Equatable {
        var label: String
        var value: String
        var pace: Pace?
        var reset: String
        var severity: Severity
    }

    struct Tool: Equatable {
        var name: String
        var today: String
        var center: String
        var caption: String
        /// Five hours inside, the week outside.
        var inner: Ring?
        var outer: Ring?
        var rows: [Row]
        var headline: String
        var detail: String
        var severity: Severity
    }

    var claude: Tool
    var codex: Tool

    /// `clock` turns a time into a short local label such as "Thu 14:00".
    init(claude: ToolLimits, codex: ToolLimits, summary: UsageSummary, now: Double, clock: (Double) -> String) {
        self.claude = Self.tool("Claude Code", limits: claude, today: summary.claudeToday, now: now, clock: clock)
        self.codex = Self.tool("Codex", limits: codex, today: summary.codexToday, now: now, clock: clock)
    }

    private static func tool(_ name: String, limits: ToolLimits, today: ToolUsage, now: Double, clock: (Double) -> String) -> Tool {
        let insight = LimitInsight(limits, at: now)
        let five = limits.fiveHour?.current(at: now)
        let week = limits.week?.current(at: now)
        let (headline, detail) = text(insight, now: now, clock: clock)
        func severity(of window: LimitWindow) -> Severity {
            var level: Severity = window.usedPercent >= 90 ? .critical : window.usedPercent >= 70 ? .watch : .calm
            if Pace(window, at: now) == .ahead { level = max(level, .watch) }
            switch insight {
            case let .reached(length, _) where length == window.length, let .runsOut(length, _, _) where length == window.length:
                level = max(level, insight.severity)
            default:
                break
            }
            return level
        }
        func ring(_ window: LimitWindow?) -> Ring? {
            window.map { Ring(fraction: min(max($0.usedPercent / 100, 0), 1), tick: $0.elapsed(at: now), severity: severity(of: $0)) }
        }
        func row(_ label: String, _ window: LimitWindow?, resetText: (Double) -> String) -> Row {
            guard let window else { return Row(label: label, value: "—", pace: nil, reset: "", severity: .calm) }
            let reset = window.remaining(at: now).map(resetText) ?? (limits.problem == nil ? "reset" : "")
            return Row(label: label, value: percent(window.usedPercent), pace: Pace(window, at: now), reset: reset, severity: severity(of: window))
        }
        let rows = [
            row("5h", five) { duration($0) },
            row("Week", week) { clock(now + $0) },
        ]
        let center = five ?? week
        return Tool(
            name: name,
            today: today.tokens > 0 ? "Today \(tokens(today.tokens)) · \(cost(today))" : "Nothing today",
            center: center.map { percent($0.usedPercent) } ?? "—",
            caption: five != nil ? "5h" : week != nil ? "week" : "",
            inner: ring(five),
            outer: ring(week),
            rows: rows,
            headline: headline,
            detail: detail,
            severity: insight.severity
        )
    }

    static func text(_ insight: LimitInsight, now: Double, clock: (Double) -> String) -> (String, String) {
        func name(_ length: Double) -> String { length == LimitWindow.fiveHours ? "5h" : "Weekly" }
        func reset(_ length: Double, _ seconds: Double) -> String {
            length == LimitWindow.fiveHours ? "in \(duration(seconds))" : clock(now + seconds)
        }
        switch insight {
        case let .reached(length, resetsIn):
            return ("\(name(length)) limit reached", resetsIn.map { "Back \(reset(length, $0))" } ?? "Waiting for the reset")
        case let .runsOut(length, time, resetsIn):
            // How early it runs out is the part worth acting on: that long with nothing left.
            let early = "\(duration(resetsIn - time)) before its reset"
            if length == LimitWindow.fiveHours { return ("5h runs out in ~\(duration(time))", early) }
            return ("Week runs out ~\(clock(now + time))", early)
        case let .onTrack(projected):
            let headline = projected < 50 ? "Plenty of room" : projected < 85 ? "On track" : "Cutting it close"
            return (headline, "~\(percent(projected)) of 5h by its reset")
        case let .idle(used):
            return ("Quiet", used > 0 ? "\(percent(used)) used so far" : "Nothing used in this window")
        case let .problem(problem):
            switch problem {
            case .loading: return ("Checking limits…", "Asking Anthropic")
            case .signedOut: return ("Not signed in", "Sign in to Claude Code")
            case .expired: return ("Login expired", "Open Claude Code to renew")
            case .keychainDenied: return ("No Keychain access", "Reopen Noitch to retry")
            case .unavailable: return ("Limits unavailable", "Trying again in a minute")
            case .noData: return ("No limits yet", "Use it once to see them")
            }
        }
    }

    static func percent(_ value: Double) -> String { "\(Int(value.rounded()))%" }

    static func tokens(_ count: Int) -> String {
        let value = Double(count)
        switch count {
        case ..<1000: return "\(count)"
        case ..<1_000_000: return trimmed(value / 1000) + "K"
        case ..<1_000_000_000: return trimmed(value / 1_000_000) + "M"
        default: return trimmed(value / 1_000_000_000) + "B"
        }
    }

    /// A lower bound when some models have no price, and a dash when none do.
    static func cost(_ usage: ToolUsage) -> String {
        if usage.tokens > 0, usage.unpricedTokens == usage.tokens { return "—" }
        return dollars(usage.cost) + (usage.unpricedTokens > 0 ? "+" : "")
    }

    static func dollars(_ value: Double) -> String {
        String(format: value >= 100 ? "$%.0f" : "$%.2f", value)
    }

    static func duration(_ seconds: Double) -> String {
        let minutes = max(Int((seconds / 60).rounded(.up)), 0)
        if minutes >= 24 * 60 { return "\(minutes / 1440)d \(minutes % 1440 / 60)h" }
        return minutes >= 60 ? "\(minutes / 60)h \(minutes % 60)m" : "\(minutes)m"
    }

    private static func trimmed(_ value: Double) -> String {
        let text = String(format: value >= 100 ? "%.0f" : "%.1f", value)
        return text.hasSuffix(".0") ? String(text.dropLast(2)) : text
    }
}
