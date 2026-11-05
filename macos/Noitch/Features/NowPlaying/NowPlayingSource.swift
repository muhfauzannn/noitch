import Foundation
import os

/// Reads system-wide now playing from the MediaRemote adapter and sends it commands.
///
/// macOS has no public API for this, and since 15.4 MediaRemote only answers platform binaries, so
/// the adapter runs inside `/usr/bin/perl`. That child process exists only while the island is open
/// or was open moments ago, so at rest there is nothing running. Commands are short-lived children
/// too. Seeking is the exception, see `seek`.
final class NowPlayingSource {
    /// MRCommand values.
    enum Command: Int {
        case togglePlayPause = 2
        case next = 4
        case previous = 5
    }

    private static let logger = Logger(subsystem: "app.noitch.mac", category: "NowPlaying")

    private let onUpdate: (NowPlaying) -> Void
    private var state = NowPlaying()
    private var stream: Process?
    private var reader: DispatchSourceRead?
    private var buffer = Data()
    /// Kept until they exit, so each one is reaped.
    private var commands: [Process] = []
    private var setElapsedTime: (@convention(c) (Double) -> Void)?

    init(onUpdate: @escaping (NowPlaying) -> Void) {
        self.onUpdate = onUpdate
    }

    var isRunning: Bool { stream != nil }

    func start() {
        guard stream == nil, let process = Self.makeProcess(arguments: ["stream"]) else { return }
        let pipe = Pipe()
        process.standardOutput = pipe
        do {
            try process.run()
        } catch {
            Self.logger.error("Adapter failed to start: \(error.localizedDescription)")
            return
        }
        stream = process

        // On the main queue: the run loop wakes only when a line arrives, and no thread is added.
        let descriptor = pipe.fileHandleForReading.fileDescriptor
        let reader = DispatchSource.makeReadSource(fileDescriptor: descriptor, queue: .main)
        reader.setEventHandler { [weak self] in
            MainActor.assumeIsolated { self?.readAvailable(from: descriptor) }
        }
        // Holding the handle here keeps the descriptor open for as long as the source can read it.
        let handle = pipe.fileHandleForReading
        reader.setCancelHandler { try? handle.close() }
        reader.resume()
        self.reader = reader
    }

    func stop() {
        reader?.cancel()
        reader = nil
        stream?.terminate()
        stream = nil
        buffer = Data()
        // A new stream starts with a full update, so nothing from this one may leak into it.
        state = NowPlaying()
    }

    func send(_ command: Command) { run(["send", String(command.rawValue)]) }

    /// The adapter's `seek` reports failure on macOS 26.6 where this call works, so this is the one
    /// place MediaRemote enters this process, and only once the slider is first used.
    func seek(to position: TimeInterval) {
        if setElapsedTime == nil {
            let framework = "/System/Library/PrivateFrameworks/MediaRemote.framework/MediaRemote"
            guard let handle = dlopen(framework, RTLD_LAZY), let symbol = dlsym(handle, "MRMediaRemoteSetElapsedTime") else {
                Self.logger.error("MediaRemote has no seek function")
                return
            }
            setElapsedTime = unsafeBitCast(symbol, to: (@convention(c) (Double) -> Void).self)
        }
        setElapsedTime?(position)
    }

    func setShuffle(_ isOn: Bool) { run(["shuffle", isOn ? "3" : "1"]) }

    func setRepeat(_ mode: RepeatMode) { run(["repeat", String(mode.rawValue)]) }

    private func run(_ arguments: [String]) {
        guard let process = Self.makeProcess(arguments: arguments) else { return }
        commands.removeAll { !$0.isRunning }
        do {
            try process.run()
            commands.append(process)
        } catch {
            Self.logger.error("Adapter command failed: \(error.localizedDescription)")
        }
    }

    private func readAvailable(from descriptor: Int32) {
        var chunk = [UInt8](repeating: 0, count: 65_536)
        let count = read(descriptor, &chunk, chunk.count)
        guard count > 0 else {
            // The child exited. The next expansion starts a new one; retrying here could spin.
            if count == 0 { stop() }
            return
        }
        buffer.append(contentsOf: chunk[..<count])
        while let newline = buffer.firstIndex(of: 0x0A) {
            handle(line: buffer[buffer.startIndex..<newline])
            buffer.removeSubrange(buffer.startIndex...newline)
        }
    }

    private func handle(line: Data) {
        guard !line.isEmpty else { return }
        do {
            state = state.applying(try JSONDecoder().decode(AdapterLine.self, from: line), now: Date())
            onUpdate(state)
        } catch {
            Self.logger.error("Unreadable adapter line: \(error.localizedDescription)")
        }
    }

    private static func makeProcess(arguments: [String]) -> Process? {
        guard let script = Bundle.main.url(forResource: "mediaremote-adapter", withExtension: "pl"),
              let frameworks = Bundle.main.privateFrameworksURL else {
            logger.error("Adapter is missing from the app bundle")
            return nil
        }
        let framework = frameworks.appendingPathComponent("MediaRemoteAdapter.framework")
        let process = Process()
        process.executableURL = URL(fileURLWithPath: "/usr/bin/perl")
        process.arguments = [script.path, framework.path] + arguments
        process.standardError = FileHandle.nullDevice
        process.standardOutput = FileHandle.nullDevice
        return process
    }
}
