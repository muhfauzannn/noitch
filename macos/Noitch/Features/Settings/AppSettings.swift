import Foundation

/// The user's preferences. The defaults are the app's behavior without any settings stored.
struct AppSettings: Equatable {
    var openOnHover = true
    var showLyrics = true
    var showShuffleAndRepeat = true
    var tintWithArtwork = true

    private enum Key {
        static let openOnHover = "openOnHover"
        static let showLyrics = "showLyrics"
        static let showShuffleAndRepeat = "showShuffleAndRepeat"
        static let tintWithArtwork = "tintWithArtwork"
    }

    static func load(from defaults: UserDefaults = .standard) -> AppSettings {
        var settings = AppSettings()
        func read(_ key: String, into value: inout Bool) {
            if defaults.object(forKey: key) != nil { value = defaults.bool(forKey: key) }
        }
        read(Key.openOnHover, into: &settings.openOnHover)
        read(Key.showLyrics, into: &settings.showLyrics)
        read(Key.showShuffleAndRepeat, into: &settings.showShuffleAndRepeat)
        read(Key.tintWithArtwork, into: &settings.tintWithArtwork)
        return settings
    }

    func save(to defaults: UserDefaults = .standard) {
        defaults.set(openOnHover, forKey: Key.openOnHover)
        defaults.set(showLyrics, forKey: Key.showLyrics)
        defaults.set(showShuffleAndRepeat, forKey: Key.showShuffleAndRepeat)
        defaults.set(tintWithArtwork, forKey: Key.tintWithArtwork)
    }
}
