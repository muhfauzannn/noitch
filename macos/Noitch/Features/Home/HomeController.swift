import AppKit
import ImageIO

/// Owns everything Home knows: what is playing, its lyrics, and how the pointer is using the controls.
/// The island tells it when it opens and closes, and it keeps nothing alive while closed.
final class HomeController {
    /// A quick return to the island shouldn't pay for a new adapter and new layers.
    private static let releaseDelay: TimeInterval = 10

    /// Set by the island, which owns the panel and adds the layers to its view.
    var onSceneCreated: ((CALayer) -> Void)?

    private var settings: AppSettings
    private var layout: HomeLayout?
    private var scale: CGFloat = 2
    private var scene: HomeScene?
    private lazy var source = NowPlayingSource { [unowned self] in receive($0) }
    private lazy var lyricsSource = LyricsSource()

    private var nowPlaying = NowPlaying()
    private var lyrics: Lyrics?
    private var lyricsKey: String?
    private var isLoadingLyrics = false
    private var showsLyricsPanel = false
    private var shownArtwork: Data?
    private var tint = NSColor.white
    private var isOpen = false

    private var tickTimer: Timer?
    private var releaseTimer: Timer?

    private var hovered: HomeControl?
    private var pressed: HomeControl?
    /// While set, the slider shows this instead of the playback clock.
    private var scrubFraction: Double?

    #if DEBUG
    /// `--demo-media` shows sample data with no adapter and no network, for checking the layout.
    private let isDemo = ProcessInfo.processInfo.arguments.contains("--demo-media")
    #else
    private let isDemo = false
    #endif

    init(settings: AppSettings) {
        self.settings = settings
    }

    /// True during a slider drag, which may leave the island without closing it.
    var isInteracting: Bool { pressed == .slider }

    func stop() {
        source.stop()
    }

    // MARK: Island lifecycle

    func islandWillOpen(layout: HomeLayout, scale: CGFloat) {
        releaseTimer?.invalidate()
        releaseTimer = nil
        isOpen = true
        if scene == nil || layout != self.layout || scale != self.scale {
            scene?.root.removeFromSuperlayer()
            let scene = HomeScene(layout: layout, scale: scale)
            self.scene = scene
            self.layout = layout
            self.scale = scale
            onSceneCreated?(scene.root)
            updateArtwork(force: true)
        }
        if isDemo { loadDemo() } else { source.start() }
        refresh()
    }

    /// The scene's layers, if they are still alive, so a new pager can take them back.
    var sceneRoot: CALayer? { scene?.root }

    func reveal(begin: CFTimeInterval) { scene?.reveal(begin: begin) }

    func islandWillClose() {
        isOpen = false
        tickTimer?.invalidate()
        tickTimer = nil
        hovered = nil
        pressed = nil
        scrubFraction = nil
        scene?.setHover(nil, isPressed: false)
        let timer = Timer(timeInterval: Self.releaseDelay, repeats: false) { [unowned self] _ in
            MainActor.assumeIsolated { release() }
        }
        timer.tolerance = 2
        RunLoop.main.add(timer, forMode: .common)
        releaseTimer = timer
    }

    /// Nothing that holds an image, text or a process outlives this.
    private func release() {
        releaseTimer = nil
        source.stop()
        lyricsSource.cancel()
        scene?.root.removeFromSuperlayer()
        scene = nil
        shownArtwork = nil
        nowPlaying = NowPlaying()
        lyrics = nil
        lyricsKey = nil
        isLoadingLyrics = false
    }

    func apply(_ settings: AppSettings) {
        self.settings = settings
        requestLyricsIfNeeded()
        updateArtwork(force: true)
        refresh()
    }

    // MARK: Now playing

    private func receive(_ update: NowPlaying) {
        nowPlaying = update
        requestLyricsIfNeeded()
        updateArtwork(force: false)
        refresh()
    }

    private func requestLyricsIfNeeded() {
        guard settings.showLyrics else {
            lyricsSource.cancel()
            isLoadingLyrics = false
            lyricsKey = nil
            return
        }
        // Without an artist this is usually a browser tab, whose title is neither a song nor ours to send.
        guard nowPlaying.hasTrack, !nowPlaying.artist.isEmpty, !isDemo else { return }
        let key = "\(nowPlaying.artist)\n\(nowPlaying.title)"
        guard key != lyricsKey else { return }
        lyricsKey = key
        lyrics = nil
        isLoadingLyrics = true
        lyricsSource.lookUp(nowPlaying) { [weak self] found in
            guard let self, lyricsKey == key else { return }
            lyrics = found
            isLoadingLyrics = false
            refresh()
        }
    }

    private func updateArtwork(force: Bool) {
        guard let scene, force || nowPlaying.artwork != shownArtwork else { return }
        shownArtwork = nowPlaying.artwork
        let size = (layout?.artwork.width ?? 0) * scale
        let image = nowPlaying.artwork.flatMap { Self.decode($0, maxPixelSize: size) }
        tint = settings.tintWithArtwork ? (image.flatMap(Self.averageColor) ?? .white) : .white
        scene.setArtwork(image, tint: tint)
    }

    /// Decodes straight to display size, so a large cover never exists in memory at full size.
    private static func decode(_ data: Data, maxPixelSize: CGFloat) -> CGImage? {
        guard let source = CGImageSourceCreateWithData(data as CFData, nil) else { return nil }
        let options: [CFString: Any] = [
            kCGImageSourceCreateThumbnailFromImageAlways: true,
            kCGImageSourceCreateThumbnailWithTransform: true,
            kCGImageSourceThumbnailMaxPixelSize: maxPixelSize,
        ]
        return CGImageSourceCreateThumbnailAtIndex(source, 0, options as CFDictionary)
    }

    /// Scaled to one pixel, then lifted so a dark cover still gives a visible bar.
    private static func averageColor(of image: CGImage) -> NSColor? {
        var pixel = [UInt8](repeating: 0, count: 4)
        let drawn = pixel.withUnsafeMutableBytes { bytes -> Bool in
            guard let context = CGContext(
                data: bytes.baseAddress, width: 1, height: 1, bitsPerComponent: 8, bytesPerRow: 4,
                space: CGColorSpaceCreateDeviceRGB(), bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue
            ) else { return false }
            context.interpolationQuality = .medium
            context.draw(image, in: CGRect(x: 0, y: 0, width: 1, height: 1))
            return true
        }
        guard drawn else { return nil }
        let color = NSColor(red: CGFloat(pixel[0]) / 255, green: CGFloat(pixel[1]) / 255, blue: CGFloat(pixel[2]) / 255, alpha: 1)
        var (hue, saturation, brightness, alpha): (CGFloat, CGFloat, CGFloat, CGFloat) = (0, 0, 0, 0)
        color.getHue(&hue, saturation: &saturation, brightness: &brightness, alpha: &alpha)
        return NSColor(hue: hue, saturation: saturation, brightness: max(brightness, 0.75), alpha: 1)
    }

    // MARK: Showing it

    private func currentPosition() -> TimeInterval {
        if let scrubFraction { return scrubFraction * nowPlaying.duration }
        return nowPlaying.position(at: Date())
    }

    /// Call when playback itself changed. A plain clock tick uses `tick`, which leaves the bar alone.
    private func refresh() {
        guard let scene, isOpen else { return }
        render()
        let duration = nowPlaying.duration
        let fraction = duration > 0 ? min(currentPosition() / duration, 1) : 0
        let moving = nowPlaying.isPlaying && duration > 0 && scrubFraction == nil
        scene.setProgress(fraction, speed: moving ? nowPlaying.rate / duration : 0)
        scheduleTick()
    }

    private func render() {
        scene?.apply(HomeModel(
            nowPlaying: nowPlaying,
            lyrics: lyrics,
            isLoadingLyrics: isLoadingLyrics,
            position: currentPosition(),
            showsPanel: showsLyricsPanel,
            settings: settings
        ))
    }

    /// One shot at a time, only while open and playing. Paused or closed, nothing is scheduled.
    private func scheduleTick() {
        tickTimer?.invalidate()
        tickTimer = nil
        guard isOpen, nowPlaying.isPlaying else { return }
        let position = currentPosition()
        let delay = HomeModel.nextTick(position: position, rate: nowPlaying.rate, nextLyric: lyrics?.nextChange(after: position))
        let timer = Timer(timeInterval: delay, repeats: false) { [unowned self] _ in
            MainActor.assumeIsolated {
                render()
                scheduleTick()
            }
        }
        timer.tolerance = 0.03
        RunLoop.main.add(timer, forMode: .common)
        tickTimer = timer
    }

    // MARK: Pointer

    func pointerMoved(to point: CGPoint) {
        guard let layout, pressed == nil else { return }
        let control = layout.control(at: point)
        guard control != hovered else { return }
        hovered = control
        scene?.setHover(control, isPressed: false)
    }

    func pointerDown(at point: CGPoint) {
        guard let layout, let control = layout.control(at: point) else { return }
        pressed = control
        scene?.setHover(control, isPressed: true)
        if control == .slider, nowPlaying.hasTrack, !showsLyricsPanel {
            scrubFraction = layout.sliderFraction(at: point.x)
            refresh()
        }
    }

    func pointerDragged(to point: CGPoint) {
        guard let layout, pressed == .slider, scrubFraction != nil else { return }
        scrubFraction = layout.sliderFraction(at: point.x)
        refresh()
    }

    func pointerUp(at point: CGPoint) {
        guard let layout, let control = pressed else { return }
        pressed = nil
        if control == .slider, let scrubFraction {
            finishScrub(at: scrubFraction)
        } else if layout.control(at: point) == control {
            activate(control)
        }
        hovered = layout.control(at: point)
        scene?.setHover(hovered, isPressed: false)
    }

    private func finishScrub(at fraction: Double) {
        scrubFraction = nil
        let position = fraction * nowPlaying.duration
        // Shown at once. The adapter's own update follows and corrects any difference.
        nowPlaying.elapsed = position
        nowPlaying.timestamp = Date()
        if !isDemo { source.seek(to: position) }
        refresh()
    }

    private func activate(_ control: HomeControl) {
        switch control {
        case .lyrics:
            guard settings.showLyrics else { return }
            showsLyricsPanel.toggle()
            refresh()
        case .artwork:
            openPlayer()
        case .slider:
            break
        case .playPause:
            guard nowPlaying.hasTrack else { return }
            nowPlaying.elapsed = nowPlaying.position(at: Date())
            nowPlaying.timestamp = Date()
            nowPlaying.isPlaying.toggle()
            if !isDemo { source.send(.togglePlayPause) }
            refresh()
        case .previous:
            if nowPlaying.hasTrack, !isDemo { source.send(.previous) }
        case .next:
            if nowPlaying.hasTrack, !isDemo { source.send(.next) }
        case .shuffle:
            guard nowPlaying.hasTrack, settings.showShuffleAndRepeat else { return }
            nowPlaying.isShuffled.toggle()
            if !isDemo { source.setShuffle(nowPlaying.isShuffled) }
            refresh()
        case .repeatMode:
            guard nowPlaying.hasTrack, settings.showShuffleAndRepeat else { return }
            nowPlaying.repeatMode = nowPlaying.repeatMode.next
            if !isDemo { source.setRepeat(nowPlaying.repeatMode) }
            refresh()
        }
    }

    private func openPlayer() {
        guard let identifier = nowPlaying.bundleIdentifier,
              let url = NSWorkspace.shared.urlForApplication(withBundleIdentifier: identifier) else { return }
        NSWorkspace.shared.openApplication(at: url, configuration: NSWorkspace.OpenConfiguration())
    }

    #if DEBUG
    private func loadDemo() {
        guard !nowPlaying.hasTrack else { return }
        var demo = NowPlaying()
        demo.title = "Sample Track With a Long Enough Title to Need Truncating in the Column"
        demo.artist = "Sample Artist"
        demo.album = "Sample Album"
        demo.duration = 215
        demo.elapsed = 62
        demo.timestamp = Date()
        demo.isPlaying = true
        nowPlaying = demo
        lyrics = Lyrics(synced: (0..<60).map { LyricLine(start: Double($0) * 4, text: "Sample lyric line number \($0)") })
    }
    #else
    private func loadDemo() {}
    #endif
}
