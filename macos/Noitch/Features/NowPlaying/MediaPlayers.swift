/// Apps that can be the system's now playing source. While none is running, nothing is playing, so
/// the adapter can stay stopped with the island closed. Browsers count: web players are the common
/// case. The Windows app has no list: its media events cost nothing at rest.
enum MediaPlayers {
    static let bundleIdentifiers: Set<String> = [
        "com.apple.Music", "com.apple.TV", "com.apple.podcasts", "com.apple.QuickTimePlayerX",
        "com.spotify.client", "com.tidal.desktop", "com.amazon.music", "com.deezer.deezer-desktop",
        "org.videolan.vlc", "com.colliderli.iina", "io.mpv",
        "com.apple.Safari", "com.apple.SafariTechnologyPreview", "com.google.Chrome", "com.google.Chrome.canary",
        "org.chromium.Chromium", "company.thebrowser.Browser", "company.thebrowser.dia", "org.mozilla.firefox",
        "com.microsoft.edgemac", "com.brave.Browser", "com.operasoftware.Opera", "com.vivaldi.Vivaldi",
        "app.zen-browser.zen", "com.kagi.kagimacOS",
    ]

    static func isPlayer(_ bundleIdentifier: String?) -> Bool {
        bundleIdentifier.map(bundleIdentifiers.contains) ?? false
    }
}
