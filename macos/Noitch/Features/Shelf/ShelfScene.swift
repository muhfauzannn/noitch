import AppKit
import QuartzCore

final class ShelfScene {
    let root = CALayer()
    let layout: ShelfLayout
    private let scale: CGFloat
    private var cards: [CALayer] = []
    private let toolbar = CALayer()
    private let footer = CATextLayer()
    private let empty = CALayer()
    private let accent = NSColor(srgbRed: 0.46, green: 0.68, blue: 1, alpha: 1)

    init(layout: ShelfLayout, scale: CGFloat) {
        self.layout = layout
        self.scale = scale
        root.bounds = CGRect(origin: .zero, size: layout.size)
        toolbar.frame = root.bounds
        root.addSublayer(toolbar)
        root.addSublayer(empty)
        for slot in 0..<3 {
            let card = CALayer()
            card.frame = layout.card(slot)
            card.cornerRadius = 12
            root.addSublayer(card)
            cards.append(card)
        }
        footer.frame = layout.footer
        root.addSublayer(footer)
    }

    private func text(_ value: String, size: CGFloat, weight: NSFont.Weight = .regular, alpha: CGFloat = 1, frame: CGRect, on parent: CALayer, color: NSColor = .white, wrapped: Bool = false) {
        let layer = CATextLayer()
        layer.string = value
        layer.font = NSFont.systemFont(ofSize: size, weight: weight)
        layer.fontSize = size
        layer.foregroundColor = color.withAlphaComponent(alpha).cgColor
        layer.contentsScale = scale
        layer.frame = frame
        layer.truncationMode = .end
        layer.isWrapped = wrapped
        parent.addSublayer(layer)
    }

    func apply(items: [ShelfItem], images: [CGImage?], clipboard: Bool, recording: Bool, offset: Int, total: Int, hover: Int?, message: String, dropping: Bool) {
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        toolbar.sublayers = nil
        let labels = ["Files", "Clipboard", recording ? "Pause" : "Resume", "Clear", "‹", "›"]
        for index in 0..<6 where index != 2 || clipboard {
            let frame = layout.button(index)
            let button = CALayer()
            button.frame = frame
            button.cornerRadius = 8
            let selected = index == (clipboard ? 1 : 0)
            button.backgroundColor = NSColor.white.withAlphaComponent(selected ? 0.13 : 0.035).cgColor
            toolbar.addSublayer(button)
            text(labels[index], size: index >= 4 ? 18 : 11, weight: .medium, alpha: selected ? 1 : 0.65,
                 frame: CGRect(x: index >= 4 ? 9 : 10, y: index >= 4 ? -1 : 4, width: frame.width - 16, height: 21), on: button)
        }
        for (slot, card) in cards.enumerated() {
            card.sublayers = nil
            card.isHidden = slot >= items.count || dropping
            guard slot < items.count else { continue }
            let item = items[slot]
            card.backgroundColor = NSColor.white.withAlphaComponent(hover == slot ? 0.105 : 0.055).cgColor
            card.borderWidth = 1
            card.borderColor = accent.withAlphaComponent(hover == slot ? 0.65 : 0.13).cgColor
            let width = card.bounds.width
            let badge = item.kind == .files ? (item.files.count > 1 ? "FILES" : URL(fileURLWithPath: item.files.first ?? "").pathExtension.uppercased()) : item.kind.rawValue.uppercased()
            text(badge.isEmpty ? "FILE" : badge, size: 9, weight: .bold, frame: CGRect(x: 12, y: 9, width: width - 68, height: 13), on: card, color: accent)
            text(item.title, size: 12, weight: .semibold, frame: CGRect(x: 12, y: 27, width: width - 24, height: 17), on: card)
            if slot < images.count, let image = images[slot] {
                let thumbnail = CALayer()
                thumbnail.frame = CGRect(x: width - 55, y: 24, width: 43, height: 42)
                thumbnail.contents = image
                thumbnail.contentsGravity = .resizeAspectFill
                thumbnail.masksToBounds = true
                thumbnail.cornerRadius = 6
                card.addSublayer(thumbnail)
            } else {
                text(item.preview.replacingOccurrences(of: "\n", with: " "), size: 10, alpha: 0.45,
                     frame: CGRect(x: 12, y: 48, width: width - 24, height: 24), on: card, wrapped: true)
            }
            if hover == slot || item.pinned {
                text(item.pinned ? "●" : "+", size: 13, alpha: item.pinned ? 1 : 0.6,
                     frame: CGRect(x: width - 49, y: 5, width: 18, height: 18), on: card, color: accent)
            }
            if hover == slot {
                text("×", size: 16, alpha: 0.6, frame: CGRect(x: width - 24, y: 3, width: 18, height: 21), on: card)
            }
        }
        empty.sublayers = nil
        empty.frame = CGRect(x: 22, y: layout.card(0).minY, width: layout.size.width - 44, height: 77)
        empty.cornerRadius = 12
        empty.backgroundColor = accent.withAlphaComponent(dropping ? 0.13 : 0.025).cgColor
        empty.borderWidth = 1
        empty.borderColor = accent.withAlphaComponent(dropping ? 0.8 : 0.2).cgColor
        empty.isHidden = !items.isEmpty && !dropping
        if !empty.isHidden {
            text(dropping ? "Release to keep it here" : clipboard ? "Your next copy starts here" : "A little space between apps", size: 15, weight: .semibold,
                 frame: CGRect(x: 18, y: 14, width: empty.bounds.width - 36, height: 22), on: empty)
            text(clipboard ? "Copy text, a link, an image or a file. Click a card to copy it back." : "Drop files onto the notch. Pick them up again whenever you need them.", size: 11, alpha: 0.45,
                 frame: CGRect(x: 18, y: 43, width: empty.bounds.width - 36, height: 18), on: empty)
        }
        footer.string = message.isEmpty ? (total == 0 ? (clipboard ? "History stays on this Mac. Pause whenever you need." : "Files stay in their original location.") : "\(offset + 1)–\(min(offset + 3, total)) of \(total)  ·  Click to copy  ·  Drag to another app") : message
        footer.font = NSFont.systemFont(ofSize: 10)
        footer.fontSize = 10
        footer.foregroundColor = NSColor.white.withAlphaComponent(0.4).cgColor
        footer.contentsScale = scale
        footer.truncationMode = .end
        CATransaction.commit()
    }

    func reveal(begin: CFTimeInterval) { Motion.reveal(cards + [toolbar, empty], begin: begin) }
}
