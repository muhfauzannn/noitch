import AppKit

/// A plain AppKit window with a sidebar of pages, one per feature, created on first use and released
/// on close. SwiftUI would add about 8 MB that stays after the window closes, which is most of the
/// footprint budget's headroom. Only the page on screen has views.
final class SettingsWindowController: NSObject, NSWindowDelegate, NSTableViewDataSource, NSTableViewDelegate {
    private static let size = NSSize(width: 680, height: 500)
    /// The page shown when the window was last closed, for this run of the app.
    private static var lastPage = 0

    var onChange: ((AppSettings) -> Void)?
    var onClose: (() -> Void)?

    private let window: NSWindow
    private let split = NSSplitViewController()
    private let sidebar = NSTableView()
    private let detail = NSViewController()
    private let panes: [SettingsPane]
    private var settings: AppSettings
    /// The page on screen. Its form holds the controls' targets, so it lives as long as the page.
    private var form: SettingsForm?

    init(settings: AppSettings) {
        self.settings = settings
        panes = [SettingsPanes.general, SettingsPanes.pages, HomeSettingsPane.pane, UsageSettingsPane.pane, SettingsPanes.about]
        window = NSWindow(
            contentRect: NSRect(origin: .zero, size: Self.size),
            styleMask: [.titled, .closable, .miniaturizable, .fullSizeContentView],
            backing: .buffered,
            defer: true
        )
        super.init()
        window.title = "Noitch Settings"
        window.titleVisibility = .hidden
        window.titlebarAppearsTransparent = true
        window.isReleasedWhenClosed = false
        window.delegate = self
        buildSidebar()
        window.contentViewController = split
        window.setContentSize(Self.size)
        window.center()
        let start = min(Self.lastPage, panes.count - 1)
        sidebar.selectRowIndexes(IndexSet(integer: start), byExtendingSelection: false)
        show(page: start)
    }

    func show() {
        NSApp.activate()
        window.makeKeyAndOrderFront(nil)
    }

    func windowWillClose(_ notification: Notification) {
        onClose?()
    }

    private func buildSidebar() {
        let column = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("page"))
        sidebar.addTableColumn(column)
        sidebar.headerView = nil
        sidebar.style = .sourceList
        sidebar.rowSizeStyle = .medium
        sidebar.dataSource = self
        sidebar.delegate = self
        let scroll = NSScrollView()
        scroll.documentView = sidebar
        scroll.drawsBackground = false
        scroll.hasVerticalScroller = false
        let list = NSViewController()
        list.view = scroll
        let sidebarItem = NSSplitViewItem(sidebarWithViewController: list)
        sidebarItem.canCollapse = false
        sidebarItem.minimumThickness = 190
        sidebarItem.maximumThickness = 190
        detail.view = NSView()
        let detailItem = NSSplitViewItem(viewController: detail)
        split.addSplitViewItem(sidebarItem)
        split.addSplitViewItem(detailItem)
    }

    private func show(page index: Int) {
        Self.lastPage = index
        let pane = panes[index]
        let form = SettingsForm(title: pane.title, read: { [unowned self] in settings }, write: { [unowned self] updated in
            settings = updated
            updated.save()
            onChange?(updated)
        })
        pane.build(form)
        self.form = form
        detail.view.subviews.forEach { $0.removeFromSuperview() }
        let view = form.view
        view.translatesAutoresizingMaskIntoConstraints = false
        detail.view.addSubview(view)
        NSLayoutConstraint.activate([
            // Clear of the transparent title bar.
            view.topAnchor.constraint(equalTo: detail.view.safeAreaLayoutGuide.topAnchor),
            view.leadingAnchor.constraint(equalTo: detail.view.leadingAnchor),
            view.trailingAnchor.constraint(equalTo: detail.view.trailingAnchor),
            view.bottomAnchor.constraint(equalTo: detail.view.bottomAnchor),
        ])
    }

    // MARK: Sidebar

    func numberOfRows(in tableView: NSTableView) -> Int { panes.count }

    func tableView(_ tableView: NSTableView, viewFor tableColumn: NSTableColumn?, row: Int) -> NSView? {
        let pane = panes[row]
        let cell = NSTableCellView()
        let image = NSImageView(image: pane.glyph.image(color: pane.color))
        let text = NSTextField(labelWithString: pane.title)
        text.lineBreakMode = .byTruncatingTail
        cell.imageView = image
        cell.textField = text
        let stack = NSStackView(views: [image, text])
        stack.spacing = 8
        stack.translatesAutoresizingMaskIntoConstraints = false
        cell.addSubview(stack)
        NSLayoutConstraint.activate([
            stack.leadingAnchor.constraint(equalTo: cell.leadingAnchor, constant: 2),
            stack.trailingAnchor.constraint(lessThanOrEqualTo: cell.trailingAnchor),
            stack.centerYAnchor.constraint(equalTo: cell.centerYAnchor),
            image.widthAnchor.constraint(equalToConstant: 20),
            image.heightAnchor.constraint(equalToConstant: 20),
        ])
        return cell
    }

    func tableView(_ tableView: NSTableView, heightOfRow row: Int) -> CGFloat { 30 }

    func tableViewSelectionDidChange(_ notification: Notification) {
        let row = sidebar.selectedRow
        guard panes.indices.contains(row) else { return }
        show(page: row)
    }
}
