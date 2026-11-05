import Foundation

/// What Home shows, reduced to values. The scene draws exactly this and decides nothing.
struct HomeModel: Equatable {
    var title = "Nothing playing"
    var artist = "Start something to see it here"
    var lyricLine = ""
    var panelLines = ["", "", ""]
    var elapsedText = "0:00"
    var remainingText = "0:00"
    var hasTrack = false
    var isPlaying = false
    var isShuffled = false
    var repeatMode = RepeatMode.off
    var showsLyricsPanel = false
    var showsLyricsButton = true
    var showsShuffleAndRepeat = true

    init() {}

    init(nowPlaying: NowPlaying, lyrics: Lyrics?, isLoadingLyrics: Bool, position: TimeInterval, showsPanel: Bool, settings: AppSettings) {
        showsLyricsButton = settings.showLyrics
        showsShuffleAndRepeat = settings.showShuffleAndRepeat
        showsLyricsPanel = showsPanel && settings.showLyrics
        guard nowPlaying.hasTrack else { return }
        hasTrack = true
        title = nowPlaying.title
        artist = nowPlaying.artist
        isPlaying = nowPlaying.isPlaying
        isShuffled = nowPlaying.isShuffled
        repeatMode = nowPlaying.repeatMode
        elapsedText = Self.formatTime(position)
        remainingText = "-" + Self.formatTime(max(nowPlaying.duration - position, 0))
        guard settings.showLyrics else { return }
        let status = Self.lyricStatus(lyrics, isLoading: isLoadingLyrics)
        if let lyrics, !lyrics.synced.isEmpty {
            let index = lyrics.lineIndex(at: position)
            lyricLine = index.map { lyrics.synced[$0].text } ?? ""
            let current = index ?? -1
            panelLines = [current - 1, current, current + 1].map { line in
                lyrics.synced.indices.contains(line) ? lyrics.synced[line].text : ""
            }
        } else if let lyrics, !lyrics.plain.isEmpty {
            lyricLine = status
            panelLines = Array((lyrics.plain + ["", "", ""]).prefix(3))
        } else {
            lyricLine = status
            panelLines = ["", status, ""]
        }
    }

    private static func lyricStatus(_ lyrics: Lyrics?, isLoading: Bool) -> String {
        if isLoading { return "Looking for lyrics…" }
        guard let lyrics else { return "" }
        if lyrics.isInstrumental { return "Instrumental" }
        if !lyrics.plain.isEmpty { return "Lyrics aren't synced for this track" }
        return "No lyrics found"
    }

    static func formatTime(_ seconds: TimeInterval) -> String {
        let total = Int(max(seconds, 0))
        let (hours, minutes, rest) = (total / 3600, total / 60 % 60, total % 60)
        let tail = String(format: "%02d", rest)
        return hours > 0 ? "\(hours):" + String(format: "%02d", minutes) + ":\(tail)" : "\(minutes):\(tail)"
    }

    /// How long until anything on screen changes: the next whole second or the next lyric. One
    /// one-shot timer covers both, and it only exists while the island is open and playing.
    static func nextTick(position: TimeInterval, rate: Double, nextLyric: TimeInterval?) -> TimeInterval {
        let speed = max(rate, 0.1)
        var delay = (position.rounded(.down) + 1 - position) / speed
        if let nextLyric, nextLyric > position { delay = min(delay, (nextLyric - position) / speed) }
        return max(delay, 0.05)
    }
}
