import Foundation
import os

/// Reads Claude Code's and Codex's local logs. There is no event to wait for that is cheaper than a
/// look, so it reads only when asked: when the page is shown, and once a minute while it stays shown.
/// Each look reads only bytes appended since the last one, from a mapped file that never counts
/// toward our footprint. Nothing leaves the machine.
final class UsageSource {
    /// The block estimate can reach back further than today, so a day is kept.
    static let window: Double = 24 * 3600

    private struct FileState {
        var offset = 0
        var codex = CodexFileState()
    }

    private static let logger = Logger(subsystem: "Noitch", category: "Usage")
    private let claudeRoots: [URL]
    private let codexRoots: [URL]
    private var log = UsageLog()
    private var files: [String: FileState] = [:]

    init(environment: [String: String] = ProcessInfo.processInfo.environment) {
        let home = FileManager.default.homeDirectoryForCurrentUser
        let claude = environment["CLAUDE_CONFIG_DIR"].map { $0.split(separator: ",").map { URL(fileURLWithPath: String($0)) } }
            ?? [home.appending(path: ".config/claude"), home.appending(path: ".claude")]
        claudeRoots = claude.map { $0.appending(path: "projects") }
        codexRoots = [(environment["CODEX_HOME"].map { URL(fileURLWithPath: $0) } ?? home.appending(path: ".codex")).appending(path: "sessions")]
    }

    func refresh(now: Double) -> UsageLog {
        let cutoff = now - Self.window
        let start = ContinuousClock.now
        var seen = Set<String>()
        for root in claudeRoots { scan(root, tool: .claude, cutoff: cutoff, seen: &seen) }
        for root in codexRoots { scan(root, tool: .codex, cutoff: cutoff, seen: &seen) }
        // A file that has gone quiet for a day can't add anything inside the window.
        files = files.filter { seen.contains($0.key) }
        log.prune(before: cutoff, samplesBefore: now - 2 * LimitRates.recentSpan)
        Self.logger.debug("Usage read in \(ContinuousClock.now - start)")
        return log
    }

    private func scan(_ root: URL, tool: AITool, cutoff: Double, seen: inout Set<String>) {
        let keys: [URLResourceKey] = [.contentModificationDateKey, .fileSizeKey, .isRegularFileKey]
        guard let enumerator = FileManager.default.enumerator(at: root, includingPropertiesForKeys: keys, options: [.skipsHiddenFiles]) else { return }
        for case let url as URL in enumerator where url.pathExtension == "jsonl" {
            guard let values = try? url.resourceValues(forKeys: Set(keys)), values.isRegularFile == true,
                  let modified = values.contentModificationDate?.timeIntervalSince1970, modified >= cutoff,
                  let size = values.fileSize else { continue }
            let path = url.path
            seen.insert(path)
            var state = files[path] ?? FileState()
            if size < state.offset { state = FileState() }
            guard size > state.offset else { continue }
            guard let data = try? Data(contentsOf: url, options: .alwaysMapped) else { continue }
            let offset = state.offset
            let used = data.withUnsafeBytes { raw -> Int in
                let rest = UnsafeRawBufferPointer(rebasing: raw[min(offset, raw.count)...])
                return tool == .claude ? log.readClaude(rest, since: cutoff) : log.readCodex(rest, state: &state.codex, since: cutoff)
            }
            state.offset += used
            files[path] = state
        }
    }
}
