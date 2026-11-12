import Foundation

/// The user's preferences, one part per feature. The defaults are the app's behavior with nothing
/// stored. Each part reads and writes its own keys; the Windows app uses the same names.
struct AppSettings: Equatable {
    var general = GeneralSettings()
    var pages = PageSettings()
    var home = HomeSettings()
    var usage = UsageSettings()
    var tasks = TasksSettings()

    static func load(from defaults: UserDefaults = .standard) -> AppSettings {
        let store = SettingsStore(defaults: defaults)
        return AppSettings(general: .load(store), pages: .load(store), home: .load(store), usage: .load(store), tasks: .load(store))
    }

    func save(to defaults: UserDefaults = .standard) {
        let store = SettingsStore(defaults: defaults)
        general.save(store)
        pages.save(store)
        home.save(store)
        usage.save(store)
        tasks.save(store)
    }
}

/// How the island opens and where it lives.
struct GeneralSettings: Equatable {
    var openOnHover = true
    /// Seconds the pointer must rest in the notch before the island opens. 0 opens at once.
    var hoverDelay = 0.0
    var display = DisplayChoice.notch
    /// A tap on the trackpad as the island opens. Only felt on a Force Touch trackpad.
    var haptics = false
    /// Pins the island open, and lets it go, from anywhere. Nil when turned off.
    var pinShortcut: Shortcut? = .pinDefault

    static let hoverDelays = [0.0, 0.15, 0.3, 0.5]

    static func load(_ store: SettingsStore) -> GeneralSettings {
        var settings = GeneralSettings()
        store.read("openOnHover", into: &settings.openOnHover)
        store.read("hoverDelay", into: &settings.hoverDelay)
        store.read("display", into: &settings.display)
        store.read("haptics", into: &settings.haptics)
        settings.hoverDelay = min(max(settings.hoverDelay, 0), 1)
        // A value written as a 32-bit float reads back a hair off its choice, and would match none.
        if let near = hoverDelays.first(where: { abs($0 - settings.hoverDelay) < 0.001 }) { settings.hoverDelay = near }
        var key = Double(Shortcut.pinDefault.keyCode)
        var modifiers = Shortcut.pinDefault.storedModifiers
        store.read("pinKeyCode", into: &key)
        store.read("pinModifiers", into: &modifiers)
        // -1 is "none"; anything else unreadable falls back to the default.
        settings.pinShortcut = key < 0 ? nil : Shortcut(storedKey: key, modifiers: modifiers) ?? .pinDefault
        return settings
    }

    func save(_ store: SettingsStore) {
        store.write("openOnHover", openOnHover)
        store.write("hoverDelay", hoverDelay)
        store.write("display", display)
        store.write("haptics", haptics)
        store.write("pinKeyCode", pinShortcut.map { Double($0.keyCode) } ?? -1)
        store.write("pinModifiers", pinShortcut?.storedModifiers ?? 0)
    }
}

enum DisplayChoice: String, CaseIterable {
    /// The built-in display's notch when there is one, else the main display.
    case notch
    /// The display with the menu bar, even when another has a notch.
    case main

    var title: String {
        switch self {
        case .notch: "Built-in display with a notch"
        case .main: "Main display"
        }
    }
}

/// Which pages the island has, and which it opens on.
struct PageSettings: Equatable {
    var showUsage = true
    var showTasks = true
    var showShelf = true
    var openOn = OpenPage.last

    static func load(_ store: SettingsStore) -> PageSettings {
        var settings = PageSettings()
        store.read("showUsagePage", into: &settings.showUsage)
        store.read("showTasksPage", into: &settings.showTasks)
        store.read("showShelfPage", into: &settings.showShelf)
        store.read("openOn", into: &settings.openOn)
        return settings
    }

    func save(_ store: SettingsStore) {
        store.write("showUsagePage", showUsage)
        store.write("showTasksPage", showTasks)
        store.write("showShelfPage", showShelf)
        store.write("openOn", openOn)
    }

    /// Home is always there: the island needs one page, and it is the one with the controls.
    var visible: [IslandPage] {
        [.home] + (showUsage ? [.usage] : []) + (showTasks ? [.tasks] : []) + (showShelf ? [.shelf] : [])
    }

    /// The page to show on opening, given the one shown last.
    func page(opening last: IslandPage) -> IslandPage {
        let wanted: IslandPage = switch openOn {
        case .last: last
        case .home: .home
        case .usage: .usage
        case .tasks: .tasks
        case .shelf: .shelf
        }
        return visible.contains(wanted) ? wanted : .home
    }
}

enum OpenPage: String, CaseIterable {
    case last
    case home
    case usage
    case tasks
    case shelf

    var title: String {
        switch self {
        case .last: "The page used last"
        case .home: "Home"
        case .usage: "AI Usage"
        case .tasks: "Tasks"
        case .shelf: "Shelf"
        }
    }
}

/// Typed reads and writes over `UserDefaults`. A missing or unreadable value leaves the default.
struct SettingsStore {
    let defaults: UserDefaults

    func read(_ key: String, into value: inout Bool) {
        if defaults.object(forKey: key) != nil { value = defaults.bool(forKey: key) }
    }

    func read(_ key: String, into value: inout Double) {
        if let number = defaults.object(forKey: key) as? NSNumber, number.doubleValue.isFinite { value = number.doubleValue }
    }

    func read<Choice: RawRepresentable<String>>(_ key: String, into value: inout Choice) {
        if let raw = defaults.string(forKey: key), let choice = Choice(rawValue: raw) { value = choice }
    }

    func read(_ key: String, into value: inout String) {
        if let text = defaults.string(forKey: key) { value = text }
    }

    func write(_ key: String, _ value: Bool) { defaults.set(value, forKey: key) }
    func write(_ key: String, _ value: String) { defaults.set(value, forKey: key) }
    func write(_ key: String, _ value: Double) { defaults.set(value, forKey: key) }
    func write<Choice: RawRepresentable<String>>(_ key: String, _ value: Choice) { defaults.set(value.rawValue, forKey: key) }
}
