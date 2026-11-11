import Foundation

/// The tasks page's preferences. The account's key itself is in the Keychain, never here.
struct TasksSettings: Equatable {
    static let defaultSite = "https://scele.cs.ui.ac.id"
    /// Minutes between checks while the island is closed. 30 is the most often the owner approved
    /// (2026-11-12); 0 checks only when the island opens, the page shows or the Mac wakes.
    static let checkChoices = [30.0, 60, 0]

    var site = TasksSettings.defaultSite
    /// The signed-in person's name from the site, or empty when signed out.
    var accountName = ""
    var checkMinutes = 30.0
    var alerts = true
    var reminders = ReminderChoice.dayAndHour
    var scope = TaskScope.assignmentsAndQuizzes

    var isSignedIn: Bool { !accountName.isEmpty }

    static func load(_ store: SettingsStore) -> TasksSettings {
        var settings = TasksSettings()
        store.read("tasksSite", into: &settings.site)
        store.read("tasksAccountName", into: &settings.accountName)
        store.read("tasksCheckMinutes", into: &settings.checkMinutes)
        store.read("tasksAlerts", into: &settings.alerts)
        store.read("tasksReminders", into: &settings.reminders)
        store.read("tasksScope", into: &settings.scope)
        settings.site = normalizedSite(settings.site) ?? defaultSite
        // Never more often than approved.
        if settings.checkMinutes != 0 { settings.checkMinutes = min(max(settings.checkMinutes, 30), 24 * 60) }
        return settings
    }

    func save(_ store: SettingsStore) {
        store.write("tasksSite", site)
        store.write("tasksAccountName", accountName)
        store.write("tasksCheckMinutes", checkMinutes)
        store.write("tasksAlerts", alerts)
        store.write("tasksReminders", reminders)
        store.write("tasksScope", scope)
    }

    /// "scele.cs.ui.ac.id/" becomes "https://scele.cs.ui.ac.id". Only HTTPS is accepted: the key and,
    /// at sign-in, the password travel to it.
    nonisolated static func normalizedSite(_ text: String) -> String? {
        var site = text.trimmingCharacters(in: .whitespacesAndNewlines)
        if !site.contains("://") { site = "https://" + site }
        while site.hasSuffix("/") { site.removeLast() }
        guard let url = URL(string: site), url.scheme == "https", let host = url.host, host.contains(".") else { return nil }
        return site
    }
}

enum ReminderChoice: String, CaseIterable {
    case dayAndHour
    case hour
    case off

    /// Seconds before the deadline.
    var leads: [Double] {
        switch self {
        case .dayAndHour: [86400, 3600]
        case .hour: [3600]
        case .off: []
        }
    }

    var title: String {
        switch self {
        case .dayAndHour: "1 day and 1 hour"
        case .hour: "1 hour"
        case .off: "Off"
        }
    }
}
