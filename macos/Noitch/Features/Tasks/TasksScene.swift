import AppKit
import QuartzCore

/// The tasks page's layers: the next task as a countdown, the next seven days as bars of how much is
/// due, and a short list. It draws what it is given and decides nothing.
///
/// One accent, indigo, on white and greys; red only for what is due within a day or overdue.
final class TasksScene {
    /// Placed by the pager, which owns its fade and slide.
    let root = CALayer()

    /// The tasks' own colour, also used for deadlines in Home's calendar.
    static let accent = NSColor(srgbRed: 0.49, green: 0.53, blue: 1, alpha: 1)
    static let critical = NSColor(srgbRed: 1, green: 0.42, blue: 0.42, alpha: 1)
    /// New tasks are marked in the accent, so nothing on the page competes with it.
    static let fresh = accent

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

    private let week = CALayer()
    private let bars: [CALayer]
    private let barLetters: [CATextLayer]
    private let caption = TasksScene.text(size: 11, weight: .semibold)
    private let captionDetail = TasksScene.text(size: 10.5, weight: .regular, opacity: 0.5)

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
    private var hoveredBar: Int?

    init(layout: TasksLayout, scale: CGFloat) {
        self.layout = layout
        self.scale = scale
        root.bounds = CGRect(origin: .zero, size: layout.size)
        bars = (0..<TaskRule.loadDays).map { _ in CALayer() }
        barLetters = (0..<TaskRule.loadDays).map { _ in TasksScene.text(size: 8.5, weight: .medium, opacity: 0.4, alignment: .center) }
        rows = (0..<layout.visibleRows).map { _ in
            (CALayer(), CAShapeLayer(), TasksScene.text(size: 11.5, weight: .medium), TasksScene.text(size: 10.5, weight: .regular, alignment: .right))
        }
        for group in [hero, week, list, message] {
            group.frame = root.bounds
            root.addSublayer(group)
        }

        place(heroLabel, layout.heroLabel, in: hero)
        place(countdown, layout.countdown, in: hero)
        place(heroTitle, layout.heroTitle, in: hero)
        heroCourse.backgroundColor = Self.accent.withAlphaComponent(0.22).cgColor
        heroCourse.cornerRadius = 4
        place(heroCourse, layout.heroCourse, in: hero)
        place(heroDue, layout.heroDue, in: hero)
        openButton.frame = layout.openButton
        openButton.backgroundColor = NSColor.white.cgColor
        openButton.cornerRadius = layout.openButton.height / 2
        hero.addSublayer(openButton)
        openLabel.foregroundColor = NSColor.black.cgColor
        place(openLabel, layout.openButton.insetBy(dx: 0, dy: 3.5), in: hero)

        for (day, bar) in bars.enumerated() {
            // Grows from its foot, so a new height or the reveal rises out of the line.
            bar.anchorPoint = CGPoint(x: 0.5, y: 1)
            bar.frame = layout.bar(day, share: 0)
            bar.cornerRadius = 3
            bar.cornerCurve = .continuous
            week.addSublayer(bar)
            place(barLetters[day], layout.barLetter(day), in: week)
        }
        place(caption, layout.loadCaption, in: week)
        place(captionDetail, layout.loadDetail, in: week)

        hoverPlate.backgroundColor = NSColor.white.withAlphaComponent(0.08).cgColor
        hoverPlate.cornerRadius = 7
        hoverPlate.isHidden = true
        list.addSublayer(hoverPlate)
        for (index, row) in rows.enumerated() {
            row.icon.frame = layout.rowIcon(index)
            row.icon.cornerRadius = 5
            row.icon.backgroundColor = Self.accent.withAlphaComponent(0.18).cgColor
            list.addSublayer(row.icon)
            row.glyph.frame = row.icon.bounds.insetBy(dx: 3, dy: 3)
            row.glyph.fillRule = .evenOdd
            row.glyph.fillColor = Self.accent.cgColor
            row.glyph.contentsScale = scale
            row.icon.addSublayer(row.glyph)
            place(row.title, layout.rowTitle(index), in: list)
            place(row.due, layout.rowDue(index), in: list)
        }
        place(more, layout.more, in: list)
        place(overdueToggle, layout.overdueToggle, in: list)

        check.frame = layout.check
        let side = layout.check.width
        let path = CGMutablePath()
        path.addEllipse(in: check.bounds.insetBy(dx: 1, dy: 1))
        path.move(to: CGPoint(x: side * 0.3, y: side * 0.52))
        path.addLine(to: CGPoint(x: side * 0.45, y: side * 0.66))
        path.addLine(to: CGPoint(x: side * 0.72, y: side * 0.37))
        check.path = path
        check.fillColor = nil
        check.strokeColor = Self.accent.cgColor
        check.lineWidth = 2
        check.lineCap = .round
        check.lineJoin = .round
        check.contentsScale = scale
        message.addSublayer(check)
        place(messageTitle, layout.message(withCheck: false), in: message)
        place(messageDetail, layout.messageDetail(withCheck: false), in: message)
        messageButton.backgroundColor = NSColor.white.cgColor
        message.addSublayer(messageButton)
        messageButtonLabel.foregroundColor = NSColor.black.cgColor
        place(messageButtonLabel, layout.messageButton(withCheck: false), in: message)
    }

    /// The groups to stagger in, in order.
    var revealGroups: [CALayer] {
        if case .message = shown?.content { return [message] }
        return [hero, week, list]
    }

    func apply(_ model: TasksPageModel) {
        guard model != shown else { return }
        let wasAllClear = { if case let .message(_, _, _, clear) = shown?.content { return clear } else { return false } }()
        let previousLoad: [TasksPageModel.DayLoad]? = { if case let .list(_, load, _, _) = shown?.content { return load } else { return nil } }()
        shown = model
        let now = CACurrentMediaTime()
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        switch model.content {
        case let .message(title, detail, button, allClear):
            setList(visible: false)
            message.isHidden = false
            messageTitle.string = title
            messageDetail.string = detail
            messageTitle.frame = layout.message(withCheck: allClear)
            messageDetail.frame = layout.messageDetail(withCheck: allClear)
            let buttonFrame = layout.messageButton(withCheck: allClear)
            messageButton.frame = buttonFrame
            messageButton.cornerRadius = buttonFrame.height / 2
            messageButtonLabel.frame = buttonFrame.insetBy(dx: 0, dy: 4)
            messageButton.isHidden = button == nil
            messageButtonLabel.isHidden = button == nil
            messageButtonLabel.string = button
            check.isHidden = !allClear
            if allClear, !wasAllClear, !Motion.reduceMotion {
                Motion.ease(check, "strokeEnd", from: 0, to: 1, duration: 0.7, begin: now + 0.2)
            }
        case let .list(heroModel, load, rowModels, moreText):
            message.isHidden = true
            setList(visible: true)
            heroLabel.string = heroModel.label.uppercased()
            countdown.string = Self.countdownText(heroModel.countdown, color: Self.color(heroModel.severity))
            heroTitle.string = heroModel.title
            heroCourse.string = heroModel.course
            heroDue.string = heroModel.due
            openLabel.string = "Open in \(model.siteName) ↗"
            applyLoad(load, previous: previousLoad, now: now)
            caption.string = model.loadCaption[0]
            captionDetail.string = model.loadCaption[1]
            for (index, row) in rows.enumerated() {
                let data = index < rowModels.count ? rowModels[index] : nil
                for layer in [row.icon, row.title, row.due] as [CALayer] { layer.isHidden = data == nil }
                guard let data else { continue }
                row.glyph.path = Self.icon(data.module).path(side: row.glyph.bounds.width)
                row.title.string = Self.fit(Self.rowTitle(data), width: row.title.bounds.width)
                row.due.string = data.due
                row.due.foregroundColor = (data.severity == .critical ? Self.critical : NSColor.white.withAlphaComponent(0.6)).cgColor
            }
            more.string = moreText
            overdueToggle.string = model.overdueToggle
            // Red while there is overdue work to reveal; quiet once it shows.
            let revealing = model.overdueToggle.hasPrefix("Show")
            overdueToggle.foregroundColor = (revealing ? Self.critical : NSColor.white.withAlphaComponent(0.5)).cgColor
        }
        CATransaction.commit()
    }

    /// The busiest day is the accent, a day with something due within a day is red, the rest grey.
    private func applyLoad(_ load: [TasksPageModel.DayLoad], previous: [TasksPageModel.DayLoad]?, now: CFTimeInterval) {
        let most = max(load.map(\.count).max() ?? 0, 1)
        for (day, data) in load.enumerated() where day < bars.count {
            let bar = bars[day]
            let target = layout.bar(day, share: CGFloat(data.count) / CGFloat(most))
            if let previous, day < previous.count, previous[day].count != data.count, !Motion.reduceMotion {
                let from = (bar.presentation() ?? bar).bounds.height
                Motion.ease(bar, "bounds.size.height", from: from, to: target.height, duration: 0.4, begin: now)
            }
            bar.bounds = CGRect(origin: .zero, size: target.size)
            bar.position = CGPoint(x: target.midX, y: target.maxY)
            let color: NSColor = data.count == 0 ? NSColor.white.withAlphaComponent(0.12)
                : data.hasUrgent ? Self.critical
                : data.isBusiest ? Self.accent
                : NSColor.white.withAlphaComponent(0.45)
            bar.backgroundColor = color.cgColor
            bar.borderWidth = data.hasNew ? 1.5 : 0
            bar.borderColor = NSColor.white.cgColor
            barLetters[day].string = data.letter
            barLetters[day].opacity = day == 0 ? 0.9 : 0.4
        }
    }

    /// The row under the pointer, if any, lights up with its day's bar.
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
        var day: Int?
        if let row, case let .list(_, _, rows, _) = shown?.content, row < rows.count { day = rows[row].day }
        if hoveredBar != day {
            let now = CACurrentMediaTime()
            for (index, bar) in bars.enumerated() where index == hoveredBar || index == day {
                let to: CGFloat = index == day ? 1.25 : 1
                Motion.ease(bar, "transform.scale.x", from: (bar.presentation() ?? bar).value(forKeyPath: "transform.scale.x") ?? 1, to: to, duration: 0.2, begin: now)
                bar.transform = CATransform3DMakeScale(to, 1, 1)
            }
            hoveredBar = day
        }
        CATransaction.commit()
    }

    /// Staggers the groups in from `begin`; the bars rise out of their line one after another.
    func reveal(begin: CFTimeInterval) {
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        Motion.reveal(revealGroups, begin: begin)
        if !Motion.reduceMotion, case .list = shown?.content {
            for (index, bar) in bars.enumerated() {
                let start = begin + Motion.revealStagger + Double(index) * 0.035
                Motion.ease(bar, "transform.scale.y", from: 0.05, to: 1, duration: 0.45, begin: start)
            }
        }
        CATransaction.commit()
    }

    private func setList(visible: Bool) {
        hero.isHidden = !visible
        week.isHidden = !visible
        list.isHidden = !visible
    }

    private func place(_ layer: CATextLayer, _ frame: CGRect, in group: CALayer) {
        layer.frame = frame
        layer.contentsScale = scale
        group.addSublayer(layer)
    }

    /// White unless it is due within a day or overdue.
    static func color(_ severity: Severity) -> NSColor {
        severity == .critical ? critical : .white
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
            text.append(NSAttributedString(string: suffix, attributes: [.font: unit, .foregroundColor: color.withAlphaComponent(0.6)]))
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
