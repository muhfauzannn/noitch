import Foundation

/// What Home shows, reduced to values. The scene draws exactly this and decides nothing.
struct HomeModel: Equatable {
    var title = "Nothing playing"
    var artist = "Start something to see it here"
    /// The line being sung, bright, and the one after it, dim. A status message takes the first slot.
    var lyric = ""
    var nextLyric = ""
    var followingLyrics: [String] = []
    /// The synced line's index, so the scene can tell a step forward (a slide) from a jump (a fade).
    var lyricIndex: Int?
    var lyricIsStatus = false
    var elapsedText = "0:00"
    var remainingText = "0:00"
    var hasTrack = false
    var isPlaying = false
    var isShuffled = false
    var repeatMode = RepeatMode.off
    var showsShuffleAndRepeat = true
    /// After next or previous, until the player reports the new track: the old one stays, dimmed,
    /// so the press is answered at once even when the player is slow to say what comes next.
    var isSkipping = false
    /// The lyrics button shows only while lyrics are on.
    var showsLyricsButton = true
    /// The whole page is the lyrics stage.
    var isOnStage = false
    /// The stage's lines: the one before, the one being sung, and the two after. An empty line shows
    /// as a note, so every slot reads as part of the song.
    var stageLines = ["", "", "", ""]
    var stageRomanizations = ["", "", "", ""]
    var stageIsStatus = false

    init() {}

    init(nowPlaying: NowPlaying, lyrics: Lyrics?, isLoadingLyrics: Bool, position: TimeInterval, settings: HomeSettings, isSkipping: Bool = false, onStage: Bool = false) {
        showsShuffleAndRepeat = settings.showShuffleAndRepeat
        showsLyricsButton = settings.showLyrics
        isOnStage = onStage && settings.showLyrics
        guard nowPlaying.hasTrack else {
            stageLines[1] = title
            stageIsStatus = true
            return
        }
        hasTrack = true
        title = nowPlaying.title
        artist = nowPlaying.artist
        isPlaying = nowPlaying.isPlaying
        isShuffled = nowPlaying.isShuffled
        repeatMode = nowPlaying.repeatMode
        self.isSkipping = isSkipping
        guard !isSkipping else {
            elapsedText = "0:00"
            remainingText = ""
            stageLines[1] = "♪"
            stageIsStatus = true
            return
        }
        elapsedText = Self.formatTime(position)
        remainingText = "-" + Self.formatTime(max(nowPlaying.duration - position, 0))
        guard settings.showLyrics else { return }
        if let lyrics, !lyrics.synced.isEmpty {
            let index = lyrics.lineIndex(at: position + settings.lyricsOffset)
            lyricIndex = index
            // Before the first line, and in the gaps a line leaves empty, a note holds the place.
            let current = index.map { lyrics.synced[$0].text } ?? ""
            lyric = current.isEmpty ? "♪" : current
            let next = (index ?? -1) + 1
            nextLyric = settings.showNextLyric && lyrics.synced.indices.contains(next) ? lyrics.synced[next].text : ""
            if settings.showNextLyric {
                followingLyrics = (0..<8).map { step in
                    let line = next + step
                    return lyrics.synced.indices.contains(line) ? lyrics.synced[line].text : ""
                }
            }
            stageRomanizations = (-1...2).map { step in
                let line = (index ?? -1) + step
                return lyrics.synced.indices.contains(line) ? lyrics.synced[line].romanization : ""
            }
            stageLines = (-1...2).map { step in
                let line = (index ?? -1) + step
                guard lyrics.synced.indices.contains(line) else { return step == 0 ? "♪" : "" }
                return lyrics.synced[line].text.isEmpty ? "♪" : lyrics.synced[line].text
            }
        } else {
            lyric = Self.lyricStatus(lyrics, isLoading: isLoadingLyrics)
            lyricIsStatus = true
            stageLines[1] = lyric
            stageIsStatus = true
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
    /// one-shot timer covers both, and it only exists while the island is open and playing. The stage
    /// shows no clock, so there only lyrics count, and nil means nothing will change.
    static func nextTick(position: TimeInterval, rate: Double, nextLyric: TimeInterval?, wholeSeconds: Bool = true) -> TimeInterval? {
        let speed = max(rate, 0.1)
        var delay: TimeInterval? = wholeSeconds ? (position.rounded(.down) + 1 - position) / speed : nil
        if let nextLyric, nextLyric > position { delay = min(delay ?? .infinity, (nextLyric - position) / speed) }
        return delay.map { max($0, 0.05) }
    }
}
