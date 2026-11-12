import AppKit
import Carbon.HIToolbox
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
        form.section("Pin", footer: "Pinned, the island stays open when the pointer leaves. Pin it from the header, or from anywhere with the shortcut.") {
            pinShortcutRow(in: form)
        }
    }

    /// The shortcut is recorded by pressing it, as in System Settings.
    private static func pinShortcutRow(in form: SettingsForm) {
        let recorder = ShortcutRecorder(shortcut: form.settings.general.pinShortcut)
        guard let caption = form.row("Pin shortcut", caption: "Click, then press the keys. Delete turns it off.", control: recorder) else { return }
        recorder.onChange = { [weak form] shortcut in
            guard let form else { return }
            form.update { $0.general.pinShortcut = shortcut }
            caption.stringValue = HotKey.refused ? "macOS refused it: another app may use these keys." : "Click, then press the keys. Delete turns it off."
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

/// A button that shows a shortcut and, once clicked, takes the next key press as the new one. Escape
/// cancels, Delete clears. A press without ⌘, ⌥ or ⌃ is ignored: it would steal typing.
final class ShortcutRecorder: NSButton {
    var onChange: ((Shortcut?) -> Void)?
    private var shortcut: Shortcut?
    private var isRecording = false {
        didSet { refresh() }
    }

    init(shortcut: Shortcut?) {
        self.shortcut = shortcut
        super.init(frame: .zero)
        bezelStyle = .push
        controlSize = .small
        font = .systemFont(ofSize: NSFont.smallSystemFontSize)
        target = self
        action = #selector(startRecording)
        widthAnchor.constraint(greaterThanOrEqualToConstant: 120).isActive = true
        refresh()
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) {
        fatalError("Created in code")
    }

    override var acceptsFirstResponder: Bool { true }

    @objc private func startRecording() {
        isRecording = true
        window?.makeFirstResponder(self)
    }

    override func resignFirstResponder() -> Bool {
        isRecording = false
        return super.resignFirstResponder()
    }

    // ⌘ combinations arrive here before keyDown.
    override func performKeyEquivalent(with event: NSEvent) -> Bool {
        guard isRecording else { return super.performKeyEquivalent(with: event) }
        record(event)
        return true
    }

    override func keyDown(with event: NSEvent) {
        guard isRecording else { return super.keyDown(with: event) }
        record(event)
    }

    private func record(_ event: NSEvent) {
        let key = Int(event.keyCode)
        if key == kVK_Escape, event.modifierFlags.intersection([.command, .option, .control]).isEmpty {
            isRecording = false
            return
        }
        if key == kVK_Delete || key == kVK_ForwardDelete {
            finish(nil)
            return
        }
        let pressed = Shortcut(event: event)
        guard pressed.isUsable else {
            NSSound.beep()
            return
        }
        finish(pressed)
    }

    private func finish(_ value: Shortcut?) {
        shortcut = value
        isRecording = false
        window?.makeFirstResponder(nil)
        onChange?(value)
    }

    private func refresh() {
        title = isRecording ? "Type a shortcut…" : shortcut?.display ?? "None"
    }
}
