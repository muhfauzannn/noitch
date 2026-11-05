import Foundation

/// MediaRemote's values, which the adapter passes through unchanged.
enum RepeatMode: Int {
    case off = 1
    case one = 2
    case all = 3

    var next: RepeatMode {
        switch self {
        case .off: .all
        case .all: .one
        case .one: .off
        }
    }
}

/// What is playing. Pure values, so merging the adapter's diffs can be tested without a process.
struct NowPlaying: Equatable {
    var title = ""
    var artist = ""
    var album = ""
    var duration: TimeInterval = 0
    /// Playback position at `timestamp`. The clock is derived from these, never pushed per second.
    var elapsed: TimeInterval = 0
    var timestamp = Date.distantPast
    var rate = 1.0
    var isPlaying = false
    var isShuffled = false
    var repeatMode = RepeatMode.off
    var bundleIdentifier: String?
    var artwork: Data?

    var hasTrack: Bool { !title.isEmpty }

    func position(at date: Date) -> TimeInterval {
        guard isPlaying else { return elapsed }
        let position = elapsed + date.timeIntervalSince(timestamp) * rate
        return duration > 0 ? min(max(position, 0), duration) : max(position, 0)
    }

    /// A diff leaves out what did not change. A full update leaves out what the player doesn't have.
    func applying(_ line: AdapterLine, now: Date) -> NowPlaying {
        let payload = line.payload
        let isDiff = line.diff ?? false
        var next = isDiff ? self : NowPlaying()
        next.title = payload.title ?? next.title
        next.artist = payload.artist ?? next.artist
        next.album = payload.album ?? next.album
        next.duration = payload.duration ?? next.duration
        next.rate = payload.playbackRate ?? next.rate
        next.isPlaying = payload.playing ?? next.isPlaying
        if let shuffleMode = payload.shuffleMode { next.isShuffled = shuffleMode != 1 }
        if let repeatMode = payload.repeatMode { next.repeatMode = RepeatMode(rawValue: repeatMode) ?? .off }
        next.bundleIdentifier = payload.parentApplicationBundleIdentifier
            ?? payload.bundleIdentifier
            ?? next.bundleIdentifier
        if let artwork = payload.artworkData {
            // Some sessions, such as browser tabs, send empty artwork. That means none.
            let data = Data(base64Encoded: artwork, options: .ignoreUnknownCharacters)
            next.artwork = data?.isEmpty == false ? data : nil
        }

        if let elapsed = payload.elapsedTime {
            next.elapsed = elapsed
            next.timestamp = payload.timestamp.flatMap(Self.parseDate) ?? now
        } else if isDiff, next.isPlaying != isPlaying {
            // The clock restarts from where it had reached, or a pause would jump back.
            next.elapsed = position(at: now)
            next.timestamp = now
        } else if !isDiff {
            next.timestamp = now
        }
        return next
    }

    private static func parseDate(_ text: String) -> Date? {
        (try? Date.ISO8601FormatStyle(includingFractionalSeconds: true).parse(text))
            ?? (try? Date.ISO8601FormatStyle().parse(text))
    }
}

/// One line of the adapter's `stream` output.
struct AdapterLine: Decodable {
    struct Payload: Decodable {
        var title: String?
        var artist: String?
        var album: String?
        var duration: Double?
        var elapsedTime: Double?
        var shuffleMode: Int?
        var repeatMode: Int?
        var artworkData: String?
        var timestamp: String?
        var playbackRate: Double?
        var playing: Bool?
        var parentApplicationBundleIdentifier: String?
        var bundleIdentifier: String?
    }

    var payload: Payload
    var diff: Bool?
}
