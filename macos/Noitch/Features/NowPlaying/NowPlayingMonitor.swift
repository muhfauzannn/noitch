import AppKit

/// The one now playing stream, shared by Home and the closed island so there is never a second
/// adapter. It runs while Home is open (and its grace period), and, when the closed island shows
/// music, while a media player is running. Launches and quits arrive as workspace notifications, so
/// nothing is polled. This widening of the adapter's limits was approved on 2026-11-11.
final class NowPlayingMonitor: NSObject {
    enum Listener {
        case home
        case live
    }

    var onHomeUpdate: ((NowPlaying) -> Void)?
    var onLiveUpdate: ((NowPlaying) -> Void)?

    private(set) var nowPlaying = NowPlaying()
    private(set) lazy var source = NowPlayingSource { [unowned self] in publish($0) }
    private var needs: Set<Listener> = []
    private var playerRunning = false
    private var isWatching = false

    #if DEBUG
    /// `--demo-media` shows sample data with no adapter and no network, for checking the layout.
    let isDemo = ProcessInfo.processInfo.arguments.contains("--demo-media")
    #else
    let isDemo = false
    #endif

    /// Home wants updates while it is open or holding its layers.
    func setNeeded(_ needed: Bool, by listener: Listener) {
        guard needed != needs.contains(listener) else { return }
        if needed { needs.insert(listener) } else { needs.remove(listener) }
        if listener == .live { watchPlayers(needed) }
        update()
    }

    func stop() {
        watchPlayers(false)
        source.stop()
    }

    private func update() {
        if isDemo {
            publish(needs.isEmpty ? NowPlaying() : Self.demo())
            return
        }
        let running = needs.contains(.home) || (needs.contains(.live) && playerRunning)
        if running {
            source.start()
        } else if source.isRunning {
            source.stop()
            publish(NowPlaying())
        }
    }

    private func publish(_ update: NowPlaying) {
        nowPlaying = update
        onHomeUpdate?(update)
        onLiveUpdate?(update)
    }

    private func watchPlayers(_ watching: Bool) {
        guard watching != isWatching else { return }
        isWatching = watching
        let center = NSWorkspace.shared.notificationCenter
        guard watching else {
            center.removeObserver(self)
            playerRunning = false
            return
        }
        playerRunning = Self.anyPlayerRunning()
        for name in [NSWorkspace.didLaunchApplicationNotification, NSWorkspace.didTerminateApplicationNotification] {
            center.addObserver(self, selector: #selector(applicationsChanged(_:)), name: name, object: nil)
        }
    }

    @objc private func applicationsChanged(_ notification: Notification) {
        let app = notification.userInfo?[NSWorkspace.applicationUserInfoKey] as? NSRunningApplication
        guard MediaPlayers.isPlayer(app?.bundleIdentifier) else { return }
        playerRunning = Self.anyPlayerRunning()
        update()
    }

    private static func anyPlayerRunning() -> Bool {
        NSWorkspace.shared.runningApplications.contains { MediaPlayers.isPlayer($0.bundleIdentifier) }
    }

    private static func demo() -> NowPlaying {
        var demo = NowPlaying()
        demo.title = "Sample Track With a Long Enough Title to Need Truncating in the Column"
        demo.artist = "Sample Artist"
        demo.album = "Sample Album"
        demo.duration = 215
        demo.elapsed = 62
        demo.timestamp = Date()
        demo.isPlaying = true
        demo.artwork = demoArtwork()
        return demo
    }

    /// A strongly coloured square, so the artwork tint shows in demo screenshots.
    private static func demoArtwork() -> Data? {
        guard let bitmap = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: 64, pixelsHigh: 64, bitsPerSample: 8, samplesPerPixel: 4,
                                            hasAlpha: true, isPlanar: false, colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0) else { return nil }
        NSGraphicsContext.saveGraphicsState()
        NSGraphicsContext.current = NSGraphicsContext(bitmapImageRep: bitmap)
        NSGradient(starting: NSColor(srgbRed: 0.95, green: 0.35, blue: 0.2, alpha: 1), ending: NSColor(srgbRed: 0.6, green: 0.1, blue: 0.5, alpha: 1))?
            .draw(in: NSRect(x: 0, y: 0, width: 64, height: 64), angle: 45)
        NSGraphicsContext.restoreGraphicsState()
        return bitmap.representation(using: .png, properties: [:])
    }
}
