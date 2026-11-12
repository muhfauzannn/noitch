import AppKit
import ImageIO

final class ShelfController {
    var onSceneCreated: ((CALayer) -> Void)?
    private let source: ShelfSource
    private var collection: ShelfCollection
    private var scene: ShelfScene?
    private var clipboard = false
    private var offset = 0
    private var hover: Int?
    private var pressed: CGPoint?
    private var dropping = false
    private var message = ""
    private(set) var isDragging = false
    private var isOpen = false
    private var recording: Bool
    private var enabled: Bool
    private var demo = false
    private var thumbnails: [String: CGImage] = [:]

    init(enabled: Bool = true) {
        self.enabled = enabled
        #if DEBUG
        demo = ProcessInfo.processInfo.arguments.contains("--demo-shelf")
        #endif
        source = ShelfSource()
        recording = !demo && (UserDefaults.standard.object(forKey: "shelfRecording") == nil || UserDefaults.standard.bool(forKey: "shelfRecording"))
        collection = demo ? ShelfCollection() : source.load()
        source.onCapture = { [weak self] item in
            guard let self else { return }
            collection.clipboard = ShelfCollection.inserting(item, into: collection.clipboard)
            offset = clipboard ? 0 : offset
            save()
            refresh()
        }
        source.setRecording(recording && enabled)
        #if DEBUG
        if demo {
            collection.files = [ShelfItem(id: "1", kind: .files, title: "Launch notes.pdf", preview: "PDF document · ready to share"),
                                ShelfItem(id: "2", kind: .files, title: "Cover artwork.png", preview: "Image · picked up from Finder", pinned: true),
                                ShelfItem(id: "3", kind: .files, title: "Experiment results.csv", preview: "Spreadsheet · kept in place")]
            collection.clipboard = [ShelfItem(id: "4", kind: .text, title: "Text", preview: "A place for the things you want to use again."), ShelfItem(id: "5", kind: .link, title: "developer.apple.com", preview: "https://developer.apple.com/documentation/", pinned: true)]
        }
        #endif
    }

    private var perPage: Int { ShelfLayout.perPage(clipboard: clipboard) }
    private var items: [ShelfItem] { (clipboard ? collection.clipboard : collection.files).sorted { $0.pinned && !$1.pinned } }
    var sceneRoot: CALayer? { scene?.root }
    func stop() { source.stop() }
    func setEnabled(_ enabled: Bool) {
        guard self.enabled != enabled else { return }
        self.enabled = enabled
        source.setRecording(recording && enabled && !demo)
    }
    private func save() { if !demo { source.save(collection) } }

    func pageWillShow(layout: ShelfLayout, scale: CGFloat) {
        isOpen = true
        scene?.root.removeFromSuperlayer()
        let scene = ShelfScene(layout: layout, scale: scale)
        self.scene = scene
        onSceneCreated?(scene.root)
        if !demo, recording { source.capture() }
        refresh()
    }
    func pageWillHide() {
        isOpen = false
        thumbnails.removeAll()
        pressed = nil
        hover = nil
        // The pager owns any outgoing crossfade; release once it has finished.
        let old = scene
        scene = nil
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.5) { old?.root.removeFromSuperlayer() }
    }
    func reveal(begin: CFTimeInterval) { scene?.reveal(begin: begin) }

    private func refresh() {
        guard isOpen, let scene else { return }
        offset = min(max(offset, 0), max(items.count - 1, 0) / perPage * perPage)
        let visible = Array(items.dropFirst(offset).prefix(perPage))
        let ids = Set(visible.map(\.id))
        thumbnails = thumbnails.filter { ids.contains($0.key) }
        let images: [CGImage?] = visible.map { item in
            if let cached = thumbnails[item.id] { return cached }
            let options = [kCGImageSourceCreateThumbnailFromImageAlways: true, kCGImageSourceCreateThumbnailWithTransform: true, kCGImageSourceThumbnailMaxPixelSize: 128] as CFDictionary
            let image: CGImageSource?
            if item.kind == .image, let data = source.data(for: item) {
                image = CGImageSourceCreateWithData(data as CFData, nil)
            } else if item.kind == .files, item.files.count == 1, let path = item.files.first, ShelfFileType.of(path: path) == .image {
                // A dropped picture shows itself, read from where it lies.
                image = CGImageSourceCreateWithURL(URL(fileURLWithPath: path) as CFURL, nil)
            } else {
                image = nil
            }
            guard let image else { return nil }
            let thumbnail = CGImageSourceCreateThumbnailAtIndex(image, 0, options)
            thumbnails[item.id] = thumbnail
            return thumbnail
        }
        scene.apply(items: visible, images: images, clipboard: clipboard, recording: recording, offset: offset, total: items.count, hover: hover, message: message, dropping: dropping)
    }

    func setDropping(_ value: Bool) { dropping = value; refresh() }
    func accept(_ urls: [URL]) -> Bool {
        let valid = urls.filter(\.isFileURL).prefix(50)
        guard !valid.isEmpty else { return false }
        var added = false
        for url in valid.reversed() {
            if let item = source.fileItem([url]) {
                collection.files = ShelfCollection.inserting(item, into: collection.files)
                added = added || collection.files.contains { $0.id == item.id }
            }
        }
        guard added else { message = "Shelf full: unpin or remove something"; refresh(); return false }
        clipboard = false
        offset = 0
        message = "Added to shelf"
        save(); refresh()
        return true
    }
    func pointerMoved(to point: CGPoint) {
        let visible = max(min(items.count - offset, perPage), 0)
        let next = scene?.layout.slot(at: point, clipboard: clipboard).flatMap { $0 < visible ? $0 : nil }
        if next != hover { hover = next; refresh() }
    }
    func pointerDown(at point: CGPoint) { pressed = point; message = "" }
    func pointerUp(at point: CGPoint) {
        defer { pressed = nil }
        guard let down = pressed, hypot(point.x - down.x, point.y - down.y) < 6, let layout = scene?.layout else { return }
        for index in 0..<6 where (index != 2 || clipboard) && layout.button(index).contains(point) && layout.button(index).contains(down) {
            switch index {
            case 0: clipboard = false; offset = 0
            case 1: clipboard = true; offset = 0; if recording && !demo { source.capture(force: true) }
            case 2: recording.toggle(); source.setRecording(recording && enabled && !demo); if !demo { UserDefaults.standard.set(recording, forKey: "shelfRecording") }
            case 3:
                if clipboard { collection.clipboard.removeAll { !$0.pinned } } else { collection.files.removeAll { !$0.pinned } }
                save()
            case 4: offset = max(offset - perPage, 0)
            default: offset = min(offset + perPage, max(items.count - 1, 0) / perPage * perPage)
            }
            refresh(); return
        }
        guard let slot = layout.slot(at: point, clipboard: clipboard), layout.slot(at: down, clipboard: clipboard) == slot,
              items.indices.contains(offset + slot) else { return }
        let item = items[offset + slot]
        // The bubbles only show on hover, but a press there always means them.
        let hitArea = { (frame: CGRect) in frame.insetBy(dx: -3, dy: -3).contains(point) }
        if hitArea(layout.removeBubble(slot, clipboard: clipboard)) {
            mutate(item.id, remove: true)
        } else if hitArea(layout.pinBubble(slot, clipboard: clipboard)) {
            mutate(item.id, remove: false)
        } else { message = source.copy(item) ? "Copied" : "No longer available" }
        refresh()
    }
    private func mutate(_ id: String, remove: Bool) {
        var values = clipboard ? collection.clipboard : collection.files
        if remove { values.removeAll { $0.id == id } }
        else if let index = values.firstIndex(where: { $0.id == id }) { values[index].pinned.toggle() }
        if clipboard { collection.clipboard = values } else { collection.files = values }
        save()
    }
    func dragItems(at point: CGPoint) -> [NSDraggingItem] {
        guard let layout = scene?.layout, let slot = layout.slot(at: point, clipboard: clipboard), items.indices.contains(offset + slot) else { return [] }
        pressed = nil
        return source.draggingItems(items[offset + slot])
    }
    func setDragging(_ value: Bool) { isDragging = value }
    func scrolled(_ delta: Double) {
        guard abs(delta) > 1 else { return }
        offset += delta > 0 ? -perPage : perPage
        refresh()
    }
}
