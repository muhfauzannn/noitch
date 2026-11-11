import CoreGraphics
import Foundation
import Testing
@testable import Noitch

struct CalendarTests {
    private var calendar: Calendar {
        var calendar = Calendar(identifier: .gregorian)
        calendar.timeZone = TimeZone(identifier: "UTC")!
        calendar.firstWeekday = 2
        return calendar
    }

    /// Wednesday 11 November 2026, 10:00 UTC.
    private let now = Date(timeIntervalSince1970: 1_794_391_200)

    private func date(_ day: Int, _ hour: Int, _ minute: Int = 0) -> Date {
        calendar.date(from: DateComponents(year: 2026, month: 11, day: day, hour: hour, minute: minute))!
    }

    private func model(_ events: [CalendarEvent], selected: Int, access: CalendarAccess = .granted) -> CalendarModel {
        let start = CalendarModel.weekStart(for: now, calendar: calendar)
        let clock = DateFormatter()
        clock.timeZone = calendar.timeZone
        clock.dateFormat = "HH:mm"
        return CalendarModel(events: events, weekStart: start, selected: selected, now: now, calendar: calendar, access: access,
                             clock: { clock.string(from: $0) }, dayName: { _ in "Fri 13" })
    }

    @Test func `the week starts on the locale's first day and marks today`() {
        let week = model([], selected: 2)
        #expect(week.days.map(\.number) == ["9", "10", "11", "12", "13", "14", "15"])
        #expect(week.days.map(\.isToday) == [false, false, true, false, false, false, false])
        #expect(week.heading == "Today")
        #expect(model([], selected: 3).heading == "Tomorrow" && model([], selected: 1).heading == "Yesterday")
        #expect(model([], selected: 4).heading == "Fri 13")
    }

    @Test func `today skips what has ended and shows what is on now first`() {
        let events = [
            CalendarEvent(title: "Breakfast", start: date(11, 7), end: date(11, 8), isAllDay: false, color: nil),
            CalendarEvent(title: "Review", start: date(11, 14), end: date(11, 15), isAllDay: false, color: [1, 0, 0]),
            CalendarEvent(title: "Focus", start: date(11, 9, 30), end: date(11, 10, 30), isAllDay: false, color: nil),
            CalendarEvent(title: "Holiday", start: date(11, 0), end: date(12, 0), isAllDay: true, color: nil),
        ]
        let today = model(events, selected: 2)
        #expect(today.rows.map(\.title) == ["Holiday", "Focus", "Review"])
        #expect(today.rows.map(\.time) == ["All day", "Now", "14:00"])
        #expect(today.rows[1].isNow && !today.rows[2].isNow)
        #expect(today.more == nil && today.days[2].hasEvents)
    }

    @Test func `a busy day shows three rows and counts the rest`() {
        let events = (0..<5).map { CalendarEvent(title: "Meeting \($0)", start: date(12, 9 + $0), end: date(12, 10 + $0), isAllDay: false, color: nil) }
        let day = model(events, selected: 3)
        #expect(day.rows.count == CalendarModel.maxRows && day.more == "+2 more")
        #expect(day.days[3].hasEvents && !day.days[4].hasEvents)
    }

    @Test func `an empty day and a locked calendar say so`() {
        #expect(model([], selected: 4).message == "Nothing planned")
        let ended = [CalendarEvent(title: "Early", start: date(11, 6), end: date(11, 7), isAllDay: false, color: nil)]
        #expect(model(ended, selected: 2).message == "Nothing else today")
        let asking = model([], selected: 2, access: .notAsked)
        #expect(asking.messageIsAction && asking.rows.isEmpty)
        #expect(model([], selected: 2, access: .denied).messageIsAction)
        // Without access, no day claims to have events.
        #expect(!model(ended, selected: 2, access: .denied).days[2].hasEvents)
    }

    @Test func `the calendar fits beside the music in the normal island`() {
        for header in [CGFloat(24), 32, 38] {
            var layout = HomeLayout(size: IslandGeometry.expandedSize, headerHeight: header)
            layout.showsCalendar = true
            let pill = CGRect(origin: .zero, size: IslandGeometry.expandedSize)
            #expect(pill.contains(layout.calendarPanel))
            #expect(layout.calendarRow(CalendarModel.maxRows - 1).maxY <= pill.height - HomeLayout.bottomPadding)
            #expect(layout.calendarDay(0).minY >= header - 2)
            for control in HomeControl.allCases {
                #expect(layout.frame(of: control).maxX < layout.calendarDivider.minX, "\(control)")
                for other in HomeControl.allCases where other != control {
                    #expect(!layout.frame(of: control).intersects(layout.frame(of: other)), "\(control) \(other)")
                }
            }
            // The transport still starts after the column does.
            #expect(layout.frame(of: .shuffle).minX >= layout.title.minX)
            #expect(layout.calendarDayIndex(at: CGPoint(x: layout.calendarDay(6).midX, y: layout.calendarDay(6).midY)) == 6)
        }
    }

}
