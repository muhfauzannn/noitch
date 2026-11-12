import AppKit

/// Owns the calendar panel in Home: which day is chosen, the week's events, and when to look again.
/// Home tells it when it is shown, hidden and released. While shown, the only things that make it
/// look again are a calendar change, a click, and one one-shot timer at the next moment a row
/// would change (an event starting or ending, or midnight).
final class CalendarController: NSObject {
    private let source = CalendarSource()
    private var scene: CalendarScene?
    private var layout: HomeLayout?
    private var calendar = Calendar.current
    /// The first of the three days shown.
    private var rangeStart = Date()
    private var selected = 0
    private var events: [CalendarEvent] = []
    private var isShown = false
    private var timer: Timer?
    private var hovered: Int?

    /// The Moodle tasks still to do, so their deadlines show among the events.
    private let tasks: () -> [MoodleTask]

    init(tasks: @escaping () -> [MoodleTask]) {
        self.tasks = tasks
        super.init()
        source.onChange = { [unowned self] in reload() }
    }

    /// The task list changed.
    func tasksChanged() {
        if isShown { render() }
    }

    /// A new scene for a new Home layout. Its layers belong to Home's scene.
    func attach(_ scene: CalendarScene, layout: HomeLayout) {
        self.scene = scene
        self.layout = layout
        // A new scene comes with an opening, and its first text waits for the first look like the
        // read does: drawn here it cost about 50 ms of the opening's first frame, enough to open late.
        if isShown, timer == nil { render() }
    }

    func pageWillShow() {
        isShown = true
        NotificationCenter.default.addObserver(self, selector: #selector(accessChanged), name: CalendarSource.accessChanged, object: nil)
        // Every opening starts on today, as the first of the three days.
        calendar = Calendar.current
        rangeStart = calendar.startOfDay(for: Date())
        selected = 0
        // The store, the read and the first text all wait until the island has opened: the panel
        // reveals last anyway, and the opening should cost nothing extra.
        timer?.invalidate()
        let timer = Timer(timeInterval: Self.firstLookDelay, target: self, selector: #selector(firstLook), userInfo: nil, repeats: false)
        timer.tolerance = 0.02
        RunLoop.main.add(timer, forMode: .common)
        self.timer = timer
    }

    private static let firstLookDelay: TimeInterval = 0.2

    @objc private func firstLook() {
        timer = nil
        source.start()
        reload()
    }

    func pageWillHide() {
        isShown = false
        NotificationCenter.default.removeObserver(self, name: CalendarSource.accessChanged, object: nil)
        timer?.invalidate()
        timer = nil
        hovered = nil
        scene?.setHover(day: nil)
    }

    /// Nothing from the store outlives Home's layers.
    func release() {
        pageWillHide()
        source.stop()
        scene = nil
        layout = nil
        events = []
    }

    // MARK: Pointer

    func pointerMoved(to point: CGPoint) {
        let day = layout?.calendarDayIndex(at: point)
        guard day != hovered else { return }
        hovered = day
        scene?.setHover(day: day)
    }

    /// True when the click was the calendar's.
    func click(at point: CGPoint) -> Bool {
        guard let layout, layout.showsCalendar else { return false }
        for (frame, step) in [(layout.calendarPrevious, -1), (layout.calendarNext, 1)] where frame.insetBy(dx: -4, dy: -6).contains(point) {
            rangeStart = CalendarModel.rangeStart(rangeStart, moved: step, calendar: calendar)
            reload()
            return true
        }
        if let day = layout.calendarDayIndex(at: point) {
            selected = day
            render()
            scene?.setHover(day: nil)
            return true
        }
        if layout.calendarMessage.contains(point), access != .granted {
            source.requestAccess()
            return true
        }
        return false
    }

    // MARK: Showing

    @objc private func accessChanged() {
        source.start()
        reload()
    }

    #if DEBUG
    /// `--demo-media` also shows sample events, without asking for calendar access.
    private let isDemo = ProcessInfo.processInfo.arguments.contains("--demo-media")
    #else
    private let isDemo = false
    #endif

    private var access: CalendarAccess { isDemo ? .granted : source.access }

    private func reload() {
        guard isShown else { return }
        if isDemo {
            events = Self.demoEvents(from: rangeStart, calendar: calendar)
        } else {
            events = source.access == .granted ? source.events(from: rangeStart, calendar: calendar) : []
        }
        render()
    }

    private static func demoEvents(from start: Date, calendar: Calendar) -> [CalendarEvent] {
        let today = calendar.startOfDay(for: Date())
        func at(_ day: Date, _ hour: Int, _ minutes: Int, _ length: Double, _ title: String, _ color: [Double], allDay: Bool = false) -> CalendarEvent {
            let start = calendar.date(byAdding: .minute, value: hour * 60 + minutes, to: day) ?? day
            return CalendarEvent(title: title, start: start, end: start.addingTimeInterval(allDay ? 86400 : length * 3600), isAllDay: allDay, color: color)
        }
        let now = Date()
        let soon = calendar.date(byAdding: .minute, value: 25, to: now) ?? now
        let tomorrow = calendar.date(byAdding: .day, value: 1, to: today) ?? today
        return [
            CalendarEvent(title: "Focus block", start: now.addingTimeInterval(-1800), end: now.addingTimeInterval(1200), isAllDay: false, color: [0.49, 0.47, 0.87]),
            CalendarEvent(title: "Design review", start: soon, end: soon.addingTimeInterval(3600), isAllDay: false, color: [0.49, 0.47, 0.87]),
            at(today, 23, 0, 0.5, "Gym", [0.11, 0.62, 0.46]),
            at(today, 23, 30, 0.5, "Dinner with Rani", [0.85, 0.35, 0.19]),
            at(tomorrow, 9, 30, 0.5, "Standup", [0.49, 0.47, 0.87]),
            at(start, 0, 0, 0, "Public holiday", [0.11, 0.62, 0.46], allDay: true),
        ]
    }

    private func render() {
        guard let scene else { return }
        let now = Date()
        let deadlines = tasks().map { CalendarDeadline(title: $0.title, course: $0.course, due: Date(timeIntervalSince1970: $0.due)) }
        scene.apply(CalendarModel(
            events: events, deadlines: deadlines, rangeStart: rangeStart, selected: selected, now: now, calendar: calendar, access: access,
            clock: { Self.clock.string(from: $0) }, dayName: { Self.dayName.string(from: $0) }
        ))
        scheduleNextChange(after: now)
    }

    /// One shot at the next start, end or midnight, only while shown.
    private func scheduleNextChange(after now: Date) {
        timer?.invalidate()
        timer = nil
        guard isShown else { return }
        let midnight = calendar.date(byAdding: .day, value: 1, to: calendar.startOfDay(for: now)) ?? now.addingTimeInterval(3600)
        let deadlines = tasks().map { Date(timeIntervalSince1970: $0.due) }
        let next = (events.flatMap { [$0.start, $0.end] } + deadlines).filter { $0 > now }.min().map { min($0, midnight) } ?? midnight
        let timer = Timer(fireAt: next.addingTimeInterval(0.5), interval: 0, target: self, selector: #selector(nextChange), userInfo: nil, repeats: false)
        timer.tolerance = 5
        RunLoop.main.add(timer, forMode: .common)
        self.timer = timer
    }

    @objc private func nextChange() {
        timer = nil
        let now = Date()
        // Past midnight a strip that started on yesterday moves on to the new today.
        let today = calendar.startOfDay(for: now)
        if let yesterday = calendar.date(byAdding: .day, value: -1, to: today), rangeStart == yesterday, selected == 0 {
            rangeStart = today
            reload()
        } else {
            render()
        }
    }

    private static let clock: DateFormatter = {
        let formatter = DateFormatter()
        formatter.setLocalizedDateFormatFromTemplate("jj:mm")
        return formatter
    }()

    private static let dayName: DateFormatter = {
        let formatter = DateFormatter()
        formatter.setLocalizedDateFormatFromTemplate("EEE d")
        return formatter
    }()
}
