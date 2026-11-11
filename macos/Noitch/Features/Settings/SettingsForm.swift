import AppKit

/// One page of the settings window: what the sidebar shows for it, and how to build its rows. Each
/// feature describes its own page next to its code.
struct SettingsPane {
    let title: String
    let glyph: SettingsGlyph
    /// The sidebar badge's colour.
    let color: NSColor
    let build: (SettingsForm) -> Void
}

/// Builds a settings page out of AppKit controls: a title, then groups of rows in rounded boxes,
/// as System Settings does. Every control writes through a key path, so a pane only names its
/// settings and the form keeps them in sync.
final class SettingsForm {
    let view: NSView

    private let stack = NSStackView()
    private let read: () -> AppSettings
    private let write: (AppSettings) -> Void
    /// Rows whose control only means something while another setting is on.
    private var dependents: [(NSControl, KeyPath<AppSettings, Bool>)] = []
    private var targets: [ActionTarget] = []
    private var rows: [NSView] = []

    init(title: String, read: @escaping () -> AppSettings, write: @escaping (AppSettings) -> Void) {
        self.read = read
        self.write = write
        stack.orientation = .vertical
        stack.alignment = .leading
        stack.spacing = 8
        stack.edgeInsets = NSEdgeInsets(top: 20, left: 24, bottom: 24, right: 24)
        stack.translatesAutoresizingMaskIntoConstraints = false
        let heading = NSTextField(labelWithString: title)
        heading.font = .systemFont(ofSize: 20, weight: .bold)
        stack.addArrangedSubview(heading)
        stack.setCustomSpacing(16, after: heading)

        let document = FlippedView()
        document.translatesAutoresizingMaskIntoConstraints = false
        document.addSubview(stack)
        let scroll = NSScrollView()
        scroll.drawsBackground = false
        scroll.hasVerticalScroller = true
        scroll.autohidesScrollers = true
        scroll.documentView = document
        NSLayoutConstraint.activate([
            stack.topAnchor.constraint(equalTo: document.topAnchor),
            stack.leadingAnchor.constraint(equalTo: document.leadingAnchor),
            stack.trailingAnchor.constraint(equalTo: document.trailingAnchor),
            stack.bottomAnchor.constraint(equalTo: document.bottomAnchor),
            document.widthAnchor.constraint(equalTo: scroll.contentView.widthAnchor),
        ])
        view = scroll
    }

    /// The settings as they are now, for panes with controls of their own.
    var settings: AppSettings { read() }

    /// Changes settings from a pane's own control, as the form's controls do.
    func update(_ change: (inout AppSettings) -> Void) {
        var settings = read()
        change(&settings)
        commit(settings)
    }

    /// A titled group. Rows added inside `content` go into one rounded box. Returns the views it
    /// added, so a pane can hide the whole group.
    @discardableResult
    func section(_ title: String? = nil, footer: String? = nil, _ content: () -> Void) -> [NSView] {
        let first = stack.arrangedSubviews.count
        if let title {
            let label = NSTextField(labelWithString: title)
            label.font = .systemFont(ofSize: NSFont.systemFontSize, weight: .semibold)
            stack.addArrangedSubview(label)
        }
        rows = []
        content()
        let box = NSBox()
        box.boxType = .custom
        box.cornerRadius = 8
        box.borderColor = .separatorColor
        box.fillColor = .quaternarySystemFill
        box.contentViewMargins = .zero
        let inner = NSStackView()
        inner.orientation = .vertical
        inner.spacing = 0
        inner.edgeInsets = NSEdgeInsets(top: 0, left: 12, bottom: 0, right: 12)
        for (index, row) in rows.enumerated() {
            if index > 0 {
                let line = NSBox()
                line.boxType = .separator
                inner.addArrangedSubview(line)
                line.widthAnchor.constraint(equalTo: inner.widthAnchor, constant: -24).isActive = true
            }
            inner.addArrangedSubview(row)
            row.widthAnchor.constraint(equalTo: inner.widthAnchor, constant: -24).isActive = true
        }
        box.contentView = inner
        fill(box)
        if let footer {
            stack.setCustomSpacing(4, after: box)
            fill(Self.caption(footer))
        }
        stack.setCustomSpacing(18, after: stack.arrangedSubviews.last ?? box)
        return Array(stack.arrangedSubviews[first...])
    }

    func toggle(_ title: String, caption: String? = nil, _ key: WritableKeyPath<AppSettings, Bool>, enabledBy: KeyPath<AppSettings, Bool>? = nil) {
        let control = NSSwitch()
        control.controlSize = .small
        control.state = read()[keyPath: key] ? .on : .off
        bind(control) { [unowned self] sender in
            guard let control = sender as? NSSwitch else { return }
            var settings = read()
            settings[keyPath: key] = control.state == .on
            commit(settings)
        }
        row(title, caption: caption, control: control, enabledBy: enabledBy)
    }

    /// A pop-up of labelled values. A stored value that isn't among them selects nothing.
    func choice<Value: Equatable>(_ title: String, caption: String? = nil, _ key: WritableKeyPath<AppSettings, Value>, options: [(String, Value)], enabledBy: KeyPath<AppSettings, Bool>? = nil) {
        let control = NSPopUpButton(frame: .zero, pullsDown: false)
        control.controlSize = .small
        control.font = .systemFont(ofSize: NSFont.smallSystemFontSize)
        control.addItems(withTitles: options.map(\.0))
        let current = read()[keyPath: key]
        control.selectItem(at: options.firstIndex { $0.1 == current } ?? -1)
        bind(control) { [unowned self] sender in
            guard let control = sender as? NSPopUpButton, options.indices.contains(control.indexOfSelectedItem) else { return }
            var settings = read()
            settings[keyPath: key] = options[control.indexOfSelectedItem].1
            commit(settings)
        }
        row(title, caption: caption, control: control, enabledBy: enabledBy)
    }

    /// A row whose control is managed by the pane itself, such as one backed by the system. Returns
    /// the caption's label, so the pane can change it later.
    @discardableResult
    func row(_ title: String, caption: String? = nil, control: NSView, enabledBy: KeyPath<AppSettings, Bool>? = nil) -> NSTextField? {
        let labels = NSStackView(views: [NSTextField(labelWithString: title)])
        labels.orientation = .vertical
        labels.alignment = .leading
        labels.spacing = 2
        let captionLabel = caption.map(Self.caption)
        if let captionLabel { labels.addArrangedSubview(captionLabel) }
        labels.setContentHuggingPriority(.defaultLow, for: .horizontal)
        labels.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)
        control.setContentHuggingPriority(.required, for: .horizontal)
        control.setContentCompressionResistancePriority(.required, for: .horizontal)
        let row = NSStackView(views: [labels, control])
        row.orientation = .horizontal
        row.alignment = .centerY
        row.distribution = .fill
        row.spacing = 12
        row.edgeInsets = NSEdgeInsets(top: 9, left: 0, bottom: 9, right: 0)
        rows.append(row)
        if let enabledBy, let control = control as? NSControl {
            dependents.append((control, enabledBy))
            control.isEnabled = read()[keyPath: enabledBy]
        }
        return captionLabel
    }

    /// Text outside any box, such as credits.
    func note(_ text: String) {
        fill(Self.caption(text))
    }

    /// The target-action object for a control, kept alive by the form.
    func bind(_ control: NSControl, _ handler: @escaping (NSControl) -> Void) {
        let target = ActionTarget(handler)
        targets.append(target)
        control.target = target
        control.action = #selector(ActionTarget.fire(_:))
    }

    private func commit(_ settings: AppSettings) {
        write(settings)
        for (control, key) in dependents { control.isEnabled = settings[keyPath: key] }
    }

    private func fill(_ view: NSView) {
        stack.addArrangedSubview(view)
        view.widthAnchor.constraint(equalTo: stack.widthAnchor, constant: -48).isActive = true
    }

    static func caption(_ text: String) -> NSTextField {
        let label = NSTextField(wrappingLabelWithString: text)
        label.font = .systemFont(ofSize: NSFont.smallSystemFontSize)
        label.textColor = .secondaryLabelColor
        label.preferredMaxLayoutWidth = 360
        return label
    }
}

final class ActionTarget: NSObject {
    private let handler: (NSControl) -> Void

    init(_ handler: @escaping (NSControl) -> Void) {
        self.handler = handler
    }

    @objc func fire(_ sender: NSControl) { handler(sender) }
}

private final class FlippedView: NSView {
    override var isFlipped: Bool { true }
}

/// Small white glyphs for the sidebar badges, drawn from paths: SF Symbols would keep about 2 MB of
/// catalog in memory after the window closes (see docs/performance.md).
enum SettingsGlyph {
    case icon(HomeIcon)
    case pages
    case about

    func image(color: NSColor, side: CGFloat = 20) -> NSImage {
        NSImage(size: NSSize(width: side, height: side), flipped: true) { rect in
            guard let context = NSGraphicsContext.current?.cgContext else { return false }
            context.addPath(CGPath(roundedRect: rect, cornerWidth: side * 0.24, cornerHeight: side * 0.24, transform: nil))
            context.setFillColor(color.cgColor)
            context.fillPath()
            let inset = side * 0.18
            context.translateBy(x: inset, y: inset)
            context.addPath(path(side: side - inset * 2))
            context.setFillColor(NSColor.white.cgColor)
            context.fillPath(using: hasHole ? .evenOdd : .winding)
            return true
        }
    }

    private var hasHole: Bool {
        switch self {
        case let .icon(icon): icon.hasHole
        case .pages, .about: true
        }
    }

    /// On the same 24-point grid as `HomeIcon`.
    private func path(side: CGFloat) -> CGPath {
        if case let .icon(icon) = self { return icon.path(side: side) }
        let path = CGMutablePath()
        switch self {
        case .pages:
            // Two cards, one behind the other.
            path.addRoundedRect(in: CGRect(x: 3, y: 6, width: 13, height: 15), cornerWidth: 2, cornerHeight: 2)
            path.addRoundedRect(in: CGRect(x: 5, y: 8, width: 9, height: 11), cornerWidth: 1, cornerHeight: 1)
            path.addRoundedRect(in: CGRect(x: 8, y: 3, width: 10, height: 3), cornerWidth: 1.5, cornerHeight: 1.5)
            path.addRoundedRect(in: CGRect(x: 18, y: 3, width: 3, height: 13), cornerWidth: 1.5, cornerHeight: 1.5)
        case .about:
            path.addEllipse(in: CGRect(x: 2, y: 2, width: 20, height: 20))
            path.addEllipse(in: CGRect(x: 10.5, y: 5.5, width: 3, height: 3))
            path.addRoundedRect(in: CGRect(x: 10.5, y: 10, width: 3, height: 8.5), cornerWidth: 1.5, cornerHeight: 1.5)
        case .icon:
            break
        }
        var scale = CGAffineTransform(scaleX: side / 24, y: side / 24)
        return path.copy(using: &scale) ?? path
    }
}
