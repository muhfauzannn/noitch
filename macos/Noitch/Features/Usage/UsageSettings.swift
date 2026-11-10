import Foundation

/// The usage page's preferences.
struct UsageSettings: Equatable {
    var showClaude = true
    var showCodex = true
    /// A window this full or more turns amber. Red stays at 90%, or above this when it is higher.
    var warnAt = 70.0
    /// Minutes between requests for Claude's limits while the page shows. Never under one, the
    /// approved rate; the local logs are still read every minute.
    var refreshMinutes = 1.0

    static let warnLevels = [50.0, 60, 70, 80]
    static let refreshChoices = [1.0, 5, 15]

    var criticalAt: Double { max(90, warnAt + 5) }

    /// The tools shown, in column order. Never empty: with both turned off, Claude stays.
    var tools: [UsageTool] {
        let shown = [showClaude ? UsageTool.claude : nil, showCodex ? .codex : nil].compactMap { $0 }
        return shown.isEmpty ? [.claude] : shown
    }

    static func load(_ store: SettingsStore) -> UsageSettings {
        var settings = UsageSettings()
        store.read("usageShowClaude", into: &settings.showClaude)
        store.read("usageShowCodex", into: &settings.showCodex)
        store.read("usageWarnAt", into: &settings.warnAt)
        store.read("usageRefreshMinutes", into: &settings.refreshMinutes)
        settings.warnAt = min(max(settings.warnAt, 30), 95)
        settings.refreshMinutes = min(max(settings.refreshMinutes, 1), 60)
        return settings
    }

    func save(_ store: SettingsStore) {
        store.write("usageShowClaude", showClaude)
        store.write("usageShowCodex", showCodex)
        store.write("usageWarnAt", warnAt)
        store.write("usageRefreshMinutes", refreshMinutes)
    }
}

enum UsageTool: Int, CaseIterable {
    case claude
    case codex
}
