import AppKit
import QuartzCore

/// The calendar panel's layers, beside the music in Home: three days between two arrows, with a plate
/// on the chosen one, and that day's events as a short timeline coloured by calendar. Moodle
/// deadlines are rings in the tasks' own colour. It draws what it is given and
/// decides nothing. Choosing a day springs the plate across and crossfades the rows.
final class CalendarScene {
    /// Added by Home as one of its reveal groups.
    let root = CALayer()

    private static let todayColor = NSColor(srgbRed: 1, green: 0.42, blue: 0.38, alpha: 1)

    private let layout: HomeLayout
    private let divider = CALayer()
    private let plate = CALayer()
    private let hoverPlate = CALayer()
    private let letters: [CATextLayer]
    private let numbers: [CATextLayer]
    private let dots: [CALayer]
    private let rings: [CALayer]
    private let arrows = [CalendarScene.makeText(size: 15, weight: .medium, opacity: 0.5, alignment: .center),
                          CalendarScene.makeText(size: 15, weight: .medium, opacity: 0.5, alignment: .center)]
    private let list = CALayer()
    private let heading = CalendarScene.makeText(size: 11, weight: .medium, opacity: 0.5)
    private let more = CalendarScene.makeText(size: 11, weight: .medium, opacity: 0.5, alignment: .right)
    private let bars: [CALayer]
    private let times: [CATextLayer]
    private let titles: [CATextLayer]
    private let messagePlate = CALayer()
    private let message = CalendarScene.makeText(size: 12, weight: .medium, opacity: 1)
    private var shown: CalendarModel?

    init(layout: HomeLayout, scale: CGFloat) {
        self.layout = layout
        root.frame = CGRect(origin: .zero, size: layout.size)

        divider.frame = layout.calendarDivider
        divider.backgroundColor = NSColor.white.withAlphaComponent(0.1).cgColor
        root.addSublayer(divider)

        let side: CGFloat = 20
        plate.bounds = CGRect(x: 0, y: 0, width: side, height: side)
        plate.cornerRadius = side / 2
        plate.backgroundColor = NSColor.white.cgColor
        plate.position = Self.numberCenter(in: layout.calendarDay(0))
        root.addSublayer(plate)
        hoverPlate.bounds = plate.bounds
        hoverPlate.cornerRadius = side / 2
        hoverPlate.backgroundColor = NSColor.white.withAlphaComponent(0.14).cgColor
        hoverPlate.isHidden = true
        root.insertSublayer(hoverPlate, below: plate)

        let count = CalendarModel.dayCount
        letters = (0..<count).map { _ in Self.makeText(size: 10, weight: .medium, opacity: 0.45, alignment: .center) }
        numbers = (0..<count).map { _ in Self.makeText(size: 12, weight: .semibold, opacity: 1, alignment: .center) }
        dots = (0..<count).map { _ in CALayer() }
        rings = (0..<count).map { _ in CALayer() }
        for index in 0..<count {
            let cell = layout.calendarDay(index)
            letters[index].frame = CGRect(x: cell.minX, y: cell.minY, width: cell.width, height: 12)
            let center = Self.numberCenter(in: cell)
            numbers[index].frame = CGRect(x: cell.minX, y: center.y - 8, width: cell.width, height: 16)
            dots[index].bounds = CGRect(x: 0, y: 0, width: 3, height: 3)
            dots[index].cornerRadius = 1.5
            dots[index].backgroundColor = NSColor.white.withAlphaComponent(0.45).cgColor
            rings[index].bounds = CGRect(x: 0, y: 0, width: 5, height: 5)
            rings[index].cornerRadius = 2.5
            rings[index].borderWidth = 1.2
            rings[index].borderColor = TasksScene.accent.cgColor
            for layer in [letters[index], numbers[index]] {
                layer.contentsScale = scale
                root.addSublayer(layer)
            }
            root.addSublayer(dots[index])
            root.addSublayer(rings[index])
        }
        for (arrow, frame, glyph) in [(arrows[0], layout.calendarPrevious, "‹"), (arrows[1], layout.calendarNext, "›")] {
            arrow.frame = frame
            arrow.string = glyph
            arrow.contentsScale = scale
            root.addSublayer(arrow)
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
        let moved = previous.map { $0.days.map(\.number) != model.days.map(\.number) } ?? false
        for (index, day) in model.days.enumerated() where index < letters.count {
            if moved {
                // The strip moved by its length: the dates crossfade in place.
                for layer in [letters[index], numbers[index]] {
                    let fade = CATransition()
                    fade.type = .fade
                    fade.duration = 0.2
                    layer.add(fade, forKey: "fade")
                }
            }
            letters[index].string = day.letter
            numbers[index].string = day.number
            // An event's dot and a deadline's ring share the spot under the date.
            let cell = layout.calendarDay(index)
            let both = day.hasEvents && day.hasDeadline
            dots[index].position = CGPoint(x: cell.midX - (both ? 3.5 : 0), y: cell.maxY - 1.5)
            rings[index].position = CGPoint(x: cell.midX + (both ? 3.5 : 0), y: cell.maxY - 1.5)
            rings[index].isHidden = !day.hasDeadline
            let selected = index == model.selected
            // On its plate the date is dark, or white on today's red one.
            let color: NSColor = selected ? (day.isToday ? .white : .black) : day.isToday ? Self.todayColor : .white
            numbers[index].foregroundColor = color.cgColor
            dots[index].isHidden = !day.hasEvents
        }
        let target = Self.numberCenter(in: layout.calendarDay(model.selected))
        if let previous, previous.selected != model.selected, !Motion.reduceMotion {
            let spring = CASpringAnimation(perceptualDuration: 0.35, bounce: 0.15)
            spring.keyPath = "position"
            spring.fromValue = NSValue(point: (plate.presentation() ?? plate).position)
            spring.toValue = NSValue(point: target)
            spring.duration = spring.settlingDuration
            plate.add(spring, forKey: "position")
        }
        plate.position = target
        // A plate on today that isn't chosen would hide the red date, so today is told by colour.
        plate.backgroundColor = (model.days.indices.contains(model.selected) && model.days[model.selected].isToday ? Self.todayColor : .white).cgColor

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

    /// `day` is the day under the pointer, if any.
    func setHover(day: Int?) {
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        if let day, day != shown?.selected {
            hoverPlate.position = Self.numberCenter(in: layout.calendarDay(day))
            hoverPlate.isHidden = false
        } else {
            hoverPlate.isHidden = true
        }
        CATransaction.commit()
    }

    private static func numberCenter(in cell: CGRect) -> CGPoint {
        CGPoint(x: cell.midX, y: cell.minY + 22)
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
