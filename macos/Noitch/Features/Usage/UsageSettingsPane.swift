import AppKit

/// The usage page's page in the settings window.
enum UsageSettingsPane {
    static let pane = SettingsPane(title: "AI Usage", glyph: .icon(.usage), color: .systemOrange) { form in
        form.section("Tools", footer: "Claude Code's limits are asked of Anthropic with its own login. Codex's come from its local logs.") {
            form.toggle("Claude Code", \.usage.showClaude, enabledBy: \.pages.showUsage)
            form.toggle("Codex", \.usage.showCodex, enabledBy: \.pages.showUsage)
        }
        form.section("Limits") {
            form.choice("Warn at", caption: "A ring this full turns amber; red starts at 90%.", \.usage.warnAt,
                        options: UsageSettings.warnLevels.map { ("\(Int($0))%", $0) }, enabledBy: \.pages.showUsage)
            form.choice("Check Claude every", caption: "Only while the page is on screen.", \.usage.refreshMinutes,
                        options: UsageSettings.refreshChoices.map { ($0 == 1 ? "Minute" : "\(Int($0)) minutes", $0) },
                        enabledBy: \.pages.showUsage)
        }
    }
}
