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

/// Logical points, shared with the independent Windows layout.
struct ShelfLayout {
    var size: CGSize
    var headerHeight: CGFloat
    var toolbarY: CGFloat { headerHeight + 7 }
    func card(_ slot: Int) -> CGRect {
        let width = (size.width - 44 - 16) / 3
        return CGRect(x: 22 + CGFloat(slot) * (width + 8), y: headerHeight + 37, width: width, height: 77)
    }
    func button(_ index: Int) -> CGRect {
        let widths: [CGFloat] = [59, 83, 60, 52, 28, 28]
        let xs: [CGFloat] = [22, 83, size.width - 254, size.width - 190, size.width - 132, size.width - 100]
        return CGRect(x: xs[index], y: toolbarY, width: widths[index], height: 23)
    }
    var footer: CGRect { CGRect(x: 24, y: size.height - 20, width: size.width - 48, height: 13) }
}
