import AppKit
import QuartzCore

/// The usage page's layers: one column per tool. It draws what it is given and decides nothing.
/// Text changes only when a value changes, at most once a minute while shown.
final class UsageScene {
    /// Placed by the pager, which owns its fade and slide.
    let root = CALayer()

    private struct Column {
        let group = CALayer()
        let title = UsageScene.makeText(size: 12, weight: .semibold, opacity: 0.6)
        let tokens = UsageScene.makeText(size: 22, weight: .semibold, opacity: 1)
        let cost = UsageScene.makeText(size: 15, weight: .medium, opacity: 0.85, alignment: .right)
        let limit = UsageScene.makeText(size: 11, weight: .regular, opacity: 0.6)
        let track = CALayer()
        let fill = CALayer()
        let names = (0..<UsageModel.maxRows).map { _ in UsageScene.makeText(size: 11, weight: .regular, opacity: 0.8) }
        let values = (0..<UsageModel.maxRows).map { _ in UsageScene.makeText(size: 11, weight: .regular, opacity: 0.5, alignment: .right) }
    }

    private let layout: UsageLayout
    private let columns = [Column(), Column()]
    private var shown: UsageModel?

    init(layout: UsageLayout, scale: CGFloat) {
        self.layout = layout
        root.bounds = CGRect(origin: .zero, size: layout.size)
        for (index, column) in columns.enumerated() {
            column.group.frame = root.bounds
            root.addSublayer(column.group)
            let texts = [(column.title, layout.title(index)), (column.tokens, layout.tokens(index)), (column.cost, layout.cost(index)),
                         (column.limit, layout.limit(index))]
                + column.names.enumerated().map { ($0.element, layout.row(index, $0.offset)) }
                + column.values.enumerated().map { ($0.element, layout.row(index, $0.offset)) }
            for (layer, frame) in texts {
                layer.frame = frame
                layer.contentsScale = scale
                column.group.addSublayer(layer)
            }
            column.track.frame = layout.limitTrack(index)
            column.track.cornerRadius = UsageLayout.trackHeight / 2
            column.track.backgroundColor = NSColor.white.withAlphaComponent(0.2).cgColor
            column.group.addSublayer(column.track)
            column.fill.anchorPoint = .zero
            column.fill.position = .zero
            column.fill.cornerRadius = UsageLayout.trackHeight / 2
            column.track.addSublayer(column.fill)
        }
    }

    func apply(_ model: UsageModel) {
        guard model != shown else { return }
        shown = model
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        for (column, data) in zip(columns, [model.claude, model.codex]) {
            column.title.string = data.title
            column.tokens.string = data.tokens
            column.cost.string = data.cost
            column.limit.string = data.limit
            column.track.isHidden = data.limitFraction == nil
            let fraction = data.limitFraction ?? 0
            column.fill.bounds = CGRect(x: 0, y: 0, width: layout.columnWidth * fraction, height: UsageLayout.trackHeight)
            // Amber once most of the window is used, so a glance tells whether to slow down.
            column.fill.backgroundColor = (fraction >= 0.8 ? NSColor.systemOrange : NSColor.white).cgColor
            for (index, (name, value)) in zip(column.names, column.values).enumerated() {
                let row = index < data.rows.count ? data.rows[index] : nil
                name.string = row?.name
                value.string = row?.value
            }
        }
        CATransaction.commit()
    }

    func reveal(begin: CFTimeInterval) {
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        Motion.reveal(columns.map(\.group), begin: begin)
        CATransaction.commit()
    }

    private static func makeText(size: CGFloat, weight: NSFont.Weight, opacity: Float, alignment: CATextLayerAlignmentMode = .left) -> CATextLayer {
        let layer = CATextLayer()
        layer.font = NSFont.systemFont(ofSize: size, weight: weight)
        layer.fontSize = size
        layer.foregroundColor = NSColor.white.cgColor
        layer.opacity = opacity
        layer.truncationMode = .end
        layer.alignmentMode = alignment
        return layer
    }
}
