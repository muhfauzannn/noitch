import Foundation

/// One event, reduced to what the panel shows. Pure values, so the day logic is tested without
/// EventKit.
struct CalendarEvent: Equatable {
    var title: String
    var start: Date
    var end: Date
    var isAllDay: Bool
    /// The calendar's colour as sRGB components, when it has one.
    var color: [Double]?
}

/// A Moodle task's deadline, shown among the day's events in the tasks' own colour.
struct CalendarDeadline: Equatable {
    var title: String
    var course: String
    var due: Date
}

/// Whether the panel may read calendars.
enum CalendarAccess: Equatable {
    /// Never asked: the panel offers to ask, rather than prompting as soon as Home opens.
    case notAsked
    case granted
    case denied
}

/// What the calendar panel shows, as values and text. The scene draws exactly this.
struct CalendarModel: Equatable {
    struct Day: Equatable {
        var letter: String
        var number: String
        var isToday: Bool
        var hasEvents: Bool
        var hasDeadline: Bool
    }

    struct Row: Equatable {
        var time: String
        var title: String
        var color: [Double]?
        /// Happening right now, so the row is brighter.
        var isNow: Bool
        /// A task's deadline rather than an event: drawn as a ring in the tasks' colour.
        var isDeadline = false
    }

    static let maxRows = 3
    /// Days in the strip; the arrows move it by this many.
    static let dayCount = 3

    var days: [Day] = []
    var selected = 0
    /// Days from 2001 to the first day shown, so the scene can tell which way the strip moved.
    var rangeIndex = 0
    var heading = ""
    var rows: [Row] = []
    /// "+2 more", or nil.
    var more: String?
    /// Shown when there are no rows: an empty day, or why there can't be any.
    var message: String?
    /// The message is a button: it asks for access, or opens System Settings.
    var messageIsAction = false

    init() {}

    /// `rangeStart` is the first day shown; `selected` counts from it. `clock` gives "14:00" and
    /// `dayName` gives "Wed 12" for a date. Deadlines show whether or not the calendar may be read.
    init(events: [CalendarEvent], deadlines: [CalendarDeadline] = [], rangeStart: Date, selected: Int, now: Date, calendar: Calendar,
         access: CalendarAccess, clock: (Date) -> String, dayName: (Date) -> String) {
        self.selected = selected
        rangeIndex = Int((rangeStart.timeIntervalSinceReferenceDate / 86400).rounded(.down))
        let letters = calendar.shortWeekdaySymbols.map { $0.uppercased() }
        let today = calendar.startOfDay(for: now)
        let starts = (0..<Self.dayCount).compactMap { calendar.date(byAdding: .day, value: $0, to: rangeStart) }
        days = starts.map { start in
            let end = calendar.date(byAdding: .day, value: 1, to: start) ?? start
            return Day(
                letter: letters[calendar.component(.weekday, from: start) - 1],
                number: String(calendar.component(.day, from: start)),
                isToday: start == today,
                hasEvents: access == .granted && events.contains { $0.start < end && $0.end > start },
                hasDeadline: deadlines.contains { $0.due >= start && $0.due < end }
            )
        }
        guard starts.indices.contains(selected) else { return }
        let dayStart = starts[selected]
        let dayEnd = calendar.date(byAdding: .day, value: 1, to: dayStart) ?? dayStart
        switch calendar.dateComponents([.day], from: today, to: dayStart).day {
        case 0: heading = "Today"
        case 1: heading = "Tomorrow"
        case -1: heading = "Yesterday"
        default: heading = dayName(dayStart)
        }
        var dayDeadlines = deadlines.filter { $0.due >= dayStart && $0.due < dayEnd }
        let hadDeadlines = !dayDeadlines.isEmpty
        if dayStart == today { dayDeadlines.removeAll { $0.due <= now } }
        let deadlineRows = dayDeadlines.map { deadline in
            (sortKey: (1, deadline.due, deadline.title),
             row: Row(time: clock(deadline.due), title: deadline.course.isEmpty ? deadline.title : "\(deadline.title) · \(deadline.course)",
                      color: nil, isNow: false, isDeadline: true))
        }
        guard access == .granted else {
            // Without the calendar, a day with deadlines still lists them; the others offer access.
            guard deadlineRows.isEmpty else {
                fill(deadlineRows.sorted { $0.sortKey < $1.sortKey }.map(\.row), hadAny: hadDeadlines)
                return
            }
            message = access == .notAsked ? "Show your calendar" : "Allow access in System Settings"
            messageIsAction = true
            return
        }
        var onDay = events.filter { $0.start < dayEnd && $0.end > dayStart }
        let hadAny = !onDay.isEmpty || hadDeadlines
        // Today, what has finished is no longer worth a row.
        if dayStart == today { onDay.removeAll { !$0.isAllDay && $0.end <= now } }
        let eventRows = onDay.map { event in
            let isNow = !event.isAllDay && event.start <= now && event.end > now
            let time = event.isAllDay ? "All day" : isNow ? "Now" : event.start < dayStart ? "Until \(clock(event.end))" : clock(event.start)
            return (sortKey: (event.isAllDay ? 0 : 1, event.start, event.title),
                    row: Row(time: time, title: event.title.isEmpty ? "Untitled" : event.title, color: event.color, isNow: isNow))
        }
        fill((eventRows + deadlineRows).sorted { $0.sortKey < $1.sortKey }.map(\.row), hadAny: hadAny)
    }

    private mutating func fill(_ all: [Row], hadAny: Bool) {
        rows = Array(all.prefix(Self.maxRows))
        if all.count > Self.maxRows { more = "+\(all.count - Self.maxRows) more" }
        if all.isEmpty { message = hadAny ? "Nothing else today" : "Nothing planned" }
    }

    /// The strip moved `pages` times its length from `start`.
    static func rangeStart(_ start: Date, moved pages: Int, calendar: Calendar) -> Date {
        calendar.date(byAdding: .day, value: pages * dayCount, to: start) ?? start
    }
}
