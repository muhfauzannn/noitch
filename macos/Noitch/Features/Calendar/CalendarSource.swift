import AppKit
import EventKit
import os

/// Reads the week's events from the Calendar app's store. It exists only while Home is shown or in
/// its grace period: the store is made on first use and dropped on release. Changes arrive as
/// `EKEventStoreChanged` notifications, so nothing polls. It never asks for access on its own; the
/// panel asks when the user clicks it.
final class CalendarSource: NSObject {
    private static let logger = Logger(subsystem: "app.noitch.mac", category: "Calendar")

    /// Called on the main thread when events may have changed or access was just granted.
    var onChange: (() -> Void)?

    private var store: EKEventStore?

    var access: CalendarAccess {
        switch EKEventStore.authorizationStatus(for: .event) {
        case .fullAccess: .granted
        case .notDetermined: .notAsked
        default: .denied
        }
    }

    func start() {
        guard store == nil, access == .granted else { return }
        let store = EKEventStore()
        self.store = store
        NotificationCenter.default.addObserver(self, selector: #selector(storeChanged), name: .EKEventStoreChanged, object: store)
    }

    func stop() {
        NotificationCenter.default.removeObserver(self)
        store = nil
    }

    /// Events overlapping the seven days from `weekStart`. Synchronous: a week is a small read.
    func events(weekStart: Date, calendar: Calendar) -> [CalendarEvent] {
        guard let store, let end = calendar.date(byAdding: .day, value: 7, to: weekStart) else { return [] }
        let predicate = store.predicateForEvents(withStart: weekStart, end: end, calendars: nil)
        return store.events(matching: predicate).map { event in
            let color = event.calendar?.cgColor.flatMap { NSColor(cgColor: $0)?.usingColorSpace(.sRGB) }
            return CalendarEvent(
                title: event.title ?? "",
                start: event.startDate,
                end: event.endDate,
                isAllDay: event.isAllDay,
                color: color.map { [Double($0.redComponent), Double($0.greenComponent), Double($0.blueComponent)] }
            )
        }
    }

    /// Shows the system prompt the first time; after a refusal only System Settings can change it.
    func requestAccess() {
        switch access {
        case .granted:
            return
        case .denied:
            if let url = URL(string: "x-apple.systempreferences:com.apple.preference.security?Privacy_Calendars") {
                NSWorkspace.shared.open(url)
            }
        case .notAsked:
            // The store is held by the handler, so it lives until the prompt is answered.
            let asking = EKEventStore()
            asking.requestFullAccessToEvents { [asking] _, error in
                if let error { Self.logger.error("Calendar access failed: \(error.localizedDescription)") }
                _ = asking
                DispatchQueue.main.async {
                    NotificationCenter.default.post(name: Self.accessChanged, object: nil)
                }
            }
        }
    }

    /// Posted on the main thread after the access prompt is answered.
    static let accessChanged = Notification.Name("app.noitch.calendarAccessChanged")

    @objc private func storeChanged() {
        onChange?()
    }
}
