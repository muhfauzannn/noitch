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
    }

    struct Row: Equatable {
        var time: String
        var title: String
        var color: [Double]?
        /// Happening right now, so the row is brighter.
        var isNow: Bool
    }

    static let maxRows = 3

    var days: [Day] = []
    var selected = 0
    var heading = ""
    var rows: [Row] = []
    /// "+2 more", or nil.
    var more: String?
    /// Shown when there are no rows: an empty day, or why there can't be any.
    var message: String?
    /// The message is a button: it asks for access, or opens System Settings.
    var messageIsAction = false

    init() {}

    /// `weekStart` is the first day shown; `selected` counts from it. `clock` gives "14:00" and
    /// `dayName` gives "Wed 12" for a date.
    init(events: [CalendarEvent], weekStart: Date, selected: Int, now: Date, calendar: Calendar, access: CalendarAccess,
         clock: (Date) -> String, dayName: (Date) -> String) {
        self.selected = selected
        let letters = calendar.veryShortWeekdaySymbols
        let today = calendar.startOfDay(for: now)
        let starts = (0..<7).compactMap { calendar.date(byAdding: .day, value: $0, to: weekStart) }
        days = starts.map { start in
            let end = calendar.date(byAdding: .day, value: 1, to: start) ?? start
            return Day(
                letter: letters[calendar.component(.weekday, from: start) - 1],
                number: String(calendar.component(.day, from: start)),
                isToday: start == today,
                hasEvents: access == .granted && events.contains { $0.start < end && $0.end > start }
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
        switch access {
        case .notAsked:
            message = "Show your calendar"
            messageIsAction = true
            return
        case .denied:
            message = "Allow access in System Settings"
            messageIsAction = true
            return
        case .granted:
            break
        }
        var onDay = events.filter { $0.start < dayEnd && $0.end > dayStart }
        let hadAny = !onDay.isEmpty
        // Today, what has finished is no longer worth a row.
        if dayStart == today { onDay.removeAll { !$0.isAllDay && $0.end <= now } }
        onDay.sort { ($0.isAllDay ? 0 : 1, $0.start, $0.title) < ($1.isAllDay ? 0 : 1, $1.start, $1.title) }
        rows = onDay.prefix(Self.maxRows).map { event in
            let isNow = !event.isAllDay && event.start <= now && event.end > now
            let time = event.isAllDay ? "All day" : isNow ? "Now" : event.start < dayStart ? "Until \(clock(event.end))" : clock(event.start)
            return Row(time: time, title: event.title.isEmpty ? "Untitled" : event.title, color: event.color, isNow: isNow)
        }
        if onDay.count > Self.maxRows { more = "+\(onDay.count - Self.maxRows) more" }
        if onDay.isEmpty { message = hadAny ? "Nothing else today" : "Nothing planned" }
    }

    /// The week that holds `date`, starting on the locale's first weekday.
    static func weekStart(for date: Date, calendar: Calendar) -> Date {
        calendar.dateInterval(of: .weekOfYear, for: date)?.start ?? calendar.startOfDay(for: date)
    }
}
