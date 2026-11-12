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

    /// The strip starts on yesterday, the 10th, so the old week-based day numbers read as
    /// `selected - 1`: 1 is today, 2 tomorrow.
    private func model(_ events: [CalendarEvent], deadlines: [CalendarDeadline] = [], selected: Int, access: CalendarAccess = .granted,
                       moved: Int = 0) -> CalendarModel {
        let start = CalendarModel.rangeStart(calendar.date(byAdding: .day, value: -1, to: calendar.startOfDay(for: now))!, moved: moved, calendar: calendar)
        let clock = DateFormatter()
        clock.timeZone = calendar.timeZone
        clock.dateFormat = "HH:mm"
        return CalendarModel(events: events, deadlines: deadlines, rangeStart: start, selected: selected, now: now, calendar: calendar, access: access,
                             clock: { clock.string(from: $0) }, dayName: { _ in "Fri 13" })
    }

    @Test func `the strip shows three days, marks today and moves by three`() {
        let strip = model([], selected: 1)
        #expect(strip.days.map(\.number) == ["10", "11", "12"])
        #expect(strip.days.map(\.isToday) == [false, true, false])
        #expect(strip.heading == "Today")
        #expect(model([], selected: 2).heading == "Tomorrow" && model([], selected: 0).heading == "Yesterday")
        #expect(model([], selected: 0, moved: 1).days.map(\.number) == ["13", "14", "15"])
        #expect(model([], selected: 0, moved: 1).heading == "Fri 13")
        #expect(model([], selected: 0, moved: -1).days.map(\.number) == ["7", "8", "9"])
    }

    @Test func `deadlines show among the events, as rings, with or without calendar access`() {
        let events = [CalendarEvent(title: "Review", start: date(11, 14), end: date(11, 15), isAllDay: false, color: [1, 0, 0])]
        let deadlines = [CalendarDeadline(title: "Tugas 3", course: "PBP", due: date(11, 12)),
                         CalendarDeadline(title: "Gone", course: "SDA", due: date(11, 9)),
                         CalendarDeadline(title: "Kuis 4", course: "SDA", due: date(12, 10))]
        let today = model(events, deadlines: deadlines, selected: 1)
        #expect(today.rows.map(\.title) == ["Tugas 3 · PBP", "Review"])
        #expect(today.rows[0].isDeadline && today.rows[0].time == "12:00" && !today.rows[1].isDeadline)
        #expect(today.days.map(\.hasDeadline) == [false, true, true])
        let locked = model([], deadlines: deadlines, selected: 2, access: .notAsked)
        #expect(locked.rows.map(\.title) == ["Kuis 4 · SDA"] && !locked.messageIsAction)
        #expect(model([], deadlines: deadlines, selected: 0, access: .notAsked).messageIsAction)
    }

    @Test func `today skips what has ended and shows what is on now first`() {
        let events = [
            CalendarEvent(title: "Breakfast", start: date(11, 7), end: date(11, 8), isAllDay: false, color: nil),
            CalendarEvent(title: "Review", start: date(11, 14), end: date(11, 15), isAllDay: false, color: [1, 0, 0]),
            CalendarEvent(title: "Focus", start: date(11, 9, 30), end: date(11, 10, 30), isAllDay: false, color: nil),
            CalendarEvent(title: "Holiday", start: date(11, 0), end: date(12, 0), isAllDay: true, color: nil),
        ]
        let today = model(events, selected: 1)
        #expect(today.rows.map(\.title) == ["Holiday", "Focus", "Review"])
        #expect(today.rows.map(\.time) == ["All day", "Now", "14:00"])
        #expect(today.rows[1].isNow && !today.rows[2].isNow)
        #expect(today.more == nil && today.days[1].hasEvents)
    }

    @Test func `a busy day shows three rows and counts the rest`() {
        let events = (0..<5).map { CalendarEvent(title: "Meeting \($0)", start: date(12, 9 + $0), end: date(12, 10 + $0), isAllDay: false, color: nil) }
        let day = model(events, selected: 2)
        #expect(day.rows.count == CalendarModel.maxRows && day.more == "+2 more")
        #expect(day.days[2].hasEvents && !day.days[1].hasEvents)
    }

    @Test func `an empty day and a locked calendar say so`() {
        #expect(model([], selected: 2).message == "Nothing planned")
        let ended = [CalendarEvent(title: "Early", start: date(11, 6), end: date(11, 7), isAllDay: false, color: nil)]
        #expect(model(ended, selected: 1).message == "Nothing else today")
        let asking = model([], selected: 1, access: .notAsked)
        #expect(asking.messageIsAction && asking.rows.isEmpty)
        #expect(model([], selected: 1, access: .denied).messageIsAction)
        // Without access, no day claims to have events.
        #expect(!model(ended, selected: 1, access: .denied).days[1].hasEvents)
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
            let last = CalendarModel.dayCount - 1
            #expect(layout.calendarDayIndex(at: CGPoint(x: layout.calendarDay(last).midX, y: layout.calendarDay(last).midY)) == last)
            #expect(layout.calendarPrevious.maxX <= layout.calendarDay(0).minX && layout.calendarNext.minX >= layout.calendarDay(last).maxX)
            #expect(pill.contains(layout.calendarNext))
        }
    }


    @Test func `every opening is centred on today`() {
        let start = CalendarModel.centredStart(on: now, calendar: calendar)
        let clock = DateFormatter()
        let strip = CalendarModel(events: [], rangeStart: start, selected: CalendarModel.dayCount / 2, now: now, calendar: calendar, access: .granted,
                                  clock: { clock.string(from: $0) }, dayName: { _ in "" })
        #expect(strip.days.map(\.number) == ["10", "11", "12"])
        #expect(strip.days[strip.selected].isToday && strip.heading == "Today")
    }
}
