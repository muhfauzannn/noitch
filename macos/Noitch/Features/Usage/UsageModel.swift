import Foundation

/// What the usage page shows, as text. Pure, so the wording is tested; the Windows app matches it.
struct UsageModel: Equatable {
    struct Row: Equatable {
        var name: String
        var value: String
    }

    struct Column: Equatable {
        var title: String
        var tokens: String
        var cost: String
        var limit: String
        /// How full the five-hour window is, from 0 to 1, or nil when unknown.
        var limitFraction: Double?
        var rows: [Row]
    }

    var claude: Column
    var codex: Column

    init(summary: UsageSummary, now: Double) {
        claude = Self.column(title: "Claude Code · today", usage: summary.claudeToday)
        if let block = summary.claudeBlock {
            let usage = block.usage
            claude.limit = "5h block · \(Self.tokens(usage.tokens)) · \(Self.cost(usage)) · resets in \(Self.duration(block.end - now))"
            claude.limitFraction = min(max((now - block.start) / UsageLog.blockLength, 0), 1)
        } else {
            claude.limit = "No active 5h block"
        }
        codex = Self.column(title: "Codex · today", usage: summary.codexToday)
        if let limit = summary.codexLimit {
            codex.limit = "5h limit \(Int(limit.usedPercent.rounded()))% · resets in \(Self.duration(limit.resetsAt - now))"
            codex.limitFraction = min(max(limit.usedPercent / 100, 0), 1)
        } else {
            codex.limit = "5h limit not reported yet"
        }
    }

    static let maxRows = 3

    private static func column(title: String, usage: ToolUsage) -> Column {
        let rows = usage.models.prefix(maxRows).map { model in
            let cost = model.cost.map { " · \(dollars($0))" } ?? ""
            return Row(name: displayName(model.name), value: tokens(model.tokens) + cost)
        }
        return Column(title: title, tokens: tokens(usage.tokens), cost: cost(usage), limit: "", limitFraction: nil,
                      rows: rows.isEmpty ? [Row(name: "No usage yet", value: "")] : rows)
    }

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
        return minutes >= 60 ? "\(minutes / 60)h \(minutes % 60)m" : "\(minutes)m"
    }

    /// "claude-opus-5-5-20260101" reads as "Opus 5.5". Other tools' names are already short.
    static func displayName(_ model: String) -> String {
        guard model.hasPrefix("claude-") else { return model }
        var parts = model.dropFirst("claude-".count).split(separator: "-").map(String.init)
        if let last = parts.last, last.count == 8, last.allSatisfy(\.isNumber) { parts.removeLast() }
        guard let family = parts.first else { return model }
        let version = parts.dropFirst().joined(separator: ".")
        return family.prefix(1).uppercased() + family.dropFirst() + (version.isEmpty ? "" : " " + version)
    }

    private static func trimmed(_ value: Double) -> String {
        let text = String(format: value >= 100 ? "%.0f" : "%.1f", value)
        return text.hasSuffix(".0") ? String(text.dropLast(2)) : text
    }
}
