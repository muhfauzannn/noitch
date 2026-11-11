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
    private var weekStart = Date()
    private var selected = 0
    private var events: [CalendarEvent] = []
    private var isShown = false
    private var timer: Timer?
    private var hovered: Int?

    override init() {
        super.init()
        source.onChange = { [unowned self] in reload() }
    }

    /// A new scene for a new Home layout. Its layers belong to Home's scene.
    func attach(_ scene: CalendarScene, layout: HomeLayout) {
        self.scene = scene
        self.layout = layout
        render()
    }

    func pageWillShow() {
        isShown = true
        NotificationCenter.default.addObserver(self, selector: #selector(accessChanged), name: CalendarSource.accessChanged, object: nil)
        // Every opening starts on today, in the week that holds it.
        calendar = Calendar.current
        let now = Date()
        weekStart = CalendarModel.weekStart(for: now, calendar: calendar)
        selected = calendar.dateComponents([.day], from: weekStart, to: calendar.startOfDay(for: now)).day ?? 0
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
            events = Self.demoEvents(weekStart: weekStart, calendar: calendar)
        } else {
            events = source.access == .granted ? source.events(weekStart: weekStart, calendar: calendar) : []
        }
        render()
    }

    private static func demoEvents(weekStart: Date, calendar: Calendar) -> [CalendarEvent] {
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
            at(weekStart, 0, 0, 0, "Public holiday", [0.11, 0.62, 0.46], allDay: true),
        ]
    }

    private func render() {
        guard let scene else { return }
        let now = Date()
        scene.apply(CalendarModel(
            events: events, weekStart: weekStart, selected: selected, now: now, calendar: calendar, access: access,
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
        let next = events.flatMap { [$0.start, $0.end] }.filter { $0 > now }.min().map { min($0, midnight) } ?? midnight
        let timer = Timer(fireAt: next.addingTimeInterval(0.5), interval: 0, target: self, selector: #selector(nextChange), userInfo: nil, repeats: false)
        timer.tolerance = 5
        RunLoop.main.add(timer, forMode: .common)
        self.timer = timer
    }

    @objc private func nextChange() {
        timer = nil
        let now = Date()
        // Past midnight the week may have moved on; the chosen day stays today.
        let today = calendar.startOfDay(for: now)
        let week = CalendarModel.weekStart(for: now, calendar: calendar)
        if week != weekStart {
            weekStart = week
            selected = calendar.dateComponents([.day], from: week, to: today).day ?? 0
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
