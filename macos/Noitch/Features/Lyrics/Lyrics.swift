import Foundation

struct LyricLine: Equatable {
    var start: TimeInterval
    var text: String
    var romanization: String = ""
}

/// Lyrics for one track. Pure values: parsing, matching and timing are all tested without a network.
struct Lyrics: Equatable {
    /// Sorted by start time. Empty if only plain lyrics exist.
    var synced: [LyricLine] = []
    var plain: [String] = []
    var isInstrumental = false

    var isEmpty: Bool { synced.isEmpty && plain.isEmpty && !isInstrumental }

    /// The line being sung at `position`, or nil before the first line.
    func lineIndex(at position: TimeInterval) -> Int? {
        var low = 0
        var high = synced.count
        while low < high {
            let middle = (low + high) / 2
            if synced[middle].start <= position { low = middle + 1 } else { high = middle }
        }
        return low == 0 ? nil : low - 1
    }

    /// When the visible line next changes, so one one-shot timer can replace polling.
    func nextChange(after position: TimeInterval) -> TimeInterval? {
        let next = lineIndex(at: position).map { $0 + 1 } ?? 0
        return next < synced.count ? synced[next].start : nil
    }

    /// Parses LRC text. A line may carry several timestamps. Metadata tags such as `[ar:Name]` are
    /// not timestamps, so they fall out here.
    static func parseLRC(_ text: String) -> [LyricLine] {
        var lines: [LyricLine] = []
        for rawLine in text.split(whereSeparator: \.isNewline) {
            var rest = Substring(rawLine)
            var starts: [TimeInterval] = []
            while rest.first == "[", let close = rest.firstIndex(of: "]") {
                guard let time = parseTimestamp(rest[rest.index(after: rest.startIndex)..<close]) else { break }
                starts.append(time)
                rest = rest[rest.index(after: close)...]
            }
            let lyric = rest.trimmingCharacters(in: .whitespaces)
            lines += starts.map { LyricLine(start: $0, text: lyric) }
        }
        return lines.enumerated()
            .sorted { ($0.element.start, $0.offset) < ($1.element.start, $1.offset) }
            .map(\.element)
    }

    private static func parseTimestamp(_ tag: Substring) -> TimeInterval? {
        let parts = tag.split(separator: ":")
        guard parts.count == 2, let minutes = Double(parts[0]), let seconds = Double(parts[1]) else { return nil }
        return minutes * 60 + seconds
    }

    /// Removes what a player adds to a title but a lyrics database doesn't have, such as
    /// "(feat. Someone)" or " - Remastered 2011".
    static func cleanTitle(_ title: String) -> String {
        let noise = ["feat", "ft.", "with ", "remaster", "live", "official", "lyric", "video", "audio", "version", "mono", "stereo", "deluxe", "edit"]
        func isNoise(_ text: some StringProtocol) -> Bool {
            let lowered = text.lowercased()
            return noise.contains { lowered.contains($0) }
        }
        var result = ""
        var depth = 0
        var group = ""
        for character in title {
            if "([".contains(character) {
                if depth > 0 { group.append(character) }
                depth += 1
            } else if ")]".contains(character), depth > 0 {
                depth -= 1
                if depth == 0 {
                    if !isNoise(group) { result += "(\(group))" }
                    group = ""
                } else {
                    group.append(character)
                }
            } else if depth > 0 {
                group.append(character)
            } else {
                result.append(character)
            }
        }
        if let dash = result.range(of: " - ", options: .backwards), isNoise(result[dash.upperBound...]) {
            result = String(result[..<dash.lowerBound])
        }
        return result.split(whereSeparator: \.isWhitespace).joined(separator: " ")
    }
}

/// An LRCLIB result.
struct LyricsRecord: Codable, Equatable {
    /// A longer difference than this is probably another recording of the song.
    static let maxDurationMismatch: TimeInterval = 15

    var duration: Double?
    var instrumental: Bool?
    var plainLyrics: String?
    var syncedLyrics: String?

    var lyrics: Lyrics {
        Lyrics(
            synced: Lyrics.parseLRC(syncedLyrics ?? ""),
            plain: (plainLyrics ?? "").split(whereSeparator: \.isNewline).map(String.init),
            isInstrumental: instrumental ?? false
        )
    }

    /// Synced lyrics beat plain ones; among those, the closest length wins.
    static func best(of records: [LyricsRecord], duration: TimeInterval) -> LyricsRecord? {
        func mismatch(_ record: LyricsRecord) -> TimeInterval {
            guard duration > 0, let length = record.duration else { return 0 }
            return abs(length - duration)
        }
        func score(_ record: LyricsRecord) -> Double {
            (record.syncedLyrics?.isEmpty == false ? 1000 : 0) - mismatch(record)
        }
        let matches = records.filter { !$0.lyrics.isEmpty && mismatch($0) <= maxDurationMismatch }
        return matches.max { score($0) < score($1) }
    }
}
