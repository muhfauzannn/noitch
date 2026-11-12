import AppKit
import Foundation
import Testing
@testable import Noitch

struct ShelfTests {
    private func item(_ id: String, pinned: Bool = false) -> ShelfItem {
        ShelfItem(id: id, kind: .text, title: id, preview: id, pinned: pinned)
    }
    @Test func duplicateCopiesMoveToFrontAndKeepTheirPin() {
        let values = [item("new"), item("repeat", pinned: true), item("old")]
        let result = ShelfCollection.inserting(item("repeat"), into: values)
        #expect(result.map(\.id) == ["repeat", "new", "old"])
        #expect(result[0].pinned)
    }
    @Test func capacityEvictsOldestUnpinnedAndProtectsFullPinnedHistory() {
        let values = (0..<50).map { item(String($0), pinned: $0 == 49) }
        let result = ShelfCollection.inserting(item("fresh"), into: values)
        #expect(result.count == 50)
        #expect(!result.contains { $0.id == "48" })
        #expect(result.contains { $0.id == "49" })
        let pinned = values.map { value in var value = value; value.pinned = true; return value }
        #expect(ShelfCollection.inserting(item("extra"), into: pinned) == pinned)
    }
    @Test func layoutSeparatesCardsControlsAndFooter() {
        for header in [CGFloat(24), 32, 38] {
            let layout = ShelfLayout(size: CGSize(width: 580, height: 180), headerHeight: header)
            for slot in 0..<3 {
                #expect(layout.card(slot).minY >= header)
                #expect(layout.card(slot).maxY < layout.footer.minY)
                for button in 0..<6 { #expect(!layout.card(slot).intersects(layout.button(button))) }
            }
            for button in 0..<5 { #expect(layout.button(button).maxX <= layout.button(button + 1).minX) }
        }
    }
    @Test func isolatedClipboardRoundTripAndSecretExclusion() throws {
        let board = NSPasteboard.withUniqueName()
        defer { board.releaseGlobally() }
        let folder = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        defer { try? FileManager.default.removeItem(at: folder) }
        let source = ShelfSource(pasteboard: board, directory: folder)
        board.clearContents(); board.setString("https://example.com/notes", forType: .string)
        let link = try #require(source.capture(force: true))
        #expect(link.kind == .link)
        #expect(source.copy(link))
        #expect(board.string(forType: .string) == "https://example.com/notes")
        #expect(source.capture(force: true) == nil)
        board.clearContents(); board.setString("secret", forType: .string)
        board.setData(Data(), forType: .init("org.nspasteboard.ConcealedType"))
        #expect(source.capture(force: true) == nil)
        board.clearContents(); board.setString("Hello 世界", forType: .string)
        let text = try #require(source.capture(force: true))
        let collection = ShelfCollection(clipboard: [text])
        source.save(collection)
        #expect(source.load() == collection)
        #expect(source.copy(text))
        #expect(board.string(forType: .string) == "Hello 世界")
    }
    @Test func missingFilesDoNotOverwriteTheClipboard() throws {
        let board = NSPasteboard.withUniqueName(); defer { board.releaseGlobally() }
        let source = ShelfSource(pasteboard: board)
        board.setString("keep me", forType: .string)
        let missing = try #require(source.fileItem([URL(fileURLWithPath: "/tmp/noitch-missing-" + UUID().uuidString)]))
        #expect(!source.copy(missing))
        #expect(board.string(forType: .string) == "keep me")
    }
    @Test func imagePayloadRoundTripsAndClearOnlyRemovesStoredCopies() throws {
        let board = NSPasteboard.withUniqueName(); defer { board.releaseGlobally() }
        let folder = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        defer { try? FileManager.default.removeItem(at: folder) }
        let source = ShelfSource(pasteboard: board, directory: folder)
        let bitmap = try #require(NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: 2, pixelsHigh: 2,
            bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true, isPlanar: false, colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0))
        for x in 0..<2 { for y in 0..<2 { bitmap.setColor(.red, atX: x, y: y) } }
        let png = try #require(bitmap.representation(using: .png, properties: [:]))
        board.setData(png, forType: .png)
        let image = try #require(source.capture(force: true))
        #expect(image.kind == .image && image.payload.hasSuffix(".png"))
        #expect(source.copy(image))
        #expect(board.data(forType: .png) == png)
        source.save(ShelfCollection(clipboard: [image]))
        #expect(source.data(for: image) == png)
        let original = folder.appendingPathComponent("original.txt")
        try Data("original stays".utf8).write(to: original)
        let file = try #require(source.fileItem([original]))
        #expect(source.copy(file))
        let paths = board.readObjects(forClasses: [NSURL.self], options: [.urlReadingFileURLsOnly: true]) as? [URL]
        #expect(paths == [original])
        // Moving an image from Clipboard into Files must keep its underlying local payload alive.
        let internalFile = try #require(source.fileItem([folder.appendingPathComponent(image.payload)]))
        source.save(ShelfCollection(files: [internalFile]))
        #expect(source.data(for: image) == png)
        source.save(ShelfCollection())
        #expect(source.data(for: image) == nil)
        #expect(FileManager.default.fileExists(atPath: original.path))
        board.clearContents(); board.setString(String(repeating: "x", count: 64_001), forType: .string)
        #expect(source.capture(force: true) == nil)
    }

}
