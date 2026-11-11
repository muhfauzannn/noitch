import Foundation

/// Home's preferences. The keys predate the split into parts, so existing choices carry over.
struct HomeSettings: Equatable {
    var showLyrics = true
    var showNextLyric = true
    /// Seconds added to the playback position when picking the lyric, for lyrics that run early
    /// (negative) or late (positive).
    var lyricsOffset = 0.0
    var showShuffleAndRepeat = true
    /// The progress bar takes the artwork's color.
    var tintWithArtwork = true
    /// A soft wash of the artwork's color behind the page.
    var artworkGlow = true
    /// While something plays, the closed island widens to show its artwork and level bars.
    var showLive = true
    /// A week and the chosen day's events beside the music, which narrows for it. macOS only.
    var showCalendar = true

    static let lyricsOffsets = [-1.0, -0.5, -0.25, 0, 0.25, 0.5, 1.0]

    static func load(_ store: SettingsStore) -> HomeSettings {
        var settings = HomeSettings()
        store.read("showLyrics", into: &settings.showLyrics)
        store.read("showNextLyric", into: &settings.showNextLyric)
        store.read("lyricsOffset", into: &settings.lyricsOffset)
        store.read("showShuffleAndRepeat", into: &settings.showShuffleAndRepeat)
        store.read("tintWithArtwork", into: &settings.tintWithArtwork)
        store.read("artworkGlow", into: &settings.artworkGlow)
        store.read("showLive", into: &settings.showLive)
        store.read("showCalendar", into: &settings.showCalendar)
        settings.lyricsOffset = min(max(settings.lyricsOffset, -5), 5)
        return settings
    }

    func save(_ store: SettingsStore) {
        store.write("showLyrics", showLyrics)
        store.write("showNextLyric", showNextLyric)
        store.write("lyricsOffset", lyricsOffset)
        store.write("showShuffleAndRepeat", showShuffleAndRepeat)
        store.write("tintWithArtwork", tintWithArtwork)
        store.write("artworkGlow", artworkGlow)
        store.write("showLive", showLive)
        store.write("showCalendar", showCalendar)
    }
}
