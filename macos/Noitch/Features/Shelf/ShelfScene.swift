import AppKit
import QuartzCore

/// Shelf's layers: a slim toolbar, then a tray of file tiles or clipboard cards. It decides nothing
/// and draws only when a value changes; the pager owns the page's fade and slide.
final class ShelfScene {
    let root = CALayer()
    let layout: ShelfLayout
    private let scale: CGFloat
    private let toolbar = CALayer()
    private let tray = CALayer()
    private let accent = NSColor(srgbRed: 0.46, green: 0.68, blue: 1, alpha: 1)

    init(layout: ShelfLayout, scale: CGFloat) {
        self.layout = layout
        self.scale = scale
        root.bounds = CGRect(origin: .zero, size: layout.size)
        for layer in [toolbar, tray] {
            layer.frame = root.bounds
            root.addSublayer(layer)
        }
    }

    // MARK: Drawing helpers

    @discardableResult
    private func text(_ value: Any, size: CGFloat, weight: NSFont.Weight = .regular, alpha: CGFloat = 1, frame: CGRect, on parent: CALayer,
                      color: NSColor = .white, align: CATextLayerAlignmentMode = .left, wrapped: Bool = false) -> CATextLayer {
        let layer = CATextLayer()
        layer.string = value
        layer.font = NSFont.systemFont(ofSize: size, weight: weight)
        layer.fontSize = size
        layer.foregroundColor = color.withAlphaComponent(alpha).cgColor
        layer.contentsScale = scale
        layer.frame = frame
        layer.alignmentMode = align
        layer.truncationMode = .end
        layer.isWrapped = wrapped
        parent.addSublayer(layer)
        return layer
    }

    private func plate(_ frame: CGRect, radius: CGFloat, color: NSColor, on parent: CALayer) -> CALayer {
        let layer = CALayer()
        layer.frame = frame
        layer.cornerRadius = radius
        layer.cornerCurve = .continuous
        layer.backgroundColor = color.cgColor
        parent.addSublayer(layer)
        return layer
    }

    private func icon(_ icon: HomeIcon, in frame: CGRect, color: NSColor, on parent: CALayer) {
        let layer = CAShapeLayer()
        layer.frame = frame
        layer.path = icon.path(side: frame.width)
        layer.fillRule = icon.hasHole ? .evenOdd : .nonZero
        layer.fillColor = icon.isOutline ? nil : color.cgColor
        layer.strokeColor = icon.isOutline ? color.cgColor : nil
        layer.lineWidth = max(frame.width / 10, 1.4)
        layer.lineCap = .round
        layer.lineJoin = .round
        parent.addSublayer(layer)
    }

    private static func color(_ type: ShelfFileType, alpha: CGFloat = 1) -> NSColor {
        let (r, g, b) = type.rgb
        return NSColor(srgbRed: r, green: g, blue: b, alpha: alpha)
    }

    /// A page with a folded corner in the type's colour and its extension printed on it, a folder
    /// for a folder, or a little stack of pages for several files.
    private func fileGlyph(paths: [String], in frame: CGRect, on parent: CALayer) {
        let first = paths.first ?? ""
        let type = paths.count > 1 ? ShelfFileType.other : ShelfFileType.of(path: first)
        let color = Self.color(paths.count > 1 ? ShelfFileType.of(path: first) : type)
        let sx = frame.width / 24, sy = frame.height / 24
        func point(_ x: CGFloat, _ y: CGFloat) -> CGPoint { CGPoint(x: x * sx, y: y * sy) }
        func shape(_ points: [CGPoint], fill: NSColor, offset: CGPoint = .zero) {
            let layer = CAShapeLayer()
            layer.frame = frame.offsetBy(dx: offset.x, dy: offset.y)
            let path = CGMutablePath()
            path.addLines(between: points)
            path.closeSubpath()
            layer.path = path
            layer.fillColor = fill.cgColor
            layer.strokeColor = fill.cgColor
            layer.lineWidth = 2
            layer.lineJoin = .round
            parent.addSublayer(layer)
        }
        if type == .folder {
            shape([point(2, 5), point(9, 5), point(11, 7.5), point(22, 7.5), point(22, 21), point(2, 21)], fill: color)
            shape([point(2, 10), point(22, 10), point(22, 21), point(2, 21)], fill: color.blended(withFraction: 0.25, of: .white) ?? color)
            return
        }
        let page = [point(4, 1.5), point(15, 1.5), point(20, 6.5), point(20, 22.5), point(4, 22.5)]
        if paths.count > 1 {
            // Two pages behind, offset up and to the right.
            shape(page, fill: color.withAlphaComponent(0.35), offset: CGPoint(x: 5, y: -4))
            shape(page, fill: color.withAlphaComponent(0.6), offset: CGPoint(x: 2.5, y: -2))
        }
        shape(page, fill: color)
        shape([point(15, 1.5), point(15, 6.5), point(20, 6.5)], fill: NSColor.white.withAlphaComponent(0.45))
        let label = paths.count > 1 ? "\(paths.count)" : ShelfFileType.label(path: first)
        text(label.isEmpty ? "FILE" : label, size: paths.count > 1 ? 13 : 9, weight: .heavy,
             frame: CGRect(x: frame.minX, y: frame.midY - 2, width: frame.width, height: 14), on: parent, align: .center)
    }

    // MARK: State

    /// `images` holds a thumbnail, or nil, for each visible item, in order.
    func apply(items: [ShelfItem], images: [CGImage?], clipboard: Bool, recording: Bool, offset: Int, total: Int, hover: Int?, message: String, dropping: Bool) {
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        drawToolbar(clipboard: clipboard, recording: recording, offset: offset, total: total, count: items.count, message: message)
        tray.sublayers = nil
        if items.isEmpty || dropping {
            drawEmpty(clipboard: clipboard, dropping: dropping)
        } else {
            for (slot, item) in items.enumerated() {
                let image = slot < images.count ? images[slot] : nil
                if clipboard { drawCard(item, image: image, slot: slot, hovered: hover == slot) }
                else { drawTile(item, image: image, slot: slot, hovered: hover == slot) }
            }
        }
        CATransaction.commit()
    }

    private func drawToolbar(clipboard: Bool, recording: Bool, offset: Int, total: Int, count: Int, message: String) {
        toolbar.sublayers = nil
        let track = layout.button(0).union(layout.button(1))
        _ = plate(track, radius: track.height / 2, color: NSColor.white.withAlphaComponent(0.06), on: toolbar)
        let selected = layout.button(clipboard ? 1 : 0).insetBy(dx: 2, dy: 2)
        _ = plate(selected, radius: selected.height / 2, color: NSColor.white.withAlphaComponent(0.16), on: toolbar)
        for (index, name) in ["Files", "Clipboard"].enumerated() {
            let frame = layout.button(index)
            let on = index == (clipboard ? 1 : 0)
            text(name, size: 11, weight: .semibold, alpha: on ? 1 : 0.55, frame: CGRect(x: frame.minX, y: frame.minY + 5, width: frame.width, height: 15), on: toolbar, align: .center)
        }
        var buttons: [(Int, HomeIcon, Bool, Bool)] = [(3, .trash, total > 0, false), (4, .back, offset > 0, false), (5, .forward, offset + count < total, false)]
        if clipboard { buttons.insert((2, recording ? .pause : .play, true, !recording), at: 0) }
        for (index, glyph, enabled, highlighted) in buttons {
            let frame = layout.button(index)
            _ = plate(frame, radius: frame.height / 2, color: highlighted ? accent.withAlphaComponent(0.3) : NSColor.white.withAlphaComponent(0.06), on: toolbar)
            icon(glyph, in: frame.insetBy(dx: 6.5, dy: 6.5), color: NSColor.white.withAlphaComponent(enabled ? 0.85 : 0.25), on: toolbar)
        }
        let status: String
        if !message.isEmpty { status = message }
        else if clipboard && !recording { status = "Paused" }
        else if count > 0, total > count || offset > 0 { status = "\(offset + 1)–\(offset + count) of \(total)" }
        else { status = "" }
        text(status, size: 10, weight: .medium, alpha: message.isEmpty ? 0.4 : 0.75, frame: layout.status, on: toolbar, color: message.isEmpty ? .white : accent, align: .center)
    }

    /// A dashed tray that invites a drop, lighting up while something is dragged over.
    private func drawEmpty(clipboard: Bool, dropping: Bool) {
        let frame = layout.tray
        let border = CAShapeLayer()
        border.frame = frame
        border.path = CGPath(roundedRect: border.bounds.insetBy(dx: 0.75, dy: 0.75), cornerWidth: 16, cornerHeight: 16, transform: nil)
        border.fillColor = (dropping ? accent.withAlphaComponent(0.12) : NSColor.white.withAlphaComponent(0.025)).cgColor
        border.strokeColor = (dropping ? accent : NSColor.white.withAlphaComponent(0.2)).cgColor
        border.lineWidth = 1.5
        border.lineDashPattern = dropping ? nil : [5, 4]
        tray.addSublayer(border)
        let glyph = CGRect(x: frame.midX - 11, y: frame.midY - 22, width: 22, height: 22)
        icon(clipboard ? .lyrics : .shelf, in: glyph, color: (dropping ? accent : NSColor.white).withAlphaComponent(dropping ? 1 : 0.35), on: tray)
        let line = dropping ? "Release to drop it here" : clipboard ? "Copy something and it lands here" : "Drop files here, or onto the notch"
        text(line, size: 12, weight: .semibold, alpha: dropping ? 1 : 0.55, frame: CGRect(x: frame.minX, y: glyph.maxY + 8, width: frame.width, height: 16), on: tray, align: .center)
    }

    /// One file, Boring Notch style: a big glyph or thumbnail with the name under it.
    private func drawTile(_ item: ShelfItem, image: CGImage?, slot: Int, hovered: Bool) {
        let tile = layout.card(slot, clipboard: false)
        if hovered { _ = plate(tile, radius: 14, color: NSColor.white.withAlphaComponent(0.08), on: tray) }
        let glyph = layout.glyph(slot)
        if let image {
            let thumbnail = CALayer()
            thumbnail.frame = glyph.insetBy(dx: -2, dy: 3)
            thumbnail.contents = image
            thumbnail.contentsGravity = .resizeAspectFill
            thumbnail.masksToBounds = true
            thumbnail.cornerRadius = 6
            thumbnail.borderWidth = 1
            thumbnail.borderColor = NSColor.white.withAlphaComponent(0.25).cgColor
            tray.addSublayer(thumbnail)
        } else {
            fileGlyph(paths: item.files.isEmpty ? [item.title] : item.files, in: glyph, on: tray)
        }
        text(item.title, size: 10, weight: .medium, alpha: 0.85, frame: layout.name(slot), on: tray, align: .center, wrapped: true)
        drawBubbles(item, slot: slot, clipboard: false, hovered: hovered)
    }

    /// One copied thing: text as text, a link as its site, an image as itself.
    private func drawCard(_ item: ShelfItem, image: CGImage?, slot: Int, hovered: Bool) {
        let card = layout.card(slot, clipboard: true)
        let base = plate(card, radius: 14, color: NSColor.white.withAlphaComponent(hovered ? 0.11 : 0.06), on: tray)
        base.masksToBounds = true
        let inner = CGRect(x: 10, y: 10, width: card.width - 20, height: card.height - 32)
        let kind: String
        switch item.kind {
        case .image:
            kind = "Image"
            if let image {
                let picture = CALayer()
                picture.frame = base.bounds
                picture.contents = image
                picture.contentsGravity = .resizeAspectFill
                base.addSublayer(picture)
            } else {
                icon(.shelf, in: CGRect(x: card.width / 2 - 12, y: card.height / 2 - 18, width: 24, height: 24), color: NSColor.white.withAlphaComponent(0.4), on: base)
            }
        case .link:
            kind = "Link"
            text(item.title, size: 13, weight: .semibold, frame: CGRect(x: inner.minX, y: inner.minY, width: inner.width, height: 17), on: base)
            text(item.preview, size: 10, alpha: 0.5, frame: CGRect(x: inner.minX, y: inner.minY + 20, width: inner.width, height: inner.height - 20), on: base, wrapped: true)
        case .files:
            kind = item.files.count > 1 ? "\(item.files.count) files" : "File"
            fileGlyph(paths: item.files, in: CGRect(x: inner.minX, y: inner.minY, width: 26, height: 30), on: base)
            text(item.title, size: 11, weight: .semibold, frame: CGRect(x: inner.minX, y: inner.minY + 34, width: inner.width, height: inner.height - 34), on: base, wrapped: true)
        case .text:
            kind = "Text"
            let preview = item.preview.split(whereSeparator: \.isWhitespace).joined(separator: " ")
            text(preview, size: 11, weight: .medium, alpha: 0.88, frame: inner, on: base, wrapped: true)
        }
        // The kind, on a dark pill so it reads over a picture too.
        let label = NSAttributedString(string: kind, attributes: [.font: NSFont.systemFont(ofSize: 9, weight: .bold)])
        let width = ceil(label.size().width) + 12
        let pill = plate(CGRect(x: 8, y: card.height - 20, width: width, height: 14), radius: 7, color: NSColor.black.withAlphaComponent(item.kind == .image ? 0.55 : 0), on: base)
        text(kind, size: 9, weight: .bold, alpha: item.kind == .link ? 1 : 0.5, frame: CGRect(x: 0, y: 1, width: width, height: 12), on: pill,
             color: item.kind == .link ? accent : .white, align: item.kind == .image ? .center : .left)
        drawBubbles(item, slot: slot, clipboard: true, hovered: hovered)
    }

    /// Pinned items wear a small pin; hovering shows pin and remove as round bubbles.
    private func drawBubbles(_ item: ShelfItem, slot: Int, clipboard: Bool, hovered: Bool) {
        let pin = layout.pinBubble(slot, clipboard: clipboard)
        if hovered {
            for (frame, glyph, color) in [(pin, HomeIcon.pin, item.pinned ? accent : NSColor.white), (layout.removeBubble(slot, clipboard: clipboard), .close, NSColor.white)] {
                _ = plate(frame, radius: frame.height / 2, color: NSColor(white: 0.18, alpha: 0.95), on: tray)
                icon(glyph, in: frame.insetBy(dx: 4.5, dy: 4.5), color: color, on: tray)
            }
        } else if item.pinned {
            icon(.pin, in: pin.insetBy(dx: 3, dy: 3), color: accent, on: tray)
        }
    }

    func reveal(begin: CFTimeInterval) { Motion.reveal([toolbar, tray], begin: begin, rising: [tray]) }
}
