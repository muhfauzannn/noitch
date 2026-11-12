import CryptoKit
import Foundation
import os

/// Finds lyrics on LRCLIB and keeps them on disk, so a track is only ever looked up once.
final class LyricsSource {
    private static let logger = Logger(subsystem: "app.noitch.mac", category: "Lyrics")
    private static let cacheFolder = FileManager.default
        .urls(for: .cachesDirectory, in: .userDomainMask).first?
        .appendingPathComponent("Noitch/Lyrics", isDirectory: true)

    private var task: Task<Void, Never>?

    func cancel() {
        task?.cancel()
        task = nil
    }

    /// Replaces any lookup still running. `completion` isn't called for a cancelled lookup.
    func lookUp(_ track: NowPlaying, completion: @escaping (Lyrics) -> Void) {
        cancel()
        let title = Lyrics.cleanTitle(track.title)
        let (artist, album, duration) = (track.artist, track.album, track.duration)
        task = Task {
            let key = Self.cacheKey(title: title, artist: artist)
            if let cached = Self.readCache(key) {
                completion(LyricRomanizer.prepare(cached.lyrics))
                return
            }
            let record = await Self.fetch(title: title, artist: artist, album: album, duration: duration)
            guard !Task.isCancelled else { return }
            if let record { Self.writeCache(record, key: key) }
            completion(LyricRomanizer.prepare(record?.lyrics ?? Lyrics()))
        }
    }

    private static func fetch(title: String, artist: String, album: String, duration: TimeInterval) async -> LyricsRecord? {
        let configuration = URLSessionConfiguration.ephemeral
        configuration.timeoutIntervalForRequest = 10
        configuration.httpAdditionalHeaders = ["User-Agent": "Noitch (https://github.com/muhfauzannn/noitch)"]
        let session = URLSession(configuration: configuration)
        defer { session.finishTasksAndInvalidate() }

        var exact = [
            URLQueryItem(name: "track_name", value: title),
            URLQueryItem(name: "artist_name", value: artist),
        ]
        let searchItems = exact
        if !album.isEmpty, duration > 0 {
            exact.append(URLQueryItem(name: "album_name", value: album))
            exact.append(URLQueryItem(name: "duration", value: String(Int(duration.rounded()))))
            if let record: LyricsRecord = await get("api/get", exact, session), record.syncedLyrics?.isEmpty == false {
                return record
            }
        }
        let records: [LyricsRecord] = await get("api/search", searchItems, session) ?? []
        return LyricsRecord.best(of: records, duration: duration)
    }

    private static func get<Value: Decodable>(_ path: String, _ items: [URLQueryItem], _ session: URLSession) async -> Value? {
        var components = URLComponents(string: "https://lrclib.net/\(path)")
        components?.queryItems = items
        guard let url = components?.url else { return nil }
        do {
            let (data, response) = try await session.data(from: url)
            guard (response as? HTTPURLResponse)?.statusCode == 200 else { return nil }
            return try JSONDecoder().decode(Value.self, from: data)
        } catch {
            logger.debug("Lyrics lookup failed: \(error.localizedDescription)")
            return nil
        }
    }

    private static func cacheKey(title: String, artist: String) -> String {
        let digest = SHA256.hash(data: Data("\(artist)\n\(title)".lowercased().utf8))
        return digest.map { String(format: "%02x", $0) }.joined()
    }

    private static func readCache(_ key: String) -> LyricsRecord? {
        guard let file = cacheFolder?.appendingPathComponent(key), let data = try? Data(contentsOf: file) else { return nil }
        return try? JSONDecoder().decode(LyricsRecord.self, from: data)
    }

    /// Misses aren't stored, so a track that gets lyrics later is found next time.
    private static func writeCache(_ record: LyricsRecord, key: String) {
        guard let folder = cacheFolder, let data = try? JSONEncoder().encode(record) else { return }
        try? FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
        try? data.write(to: folder.appendingPathComponent(key), options: .atomic)
    }
}
