import AppKit
import ImageIO

/// Owns everything Home knows: what is playing, its lyrics, and how the pointer is using the controls.
/// The island tells it when it opens and closes, and it keeps nothing alive while closed.
final class HomeController {
    /// A quick return to the island shouldn't pay for a new adapter and new layers.
    private static let releaseDelay: TimeInterval = 10

    /// Set by the island, which owns the panel and adds the layers to its view.
    var onSceneCreated: ((CALayer) -> Void)?

    private var settings: HomeSettings
    private var layout: HomeLayout?
    private var scale: CGFloat = 2
    private var scene: HomeScene?
    private let monitor: NowPlayingMonitor
    private var source: NowPlayingSource { monitor.source }
    private lazy var lyricsSource = LyricsSource()

    private var nowPlaying = NowPlaying()
    private var lyrics: Lyrics?
    private var lyricsKey: String?
    private var isLoadingLyrics = false
    /// The title playing when next or previous was pressed, until the player reports another.
    private var skippingFrom: String?
    private var skipTimer: Timer?
    private var shownArtwork: Data?
    private var isOpen = false

    private var tickTimer: Timer?
    private var releaseTimer: Timer?

    private var hovered: HomeControl?
    private var pressed: HomeControl?
    /// While set, the slider shows this instead of the playback clock.
    private var scrubFraction: Double?

    private var isDemo: Bool { monitor.isDemo }

    init(settings: HomeSettings, monitor: NowPlayingMonitor) {
        self.settings = settings
        self.monitor = monitor
        monitor.onHomeUpdate = { [unowned self] update in if isHolding { receive(update) } }
    }

    /// True from opening until the grace period ends: Home keeps the stream running meanwhile.
    private var isHolding = false

    /// True during a slider drag, which may leave the island without closing it.
    var isInteracting: Bool { pressed == .slider }

    func stop() {
        monitor.stop()
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
        isHolding = true
        monitor.setNeeded(true, by: .home)
        if isDemo { loadDemo() }
        receive(monitor.nowPlaying)
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
        skipTimer?.invalidate()
        skipTimer = nil
        skippingFrom = nil
        isHolding = false
        monitor.setNeeded(false, by: .home)
        lyricsSource.cancel()
        scene?.root.removeFromSuperlayer()
        scene = nil
        shownArtwork = nil
        nowPlaying = NowPlaying()
        lyrics = nil
        lyricsKey = nil
        isLoadingLyrics = false
    }

    func apply(_ settings: HomeSettings) {
        self.settings = settings
        requestLyricsIfNeeded()
        updateArtwork(force: true)
        refresh()
    }

    // MARK: Now playing

    private func receive(_ update: NowPlaying) {
        nowPlaying = update
        if let skippingFrom, update.title != skippingFrom {
            self.skippingFrom = nil
            skipTimer?.invalidate()
            skipTimer = nil
        }
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
        let image = nowPlaying.artwork.flatMap { ArtworkImage.decode($0, maxPixelSize: size) }
        let tint = settings.tintWithArtwork || settings.artworkGlow ? image.flatMap(ArtworkImage.averageColor) : nil
        scene.setArtwork(image, barTint: settings.tintWithArtwork ? tint : nil, glow: settings.artworkGlow ? tint : nil)
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
        // While skipping, the bar empties at once: the next track starts from nothing.
        let fraction = duration > 0 && skippingFrom == nil ? min(currentPosition() / duration, 1) : 0
        let moving = nowPlaying.isPlaying && duration > 0 && scrubFraction == nil && skippingFrom == nil
        scene.setProgress(fraction, speed: moving ? nowPlaying.rate / duration : 0)
        scheduleTick()
    }

    private func render() {
        scene?.apply(HomeModel(
            nowPlaying: nowPlaying,
            lyrics: lyrics,
            isLoadingLyrics: isLoadingLyrics,
            position: currentPosition(),
            settings: settings,
            isSkipping: skippingFrom != nil
        ))
    }

    /// One shot at a time, only while open and playing. Paused or closed, nothing is scheduled.
    private func scheduleTick() {
        tickTimer?.invalidate()
        tickTimer = nil
        guard isOpen, nowPlaying.isPlaying else { return }
        let position = currentPosition()
        // Lyrics run on their own clock, shifted by the timing setting.
        let offset = settings.lyricsOffset
        let nextLyric = lyrics?.nextChange(after: position + offset).map { $0 - offset }
        let delay = HomeModel.nextTick(position: position, rate: nowPlaying.rate, nextLyric: nextLyric)
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
        if control == .slider, nowPlaying.hasTrack, skippingFrom == nil {
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
            guard nowPlaying.hasTrack else { return }
            if !isDemo { source.send(.previous) }
            beginSkip()
        case .next:
            guard nowPlaying.hasTrack else { return }
            if !isDemo { source.send(.next) }
            beginSkip()
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

    /// Answers the press at once: players can take a second or more to report the new track.
    /// Ends when another title arrives, or after a few seconds in case the same track restarted.
    private func beginSkip() {
        skippingFrom = nowPlaying.title
        skipTimer?.invalidate()
        let timer = Timer(timeInterval: 4, repeats: false) { [unowned self] _ in
            MainActor.assumeIsolated { endSkip() }
        }
        RunLoop.main.add(timer, forMode: .common)
        skipTimer = timer
        refresh()
    }

    private func endSkip() {
        skipTimer?.invalidate()
        skipTimer = nil
        guard skippingFrom != nil else { return }
        skippingFrom = nil
        refresh()
    }

    private func openPlayer() {
        guard let identifier = nowPlaying.bundleIdentifier,
              let url = NSWorkspace.shared.urlForApplication(withBundleIdentifier: identifier) else { return }
        NSWorkspace.shared.openApplication(at: url, configuration: NSWorkspace.OpenConfiguration())
    }

    #if DEBUG
    /// The track itself comes from the monitor's demo.
    private func loadDemo() {
        guard lyrics == nil else { return }
        lyrics = Lyrics(synced: (0..<60).map { LyricLine(start: Double($0) * 4, text: "Sample lyric line number \($0)") })
    }
    #else
    private func loadDemo() {}
    #endif
}
