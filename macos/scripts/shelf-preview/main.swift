import AppKit
import QuartzCore

let destination = CommandLine.arguments[1]
let size = CGSize(width: 580, height: 180)
let layout = ShelfLayout(size: size, headerHeight: 38)
let scene = ShelfScene(layout: layout, scale: 2)
let mode = CommandLine.arguments.count > 2 ? CommandLine.arguments[2] : "files"
let clipboard = mode == "clipboard"
let items: [ShelfItem] = mode == "empty" ? [] : clipboard ? [
    ShelfItem(id: "1", kind: .text, title: "Text", preview: "A place for the things you want to use again, as many lines as fit."),
    ShelfItem(id: "2", kind: .link, title: "developer.apple.com", preview: "https://developer.apple.com/documentation/", pinned: true),
    ShelfItem(id: "3", kind: .text, title: "Text", preview: "君の名前を教えて · What's your name?"),
    ShelfItem(id: "4", kind: .files, title: "Report.pdf", preview: "", files: ["/tmp/Report.pdf"]),
] : [
    ShelfItem(id: "1", kind: .files, title: "Launch notes.pdf", preview: "", files: ["/tmp/Launch notes.pdf"]),
    ShelfItem(id: "2", kind: .files, title: "Cover artwork.png", preview: "", files: ["/tmp/Cover artwork.png"], pinned: true),
    ShelfItem(id: "3", kind: .files, title: "Experiment results.csv", preview: "", files: ["/tmp/Experiment results.csv"]),
    ShelfItem(id: "4", kind: .files, title: "Thesis draft.docx", preview: "", files: ["/tmp/Thesis draft.docx"]),
    ShelfItem(id: "5", kind: .files, title: "Assets", preview: "", files: ["/tmp/Assets"]),
    ShelfItem(id: "6", kind: .files, title: "3 files", preview: "", files: ["/tmp/a.swift", "/tmp/b.swift", "/tmp/c.swift"]),
    ShelfItem(id: "7", kind: .files, title: "Backup.zip", preview: "", files: ["/tmp/Backup.zip"]),
]
let dropping = mode == "dropping"
scene.apply(items: items, images: [], clipboard: clipboard, recording: true, offset: 0, total: clipboard || items.isEmpty ? items.count : 12, hover: 1, message: "", dropping: dropping)
let page = CALayer()
page.frame = CGRect(origin: .zero, size: size)
page.isGeometryFlipped = true
scene.root.anchorPoint = .zero
scene.root.position = .zero
page.addSublayer(scene.root)
let header = HeaderLayout(size: size, headerHeight: 38)
for (index, icon) in [HomeIcon.home, .usage, .tasks, .shelf].enumerated() {
    let frame = header.frame(of: .tab(IslandPage(rawValue: index)!))
    if index == 3 {
        let plate = CALayer(); plate.frame = frame; plate.cornerRadius = 13
        plate.backgroundColor = NSColor.white.withAlphaComponent(0.16).cgColor
        page.addSublayer(plate)
    }
    let layer = CAShapeLayer(); layer.frame = frame.insetBy(dx: frame.width * 0.22, dy: frame.height * 0.22)
    layer.path = icon.path(side: layer.bounds.width)
    layer.fillColor = NSColor.white.withAlphaComponent(index == 3 ? 1 : 0.45).cgColor
    page.addSublayer(layer)
}
let background = CALayer()
background.frame = CGRect(origin: .zero, size: size)
background.backgroundColor = NSColor.black.cgColor
background.cornerRadius = 24
background.masksToBounds = true
background.addSublayer(page)
let rep = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: 1160, pixelsHigh: 360, bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true, isPlanar: false, colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0)!
let context = NSGraphicsContext(bitmapImageRep: rep)!.cgContext
context.scaleBy(x: 2, y: 2)
background.render(in: context)
try rep.representation(using: .png, properties: [:])!.write(to: URL(fileURLWithPath: destination))
