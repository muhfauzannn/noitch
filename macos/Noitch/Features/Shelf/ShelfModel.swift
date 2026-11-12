import Foundation
import CoreGraphics

struct ShelfItem: Codable, Equatable, Identifiable {
    enum Kind: String, Codable { case files, text, link, image }
    var id: String
    var kind: Kind
    var title: String
    var preview: String
    var files: [String] = []
    var payload = ""
    var pinned = false
}

struct ShelfCollection: Codable, Equatable {
    static let capacity = 50
    var files: [ShelfItem] = []
    var clipboard: [ShelfItem] = []

    static func inserting(_ item: ShelfItem, into items: [ShelfItem]) -> [ShelfItem] {
        var fresh = item
        fresh.pinned = items.first { $0.id == item.id }?.pinned ?? item.pinned
        var result = items.filter { $0.id != item.id }
        if result.count >= capacity {
            guard let index = result.lastIndex(where: { !$0.pinned }) else { return items }
            result.remove(at: index)
        }
        result.insert(fresh, at: 0)
        return result
    }
}

/// What kind of file a path is, by its extension, for the tile's glyph and colour. A path without an
/// extension is most often a folder, so it gets the folder shape.
enum ShelfFileType: CaseIterable {
    case image, pdf, document, sheet, slides, archive, code, media, folder, other

    static func of(path: String) -> ShelfFileType {
        let ext = (path as NSString).pathExtension.lowercased()
        if ext.isEmpty { return .folder }
        let groups: [(ShelfFileType, Set<String>)] = [
            (.image, ["png", "jpg", "jpeg", "gif", "heic", "webp", "tif", "tiff", "bmp", "svg", "raw"]),
            (.pdf, ["pdf"]),
            (.document, ["doc", "docx", "pages", "txt", "md", "rtf", "odt"]),
            (.sheet, ["xls", "xlsx", "csv", "numbers", "ods", "tsv"]),
            (.slides, ["ppt", "pptx", "key", "odp"]),
            (.archive, ["zip", "rar", "7z", "tar", "gz", "dmg", "iso", "pkg"]),
            (.code, ["swift", "c", "cpp", "h", "hpp", "m", "js", "ts", "tsx", "py", "rb", "go", "rs", "java", "kt", "json", "html", "css", "sh", "yml", "yaml", "xml"]),
            (.media, ["mp3", "wav", "m4a", "aac", "flac", "mp4", "mov", "mkv", "avi", "webm"]),
        ]
        return groups.first { $0.1.contains(ext) }?.0 ?? .other
    }

    /// sRGB, shared with the Windows app.
    var rgb: (Double, Double, Double) {
        switch self {
        case .image: (0.69, 0.45, 1)
        case .pdf: (1, 0.38, 0.35)
        case .document: (0.36, 0.6, 1)
        case .sheet: (0.3, 0.8, 0.5)
        case .slides: (1, 0.6, 0.25)
        case .archive: (0.95, 0.76, 0.3)
        case .code: (0.3, 0.78, 0.85)
        case .media: (1, 0.42, 0.7)
        case .folder: (0.48, 0.64, 0.9)
        case .other: (0.6, 0.62, 0.68)
        }
    }

    /// The label printed on a file's page: its extension, cut to four letters.
    static func label(path: String) -> String {
        String((path as NSString).pathExtension.uppercased().prefix(4))
    }
}

/// Logical points from the page's top-left, y down, shared with the independent Windows layout
/// (shelf_model.h). A slim toolbar under the header, then a tray: seven file tiles in Files, four
/// cards in Clipboard.
struct ShelfLayout {
    static let sidePadding: CGFloat = 22
    static let gap: CGFloat = 8
    static let control: CGFloat = 24
    static let segment: CGFloat = 88
    static let bubble: CGFloat = 18

    var size: CGSize
    var headerHeight: CGFloat
    var toolbarY: CGFloat { headerHeight + 6 }
    var trayTop: CGFloat { headerHeight + 38 }
    var tray: CGRect {
        CGRect(x: Self.sidePadding, y: trayTop, width: size.width - Self.sidePadding * 2, height: size.height - 10 - trayTop)
    }

    static func perPage(clipboard: Bool) -> Int { clipboard ? 4 : 7 }

    /// A file tile or a clipboard card.
    func card(_ slot: Int, clipboard: Bool) -> CGRect {
        let count = CGFloat(Self.perPage(clipboard: clipboard))
        let width = (tray.width - Self.gap * (count - 1)) / count
        return CGRect(x: tray.minX + CGFloat(slot) * (width + Self.gap), y: tray.minY, width: width, height: tray.height)
    }

    func slot(at point: CGPoint, clipboard: Bool) -> Int? {
        (0..<Self.perPage(clipboard: clipboard)).first { card($0, clipboard: clipboard).contains(point) }
    }

    /// A file's page or folder, centred near the tile's top; its name sits below.
    func glyph(_ slot: Int) -> CGRect {
        let tile = card(slot, clipboard: false)
        return CGRect(x: tile.midX - 20, y: tile.minY + 8, width: 40, height: 46)
    }
    func name(_ slot: Int) -> CGRect {
        let tile = card(slot, clipboard: false)
        return CGRect(x: tile.minX + 3, y: glyph(slot).maxY + 5, width: tile.width - 6, height: 26)
    }

    /// Shown on hover. A tile has pin at its top-left corner and remove at its top-right, clear of
    /// the glyph; a card has both at its bottom-right, clear of the text, beside the kind label.
    func pinBubble(_ slot: Int, clipboard: Bool) -> CGRect {
        let tile = card(slot, clipboard: clipboard)
        if clipboard { return removeBubble(slot, clipboard: true).offsetBy(dx: -(Self.bubble + 4), dy: 0) }
        return CGRect(x: tile.minX + 3, y: tile.minY + 3, width: Self.bubble, height: Self.bubble)
    }
    func removeBubble(_ slot: Int, clipboard: Bool) -> CGRect {
        let tile = card(slot, clipboard: clipboard)
        let y = clipboard ? tile.maxY - 4 - Self.bubble : tile.minY + 3
        return CGRect(x: tile.maxX - 4 - Self.bubble, y: y, width: Self.bubble, height: Self.bubble)
    }

    /// 0 Files and 1 Clipboard, one segmented control at the left; 2 pause, 3 clear, 4 back and
    /// 5 forward, round buttons at the right.
    func button(_ index: Int) -> CGRect {
        let y = toolbarY
        switch index {
        case 0, 1: return CGRect(x: Self.sidePadding + CGFloat(index) * Self.segment, y: y, width: Self.segment, height: Self.control)
        default:
            // Back and forward pair up at the far right; clear and pause sit a little apart.
            let right = size.width - Self.sidePadding
            let x: CGFloat
            switch index {
            case 5: x = right - Self.control
            case 4: x = right - Self.control * 2 - 6
            case 3: x = right - Self.control * 3 - 6 - 14
            default: x = right - Self.control * 4 - 6 * 2 - 14
            }
            return CGRect(x: x, y: y, width: Self.control, height: Self.control)
        }
    }

    /// Between the segments and the buttons: the page's position, or a short message.
    var status: CGRect {
        let left = button(1).maxX + 12
        return CGRect(x: left, y: toolbarY + 5, width: button(2).minX - 12 - left, height: 14)
    }
}
