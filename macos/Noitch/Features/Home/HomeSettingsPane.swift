import AppKit

/// Home's page in the settings window.
enum HomeSettingsPane {
    static let pane = SettingsPane(title: "Home", glyph: .icon(.music), color: .systemPink) { form in
        form.section("Lyrics", footer: "Lyrics come from lrclib.net, which receives the track, artist and album names.") {
            form.toggle("Show lyrics", \.home.showLyrics)
            form.toggle("Show the next line", caption: "A dimmer line under the one being sung.", \.home.showNextLyric, enabledBy: \.home.showLyrics)
            form.choice("Timing", caption: "Move lyrics earlier or later when they don't match the song.", \.home.lyricsOffset,
                        options: HomeSettings.lyricsOffsets.map { ($0 == 0 ? "As published" : SettingsPanes.seconds($0, signed: true), $0) },
                        enabledBy: \.home.showLyrics)
        }
        form.section("Controls") {
            form.toggle("Show shuffle and repeat", \.home.showShuffleAndRepeat)
        }
        form.section("Artwork color") {
            form.toggle("Tint the progress bar", \.home.tintWithArtwork)
            form.toggle("Glow behind the page", caption: "A soft wash of the artwork's color.", \.home.artworkGlow)
        }
    }
}
