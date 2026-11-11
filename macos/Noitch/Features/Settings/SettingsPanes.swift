import AppKit
import ServiceManagement

/// The pages of the settings window that belong to the app rather than one feature.
enum SettingsPanes {
    static let general = SettingsPane(title: "General", glyph: .icon(.settings), color: .systemGray) { form in
        form.section("Startup") {
            launchAtLoginRow(in: form)
        }
        form.section("Opening", footer: "A click on the notch always opens the island.") {
            form.toggle("Open on hover", caption: "When off, click the notch to open it.", \.general.openOnHover)
            form.choice("Hover delay", caption: "How long the pointer rests in the notch first.", \.general.hoverDelay,
                        options: GeneralSettings.hoverDelays.map { ($0 == 0 ? "None" : seconds($0), $0) },
                        enabledBy: \.general.openOnHover)
            form.toggle("Haptic feedback", caption: "A light tap when the island opens, on a Force Touch trackpad.", \.general.haptics)
        }
        form.section("Display") {
            form.choice("Show the island on", \.general.display, options: DisplayChoice.allCases.map { ($0.title, $0) })
        }
    }

    static let pages = SettingsPane(title: "Pages", glyph: .pages, color: .systemIndigo) { form in
        form.section("Pages", footer: "Home is always there. Swipe sideways in the island to move between pages.") {
            form.toggle("AI Usage", caption: "Claude Code and Codex limits.", \.pages.showUsage)
            form.toggle("Tasks", caption: "What's still due on your Moodle site, such as Scele.", \.pages.showTasks)
        }
        form.section("Opening") {
            form.choice("Open on", \.pages.openOn, options: OpenPage.allCases.map { ($0.title, $0) })
        }
    }

    static let about = SettingsPane(title: "About", glyph: .about, color: .systemBlue) { form in
        let version = Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? ""
        form.section {
            form.row("Version", control: NSTextField(labelWithString: version))
        }
        form.note("Now playing uses MediaRemoteAdapter, © Jonas van den Berg and contributors, BSD 3-Clause. Lyrics come from LRCLIB.")
    }

    /// "0.15 s", or "+0.5 s" and "−0.5 s" when `signed`.
    static func seconds(_ value: Double, signed: Bool = false) -> String {
        let text = abs(value).formatted(.number.precision(.fractionLength(0...2)))
        let sign = !signed || value == 0 ? "" : value > 0 ? "+" : "−"
        return "\(sign)\(text) s"
    }

    /// Backed by the system, not by `AppSettings`: the login item is the truth.
    private static func launchAtLoginRow(in form: SettingsForm) {
        let control = NSSwitch()
        control.controlSize = .small
        control.state = SMAppService.mainApp.status == .enabled ? .on : .off
        guard let status = form.row("Launch at login", caption: "", control: control) else { return }
        status.isHidden = true
        form.bind(control) { sender in
            guard let control = sender as? NSSwitch else { return }
            do {
                if control.state == .on {
                    try SMAppService.mainApp.register()
                } else {
                    try SMAppService.mainApp.unregister()
                }
                status.isHidden = true
            } catch {
                control.state = SMAppService.mainApp.status == .enabled ? .on : .off
                status.stringValue = "Couldn't change this: \(error.localizedDescription)"
                status.isHidden = false
            }
        }
    }
}
