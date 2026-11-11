import AppKit
import QuartzCore

/// The tasks page's layers: the next task as a countdown, a runway of the next two weeks with a dot
/// per task, and a short list. It draws what it is given and decides nothing.
final class TasksScene {
    /// Placed by the pager, which owns its fade and slide.
    let root = CALayer()

    static let critical = NSColor(srgbRed: 1, green: 0.35, blue: 0.31, alpha: 1)
    static let watch = NSColor(srgbRed: 1, green: 0.71, blue: 0.28, alpha: 1)
    static let fresh = NSColor(srgbRed: 0.3, green: 0.55, blue: 1, alpha: 1)

    private let layout: TasksLayout
    private let scale: CGFloat

    private let hero = CALayer()
    private let heroLabel = TasksScene.text(size: 9.5, weight: .semibold, opacity: 0.45)
    private let countdown = TasksScene.text(size: 38, weight: .semibold)
    private let heroTitle = TasksScene.text(size: 13, weight: .semibold)
    private let heroCourse = TasksScene.text(size: 9.5, weight: .bold, alignment: .center)
    private let heroDue = TasksScene.text(size: 11, weight: .regular, opacity: 0.55)
    private let openButton = CALayer()
    private let openLabel = TasksScene.text(size: 10.5, weight: .semibold, alignment: .center)

    private let runway = CALayer()
    private let dayLabels: [CATextLayer]
    private var dots: [Int: CALayer] = [:]

    private let list = CALayer()
    private let hoverPlate = CALayer()
    private let rows: [(icon: CALayer, glyph: CAShapeLayer, title: CATextLayer, due: CATextLayer)]
    private let more = TasksScene.text(size: 10, weight: .regular, opacity: 0.4)
    private let overdueToggle = TasksScene.text(size: 10, weight: .semibold, alignment: .right)

    private let message = CALayer()
    private let check = CAShapeLayer()
    private let messageTitle = TasksScene.text(size: 17, weight: .semibold, alignment: .center)
    private let messageDetail = TasksScene.text(size: 11.5, weight: .regular, opacity: 0.55, alignment: .center)
    private let messageButton = CALayer()
    private let messageButtonLabel = TasksScene.text(size: 11, weight: .semibold, alignment: .center)

    private var shown: TasksPageModel?
    private var hoveredRow: Int?
    private var hoveredDot: Int?

    init(layout: TasksLayout, scale: CGFloat) {
        self.layout = layout
        self.scale = scale
        root.bounds = CGRect(origin: .zero, size: layout.size)
        dayLabels = (0..<TaskRule.runwayDays).map { _ in TasksScene.text(size: 8.5, weight: .medium, opacity: 0.38, alignment: .center) }
        rows = (0..<TasksLayout.visibleRows).map { _ in
            (CALayer(), CAShapeLayer(), TasksScene.text(size: 11.5, weight: .medium), TasksScene.text(size: 10.5, weight: .regular, alignment: .right))
        }
        for group in [hero, runway, list, message] {
            group.frame = root.bounds
            root.addSublayer(group)
        }

        place(heroLabel, layout.heroLabel, in: hero)
        place(countdown, layout.countdown, in: hero)
        place(heroTitle, layout.heroTitle, in: hero)
        heroCourse.backgroundColor = NSColor.white.withAlphaComponent(0.13).cgColor
        heroCourse.cornerRadius = 4
        place(heroCourse, layout.heroCourse, in: hero)
        place(heroDue, layout.heroDue, in: hero)
        openButton.frame = layout.openButton
        openButton.backgroundColor = NSColor.white.cgColor
        openButton.cornerRadius = layout.openButton.height / 2
        hero.addSublayer(openButton)
        openLabel.foregroundColor = NSColor.black.cgColor
        openLabel.string = "Open in Scele ↗"
        place(openLabel, layout.openButton.insetBy(dx: 0, dy: 3.5), in: hero)

        let axis = CALayer()
        axis.frame = CGRect(x: layout.runway.minX, y: layout.runway.minY + TasksLayout.axisY, width: layout.runway.width, height: 1)
        axis.backgroundColor = NSColor.white.withAlphaComponent(0.18).cgColor
        runway.addSublayer(axis)
        for day in 1..<TaskRule.runwayDays {
            let tick = CALayer()
            let x = layout.dayLabel(day).minX
            tick.frame = CGRect(x: x, y: layout.runway.minY + TasksLayout.axisY - 2, width: 1, height: 5)
            tick.backgroundColor = NSColor.white.withAlphaComponent(0.22).cgColor
            runway.addSublayer(tick)
        }
        let now = CALayer()
        now.frame = CGRect(x: layout.runway.minX, y: layout.runway.minY + TasksLayout.axisY - 7, width: 2, height: 15)
        now.cornerRadius = 1
        now.backgroundColor = NSColor.white.cgColor
        runway.addSublayer(now)
        for (day, label) in dayLabels.enumerated() { place(label, layout.dayLabel(day), in: runway) }

        hoverPlate.backgroundColor = NSColor.white.withAlphaComponent(0.08).cgColor
        hoverPlate.cornerRadius = 7
        hoverPlate.isHidden = true
        list.addSublayer(hoverPlate)
        for (index, row) in rows.enumerated() {
            row.icon.frame = layout.rowIcon(index)
            row.icon.cornerRadius = 5
            list.addSublayer(row.icon)
            row.glyph.frame = row.icon.bounds.insetBy(dx: 3, dy: 3)
            row.glyph.fillRule = .evenOdd
            row.glyph.contentsScale = scale
            row.icon.addSublayer(row.glyph)
            place(row.title, layout.rowTitle(index), in: list)
            place(row.due, layout.rowDue(index), in: list)
        }
        place(more, layout.more, in: list)
        place(overdueToggle, layout.overdueToggle, in: list)

        check.frame = layout.check
        let circle = CGMutablePath()
        circle.addEllipse(in: check.bounds.insetBy(dx: 1, dy: 1))
        circle.move(to: CGPoint(x: 10, y: 17.5))
        circle.addLine(to: CGPoint(x: 15, y: 22.5))
        circle.addLine(to: CGPoint(x: 24.5, y: 12.5))
        check.path = circle
        check.fillColor = nil
        check.strokeColor = NSColor(srgbRed: 0.18, green: 0.82, blue: 0.49, alpha: 1).cgColor
        check.lineWidth = 2.2
        check.lineCap = .round
        check.lineJoin = .round
        check.contentsScale = scale
        message.addSublayer(check)
        place(messageTitle, layout.message, in: message)
        place(messageDetail, layout.messageDetail, in: message)
        messageButton.frame = layout.messageButton
        messageButton.backgroundColor = NSColor.white.cgColor
        messageButton.cornerRadius = layout.messageButton.height / 2
        message.addSublayer(messageButton)
        messageButtonLabel.foregroundColor = NSColor.black.cgColor
        place(messageButtonLabel, layout.messageButton.insetBy(dx: 0, dy: 4), in: message)
    }

    /// The groups to stagger in, in order.
    var revealGroups: [CALayer] {
        if case .message = shown?.content { return [message] }
        return [hero, runway, list]
    }

    func apply(_ model: TasksPageModel) {
        guard model != shown else { return }
        let wasAllClear = { if case let .message(_, _, _, clear) = shown?.content { return clear } else { return false } }()
        shown = model
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        for (label, day) in zip(dayLabels, model.days) { label.string = day }
        switch model.content {
        case let .message(title, detail, button, allClear):
            setList(visible: false)
            message.isHidden = false
            messageTitle.string = title
            messageDetail.string = detail
            messageButton.isHidden = button == nil
            messageButtonLabel.isHidden = button == nil
            messageButtonLabel.string = button
            check.isHidden = !allClear
            // Text sits lower under the tick, and higher without it.
            let shift: CGFloat = allClear ? 10 : -6
            messageTitle.frame = layout.message.offsetBy(dx: 0, dy: shift)
            messageDetail.frame = layout.messageDetail.offsetBy(dx: 0, dy: shift)
            if allClear, !wasAllClear, !Motion.reduceMotion {
                Motion.ease(check, "strokeEnd", from: 0, to: 1, duration: 0.7, begin: CACurrentMediaTime() + 0.2)
            }
        case let .list(heroModel, dotModels, rowModels, moreText):
            message.isHidden = true
            setList(visible: true)
            heroLabel.string = heroModel.label.uppercased()
            countdown.string = Self.countdownText(heroModel.countdown, color: Self.color(heroModel.severity))
            heroTitle.string = heroModel.title
            heroCourse.string = heroModel.course
            heroDue.string = heroModel.due
            updateDots(dotModels)
            for (index, row) in rows.enumerated() {
                let data = index < rowModels.count ? rowModels[index] : nil
                for layer in [row.icon, row.title, row.due] as [CALayer] { layer.isHidden = data == nil }
                guard let data else { continue }
                let tint = data.module == .quiz ? Self.critical : data.module == .assignment ? Self.watch : NSColor.white
                row.icon.backgroundColor = tint.withAlphaComponent(data.module == .forum || data.module == .other ? 0.1 : 0.18).cgColor
                row.glyph.fillColor = tint.withAlphaComponent(data.module == .forum || data.module == .other ? 0.8 : 1).cgColor
                row.glyph.path = Self.icon(data.module).path(side: row.glyph.bounds.width)
                row.title.string = Self.fit(Self.rowTitle(data), width: row.title.bounds.width)
                row.due.string = data.due
                row.due.foregroundColor = (data.severity == .calm ? NSColor.white.withAlphaComponent(0.6) : Self.color(data.severity)).cgColor
            }
            more.string = moreText
            overdueToggle.string = model.overdueToggle
            // Red while there is overdue work to reveal; quiet once it shows.
            let revealing = model.overdueToggle.hasPrefix("Show")
            overdueToggle.foregroundColor = (revealing ? Self.critical : NSColor.white.withAlphaComponent(0.5)).cgColor
        }
        CATransaction.commit()
    }

    /// The row under the pointer, if any, lights up with its dot on the runway.
    func setHover(row: Int?) {
        guard row != hoveredRow else { return }
        hoveredRow = row
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        if let row {
            hoverPlate.frame = layout.row(row)
            hoverPlate.isHidden = false
        } else {
            hoverPlate.isHidden = true
        }
        var id: Int?
        if let row, case let .list(_, _, rows, _) = shown?.content, row < rows.count { id = rows[row].id }
        if hoveredDot != id {
            if let old = hoveredDot.flatMap({ dots[$0] }) {
                Motion.ease(old, "transform.scale", from: 1.4, to: 1, duration: 0.2, begin: CACurrentMediaTime())
                old.transform = CATransform3DIdentity
            }
            if let new = id.flatMap({ dots[$0] }) {
                Motion.ease(new, "transform.scale", from: 1, to: 1.4, duration: 0.2, begin: CACurrentMediaTime())
                new.transform = CATransform3DMakeScale(1.4, 1.4, 1)
            }
            hoveredDot = id
        }
        CATransaction.commit()
    }

    /// Staggers the groups in from `begin`; dots drop onto the runway one after another.
    func reveal(begin: CFTimeInterval) {
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        Motion.reveal(revealGroups, begin: begin)
        if !Motion.reduceMotion, case .list = shown?.content {
            let sorted = dots.values.sorted { $0.position.x < $1.position.x }
            for (index, dot) in sorted.enumerated() {
                let start = begin + Motion.revealStagger + Double(index) * 0.03
                Motion.ease(dot, "opacity", from: 0, to: 1, duration: 0.25, begin: start)
                Motion.ease(dot, "transform.translation.y", from: -6, to: 0, duration: 0.35, begin: start)
            }
        }
        CATransaction.commit()
    }

    private func setList(visible: Bool) {
        hero.isHidden = !visible
        runway.isHidden = !visible
        list.isHidden = !visible
    }

    private func updateDots(_ models: [TasksPageModel.Dot]) {
        let ids = Set(models.map(\.id))
        for (id, dot) in dots where !ids.contains(id) {
            dot.removeFromSuperlayer()
            dots[id] = nil
        }
        // Later dots are drawn over earlier ones, so a stack shows its top.
        for model in models {
            let dot = dots[model.id] ?? {
                let layer = CALayer()
                layer.bounds = CGRect(x: 0, y: 0, width: TasksLayout.dot + 3, height: TasksLayout.dot + 3)
                layer.cornerRadius = layer.bounds.width / 2
                layer.borderColor = NSColor.black.cgColor
                layer.borderWidth = 1.5
                runway.addSublayer(layer)
                dots[model.id] = layer
                return layer
            }()
            dot.position = layout.dotCenter(fraction: model.fraction, stack: model.stack)
            dot.backgroundColor = (model.severity == .calm ? NSColor.white.withAlphaComponent(0.85) : Self.color(model.severity)).cgColor
            dot.borderColor = (model.isNew ? Self.fresh : .black).cgColor
            dot.borderWidth = model.isNew ? 2 : 1.5
        }
    }

    private func place(_ layer: CATextLayer, _ frame: CGRect, in group: CALayer) {
        layer.frame = frame
        layer.contentsScale = scale
        group.addSublayer(layer)
    }

    static func color(_ severity: Severity) -> NSColor {
        switch severity {
        case .calm: .white
        case .watch: watch
        case .critical: critical
        }
    }

    static func icon(_ module: TaskModule) -> HomeIcon {
        switch module {
        case .assignment: .assignment
        case .quiz: .quiz
        case .forum, .other: .lyrics
        }
    }

    /// "1d 2h" with the numbers large and the units smaller and dimmer.
    private static func countdownText(_ pieces: [TasksPageModel.Piece], color: NSColor) -> NSAttributedString {
        let text = NSMutableAttributedString()
        let digits = NSFont.monospacedDigitSystemFont(ofSize: 38, weight: .semibold)
        let unit = NSFont.systemFont(ofSize: 20, weight: .medium)
        for (index, piece) in pieces.enumerated() {
            text.append(NSAttributedString(string: "\(piece.value)", attributes: [.font: digits, .foregroundColor: color]))
            let suffix = piece.unit + (index < pieces.count - 1 ? " " : "")
            text.append(NSAttributedString(string: suffix, attributes: [.font: unit, .foregroundColor: color.withAlphaComponent(0.8)]))
        }
        return text
    }

    private static func rowTitle(_ row: TasksPageModel.Row) -> NSAttributedString {
        let text = NSMutableAttributedString()
        if row.isNew {
            text.append(NSAttributedString(string: "● ", attributes: [.font: NSFont.systemFont(ofSize: 7), .foregroundColor: fresh, .baselineOffset: 1.5]))
        }
        text.append(NSAttributedString(string: row.title, attributes: [.font: NSFont.systemFont(ofSize: 11.5, weight: .medium), .foregroundColor: NSColor.white]))
        text.append(NSAttributedString(string: "  " + row.course, attributes: [.font: NSFont.systemFont(ofSize: 10.5), .foregroundColor: NSColor.white.withAlphaComponent(0.45)]))
        return text
    }

    /// Cut to `width` with "…". A centred `CATextLayer` draws nothing at all when an attributed
    /// string is wider than it, so styled text is fitted before it is set.
    static func fit(_ text: NSAttributedString, width: CGFloat) -> NSAttributedString {
        guard text.size().width > width, text.length > 1 else { return text }
        let result = NSMutableAttributedString(attributedString: text)
        let ellipsisAttributes = text.attributes(at: text.length - 1, effectiveRange: nil)
        var low = 0
        var high = text.length
        // The longest prefix that fits with the ellipsis.
        while low < high {
            let middle = (low + high + 1) / 2
            let candidate = NSMutableAttributedString(attributedString: text.attributedSubstring(from: NSRange(location: 0, length: middle)))
            candidate.append(NSAttributedString(string: "…", attributes: ellipsisAttributes))
            if candidate.size().width <= width { low = middle } else { high = middle - 1 }
        }
        result.replaceCharacters(in: NSRange(location: low, length: text.length - low), with: NSAttributedString(string: "…", attributes: ellipsisAttributes))
        return result
    }

    private static func text(size: CGFloat, weight: NSFont.Weight, opacity: Float = 1, alignment: CATextLayerAlignmentMode = .left) -> CATextLayer {
        let layer = CATextLayer()
        layer.font = NSFont.systemFont(ofSize: size, weight: weight)
        layer.fontSize = size
        layer.foregroundColor = NSColor.white.cgColor
        layer.opacity = opacity
        layer.alignmentMode = alignment
        layer.truncationMode = .end
        return layer
    }
}
