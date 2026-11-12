import AppKit
import QuartzCore

/// The calendar panel's layers, beside the music in Home: three days set in type between two thin
/// chevrons, and the chosen day's events as a short timeline coloured by calendar. No boxes: each day
/// is its weekday over a large date, with a dot for events and an indigo ring for Moodle deadlines
/// under it. The chosen day is bright white, with a short white line over it that
/// glides to a clicked day; the others are dim. The chevrons slide the days in from the side they
/// point to. It draws what it is given and decides nothing.
final class CalendarScene {
    /// Added by Home as one of its reveal groups.
    let root = CALayer()

    private static let underlineWidth: CGFloat = 18

    private let layout: HomeLayout
    private let divider = CALayer()
    /// The line over the chosen day, which glides between days.
    private let underline = CALayer()
    /// The days' type, which slides when the strip moves.
    private let strip = CALayer()
    private let letters: [CATextLayer]
    private let numbers: [CATextLayer]
    private let dots: [CALayer]
    private let rings: [CALayer]
    private let arrows = [CAShapeLayer(), CAShapeLayer()]
    private let list = CALayer()
    private let heading = CalendarScene.makeText(size: 11, weight: .medium, opacity: 0.5)
    private let more = CalendarScene.makeText(size: 11, weight: .medium, opacity: 0.5, alignment: .right)
    private let bars: [CALayer]
    private let times: [CATextLayer]
    private let titles: [CATextLayer]
    private let messagePlate = CALayer()
    private let message = CalendarScene.makeText(size: 12, weight: .medium, opacity: 1)
    private var shown: CalendarModel?
    private var hovered: Int?

    init(layout: HomeLayout, scale: CGFloat) {
        self.layout = layout
        root.frame = CGRect(origin: .zero, size: layout.size)

        divider.frame = layout.calendarDivider
        divider.backgroundColor = NSColor.white.withAlphaComponent(0.1).cgColor
        root.addSublayer(divider)

        let count = CalendarModel.dayCount
        // Clipped to the days' row, so sliding dates don't show past the chevrons.
        let first = layout.calendarDay(0)
        let last = layout.calendarDay(count - 1)
        strip.frame = CGRect(x: first.minX, y: first.minY, width: last.maxX - first.minX, height: first.height)
        strip.masksToBounds = true
        root.addSublayer(strip)
        letters = (0..<count).map { _ in Self.makeText(size: 9, weight: .semibold, opacity: 1, alignment: .center) }
        numbers = (0..<count).map { _ in Self.makeText(size: 20, weight: .semibold, opacity: 1, alignment: .center) }
        dots = (0..<count).map { _ in CALayer() }
        rings = (0..<count).map { _ in CALayer() }
        for index in 0..<count {
            let day = layout.calendarDay(index).offsetBy(dx: -strip.frame.minX, dy: -strip.frame.minY)
            letters[index].frame = CGRect(x: day.minX, y: day.minY + 6, width: day.width, height: 11)
            numbers[index].frame = CGRect(x: day.minX, y: day.minY + 16, width: day.width, height: 24)
            dots[index].bounds = CGRect(x: 0, y: 0, width: 4, height: 4)
            dots[index].cornerRadius = 2
            dots[index].backgroundColor = NSColor.white.withAlphaComponent(0.5).cgColor
            rings[index].bounds = CGRect(x: 0, y: 0, width: 5, height: 5)
            rings[index].cornerRadius = 2.5
            rings[index].borderWidth = 1.2
            rings[index].borderColor = TasksScene.accent.cgColor
            for layer in [letters[index], numbers[index]] {
                layer.contentsScale = scale
                strip.addSublayer(layer)
            }
            strip.addSublayer(dots[index])
            strip.addSublayer(rings[index])
        }
        // Hairlines between the days, so the three read as a grid of columns.
        for index in 1..<count {
            let left = layout.calendarDay(index - 1)
            let right = layout.calendarDay(index)
            let rule = CALayer()
            rule.frame = CGRect(x: ((left.maxX + right.minX) / 2).rounded() - 0.5, y: left.minY + 8, width: 1, height: left.height - 12)
            rule.backgroundColor = NSColor.white.withAlphaComponent(0.08).cgColor
            root.addSublayer(rule)
        }
        underline.bounds = CGRect(x: 0, y: 0, width: Self.underlineWidth, height: 2)
        underline.cornerRadius = 1
        underline.position = Self.underlineCenter(in: layout.calendarDay(0))
        root.addSublayer(underline)
        for (index, frame) in [layout.calendarPrevious, layout.calendarNext].enumerated() {
            // A thin chevron, drawn, so it sits exactly level with the dates.
            let chevron = CGMutablePath()
            let direction: CGFloat = index == 0 ? -1 : 1
            let mid = CGPoint(x: frame.width / 2, y: frame.height / 2)
            chevron.move(to: CGPoint(x: mid.x - direction * 2.5, y: mid.y - 5))
            chevron.addLine(to: CGPoint(x: mid.x + direction * 2.5, y: mid.y))
            chevron.addLine(to: CGPoint(x: mid.x - direction * 2.5, y: mid.y + 5))
            arrows[index].frame = frame
            arrows[index].path = chevron
            arrows[index].fillColor = nil
            arrows[index].strokeColor = NSColor.white.withAlphaComponent(0.4).cgColor
            arrows[index].lineWidth = 1.5
            arrows[index].lineCap = .round
            arrows[index].lineJoin = .round
            arrows[index].contentsScale = scale
            root.addSublayer(arrows[index])
        }

        list.frame = root.bounds
        root.addSublayer(list)
        for (layer, frame) in [(heading, layout.calendarHeading), (more, layout.calendarMore)] {
            layer.frame = frame
            layer.contentsScale = scale
            list.addSublayer(layer)
        }
        bars = (0..<CalendarModel.maxRows).map { _ in CALayer() }
        times = (0..<CalendarModel.maxRows).map { _ in Self.makeText(size: 11, weight: .regular, opacity: 0.5) }
        titles = (0..<CalendarModel.maxRows).map { _ in Self.makeText(size: 12, weight: .regular, opacity: 1) }
        for index in 0..<CalendarModel.maxRows {
            let row = layout.calendarRow(index)
            bars[index].frame = CGRect(x: row.minX, y: row.minY + 3, width: 2.5, height: row.height - 6)
            bars[index].cornerRadius = 1.25
            times[index].frame = CGRect(x: row.minX + 8, y: row.minY + 2, width: 38, height: row.height - 2)
            titles[index].frame = CGRect(x: row.minX + 47, y: row.minY + 1, width: row.width - 47, height: row.height - 1)
            list.addSublayer(bars[index])
            for layer in [times[index], titles[index]] {
                layer.contentsScale = scale
                list.addSublayer(layer)
            }
        }
        let action = layout.calendarMessage
        messagePlate.frame = CGRect(x: action.minX, y: action.minY + 2, width: action.width, height: 24)
        messagePlate.cornerRadius = 12
        messagePlate.backgroundColor = NSColor.white.withAlphaComponent(0.1).cgColor
        list.addSublayer(messagePlate)
        message.frame = CGRect(x: action.minX + 10, y: action.minY + 7, width: action.width - 20, height: 16)
        message.contentsScale = scale
        list.addSublayer(message)
    }

    func apply(_ model: CalendarModel) {
        guard model != shown else { return }
        let previous = shown
        shown = model
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        let step = previous.map { model.rangeIndex - $0.rangeIndex } ?? 0
        let moved = step != 0
        if moved, !Motion.reduceMotion {
            // The new days push in from the side the arrow pointed to.
            let push = CATransition()
            push.type = .push
            push.subtype = step > 0 ? .fromRight : .fromLeft
            push.duration = 0.32
            push.timingFunction = Motion.easeOut
            strip.add(push, forKey: "push")
        }
        for (index, day) in model.days.enumerated() where index < letters.count {
            letters[index].string = day.letter
            numbers[index].string = day.number
            let selected = index == model.selected
            // All in white: the chosen day is bright and the rest dim. The heading under the days
            // already says "Today".
            numbers[index].foregroundColor = NSColor.white.cgColor
            letters[index].foregroundColor = NSColor.white.cgColor
            numbers[index].opacity = selected ? 1 : 0.5
            letters[index].opacity = selected ? 0.9 : 0.4
            // Markers sit under the date.
            let cell = layout.calendarDay(index).offsetBy(dx: -strip.frame.minX, dy: -strip.frame.minY)
            let both = day.hasEvents && day.hasDeadline
            dots[index].position = CGPoint(x: cell.midX - (both ? 4 : 0), y: cell.maxY - 2.5)
            rings[index].position = CGPoint(x: cell.midX + (both ? 4 : 0), y: cell.maxY - 2.5)
            dots[index].isHidden = !day.hasEvents
            rings[index].isHidden = !day.hasDeadline
        }
        let target = Self.underlineCenter(in: layout.calendarDay(model.selected))
        if let previous, previous.selected != model.selected, !Motion.reduceMotion {
            let spring = CASpringAnimation(perceptualDuration: 0.35, bounce: 0.15)
            spring.keyPath = "position"
            spring.fromValue = NSValue(point: (underline.presentation() ?? underline).position)
            spring.toValue = NSValue(point: target)
            spring.duration = spring.settlingDuration
            underline.add(spring, forKey: "position")
        }
        underline.position = target
        underline.backgroundColor = NSColor.white.cgColor
        showHover()

        if previous != nil, moved || previous?.selected != model.selected || previous?.rows != model.rows {
            let fade = CATransition()
            fade.type = .fade
            fade.duration = 0.2
            list.add(fade, forKey: "fade")
        }
        heading.string = model.heading
        more.string = model.more ?? ""
        for index in 0..<CalendarModel.maxRows {
            let row = model.rows.indices.contains(index) ? model.rows[index] : nil
            bars[index].isHidden = row == nil
            times[index].string = row?.time ?? ""
            titles[index].string = row?.title ?? ""
            guard let row else { continue }
            let color = row.isDeadline ? TasksScene.accent : row.color.map { NSColor(srgbRed: $0[0], green: $0[1], blue: $0[2], alpha: 1) } ?? .white
            let rowFrame = layout.calendarRow(index)
            if row.isDeadline {
                // A ring, not a bar: a deadline is a moment, not a span.
                bars[index].frame = CGRect(x: rowFrame.minX - 1.5, y: rowFrame.midY - 3, width: 6, height: 6)
                bars[index].cornerRadius = 3
                bars[index].backgroundColor = nil
                bars[index].borderWidth = 1.5
                bars[index].borderColor = color.cgColor
            } else {
                bars[index].frame = CGRect(x: rowFrame.minX, y: rowFrame.minY + 3, width: row.isNow ? 3.5 : 2.5, height: rowFrame.height - 6)
                bars[index].cornerRadius = 1.25
                bars[index].borderWidth = 0
                bars[index].backgroundColor = color.cgColor
            }
            titles[index].opacity = row.isNow ? 1 : 0.85
            titles[index].font = NSFont.systemFont(ofSize: 12, weight: row.isNow ? .semibold : .regular)
            times[index].foregroundColor = (row.isNow || row.isDeadline ? color : .white).cgColor
            times[index].opacity = row.isNow || row.isDeadline ? 1 : 0.5
        }
        message.string = model.message ?? ""
        message.alignmentMode = model.messageIsAction ? .center : .left
        message.opacity = model.messageIsAction ? 1 : 0.5
        message.frame.origin.x = layout.calendarMessage.minX + (model.messageIsAction ? 10 : 9)
        messagePlate.isHidden = !model.messageIsAction
        CATransaction.commit()
    }

    /// `day` is the day under the pointer, if any: its tile lightens.
    func setHover(day: Int?) {
        hovered = day
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        showHover()
        CATransaction.commit()
    }

    /// A hovered day lifts toward bright, a hint that clicking it chooses it.
    private func showHover() {
        guard let shown else { return }
        for index in numbers.indices where index != shown.selected {
            numbers[index].opacity = index == hovered ? 0.8 : 0.5
        }
    }

    /// Over the weekday, like a tab marker.
    private static func underlineCenter(in day: CGRect) -> CGPoint {
        CGPoint(x: day.midX, y: day.minY + 1)
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
