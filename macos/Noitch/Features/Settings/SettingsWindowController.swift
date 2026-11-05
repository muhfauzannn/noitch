import AppKit
import ServiceManagement

/// A plain AppKit window, created on first use and released on close. SwiftUI would add about 8 MB
/// that stays after the window closes, which is most of the footprint budget's headroom.
final class SettingsWindowController: NSObject, NSWindowDelegate {
    private static let width: CGFloat = 440

    var onChange: ((AppSettings) -> Void)?
    var onClose: (() -> Void)?

    private let window: NSWindow
    private var settings: AppSettings
    private var switches: [(NSSwitch, WritableKeyPath<AppSettings, Bool>)] = []
    private let launchAtLogin = NSSwitch()
    private let launchStatus = SettingsWindowController.makeCaption("")

    init(settings: AppSettings) {
        self.settings = settings
        window = NSWindow(
            contentRect: NSRect(x: 0, y: 0, width: Self.width, height: 400),
            styleMask: [.titled, .closable],
            backing: .buffered,
            defer: true
        )
        super.init()
        window.title = "Noitch Settings"
        window.isReleasedWhenClosed = false
        window.delegate = self
        buildContent()
        window.center()
    }

    func show() {
        NSApp.activate()
        window.makeKeyAndOrderFront(nil)
    }

    func windowWillClose(_ notification: Notification) {
        onClose?()
    }

    private func buildContent() {
        let stack = NSStackView()
        stack.orientation = .vertical
        stack.alignment = .leading
        stack.spacing = 10
        stack.edgeInsets = NSEdgeInsets(top: 20, left: 24, bottom: 20, right: 24)

        stack.addArrangedSubview(Self.makeHeading("General"))
        launchAtLogin.target = self
        launchAtLogin.action = #selector(launchAtLoginChanged)
        launchAtLogin.state = SMAppService.mainApp.status == .enabled ? .on : .off
        stack.addArrangedSubview(makeRow("Launch at login", caption: nil, control: launchAtLogin))
        stack.addArrangedSubview(launchStatus)
        launchStatus.isHidden = true
        stack.addArrangedSubview(makeToggle("Open on hover", caption: "When off, click the island to open it.", key: \.openOnHover))

        stack.addArrangedSubview(Self.makeHeading("Media"))
        stack.addArrangedSubview(makeToggle("Show lyrics", caption: "Sends the track and artist name to lrclib.net.", key: \.showLyrics))
        stack.addArrangedSubview(makeToggle("Show shuffle and repeat", caption: nil, key: \.showShuffleAndRepeat))
        stack.addArrangedSubview(makeToggle("Tint progress bar with artwork", caption: nil, key: \.tintWithArtwork))

        stack.addArrangedSubview(Self.makeHeading("About"))
        let version = Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? ""
        stack.addArrangedSubview(Self.makeCaption("Noitch \(version). Now playing uses MediaRemoteAdapter, © Jonas van den Berg and contributors, BSD 3-Clause. Lyrics come from LRCLIB."))

        stack.widthAnchor.constraint(equalToConstant: Self.width).isActive = true
        for view in stack.arrangedSubviews {
            view.widthAnchor.constraint(equalTo: stack.widthAnchor, constant: -48).isActive = true
        }
        window.contentView = stack
        window.setContentSize(stack.fittingSize)
    }

    private func makeToggle(_ title: String, caption: String?, key: WritableKeyPath<AppSettings, Bool>) -> NSView {
        let control = NSSwitch()
        control.state = settings[keyPath: key] ? .on : .off
        control.target = self
        control.action = #selector(toggleChanged)
        switches.append((control, key))
        return makeRow(title, caption: caption, control: control)
    }

    private func makeRow(_ title: String, caption: String?, control: NSControl) -> NSView {
        let labels = NSStackView(views: [NSTextField(labelWithString: title)])
        labels.orientation = .vertical
        labels.alignment = .leading
        labels.spacing = 2
        if let caption { labels.addArrangedSubview(Self.makeCaption(caption)) }
        let row = NSStackView(views: [labels, control])
        row.orientation = .horizontal
        row.distribution = .gravityAreas
        row.setHuggingPriority(.defaultLow, for: .horizontal)
        labels.setContentHuggingPriority(.defaultLow, for: .horizontal)
        control.setContentHuggingPriority(.required, for: .horizontal)
        return row
    }

    @objc private func toggleChanged(_ sender: NSSwitch) {
        guard let key = switches.first(where: { $0.0 === sender })?.1 else { return }
        settings[keyPath: key] = sender.state == .on
        settings.save()
        onChange?(settings)
    }

    @objc private func launchAtLoginChanged(_ sender: NSSwitch) {
        do {
            if sender.state == .on {
                try SMAppService.mainApp.register()
            } else {
                try SMAppService.mainApp.unregister()
            }
            launchStatus.isHidden = true
        } catch {
            sender.state = SMAppService.mainApp.status == .enabled ? .on : .off
            launchStatus.stringValue = "Couldn't change this: \(error.localizedDescription)"
            launchStatus.isHidden = false
        }
    }

    private static func makeHeading(_ title: String) -> NSTextField {
        let label = NSTextField(labelWithString: title)
        label.font = .systemFont(ofSize: NSFont.systemFontSize, weight: .semibold)
        return label
    }

    private static func makeCaption(_ text: String) -> NSTextField {
        let label = NSTextField(wrappingLabelWithString: text)
        label.font = .systemFont(ofSize: NSFont.smallSystemFontSize)
        label.textColor = .secondaryLabelColor
        return label
    }
}
