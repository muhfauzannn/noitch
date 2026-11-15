import AppKit

/// Owns the closed island's music state: whether the wings show, and what is in them. The island
/// asks it for `isActive` to size the closed pill and is told when that changes. Everything here is
/// driven by now playing updates and one one-shot timer for the end of a pause.
final class LiveController {
    /// Set by the island, which owns the panel and resizes the pill to `isActive` and `showsLyric`.
    var onShapeChange: (() -> Void)?
    var onSceneCreated: ((CALayer) -> Void)?

    private(set) var isActive = false
    /// The pill also shows the line being sung, under the notch.
    private(set) var showsLyric = false
    private(set) var lyricWidth: CGFloat = 0
    private var maxLyricWidth: CGFloat = 580
    private let monitor: NowPlayingMonitor
    private var enabled: Bool
    private var tintBars: Bool
    private var layout: LiveLayout?
    private var scale: CGFloat = 2
    private var scene: LiveScene?
    private var nowPlaying = NowPlaying()
    private var shownArtwork: Data?
    private var pausedSince: Double?
    private var pauseTimer: Timer?
    private var releaseTimer: Timer?
    private var isIslandOpen = false

    /// The lyric line has its own lookup; LRCLIB results are cached on disk, so Home's lookup for
    /// the same track reads the file instead of the network.
    private lazy var lyricsSource = LyricsSource()
    private var lyrics: Lyrics?
    private var lyricsKey: String?
    private var lyricsEnabled: Bool
    private var lyricsOffset: Double
    /// One shot at the next line, only while closed, playing and showing a line.
    private var lyricTimer: Timer?
    /// A task alert has the strip under the notch for now.
    private var lyricHeld = false

    init(monitor: NowPlayingMonitor, settings: HomeSettings) {
        self.monitor = monitor
        enabled = settings.showLive
        tintBars = settings.tintWithArtwork
        lyricsEnabled = settings.showLiveLyrics && settings.showLyrics
        lyricsOffset = settings.lyricsOffset
        monitor.onLiveUpdate = { [unowned self] update in receive(update) }
        monitor.setNeeded(enabled, by: .live)
    }

    func apply(_ settings: HomeSettings) {
        let tintChanged = settings.tintWithArtwork != tintBars
        tintBars = settings.tintWithArtwork
        if settings.showLive != enabled {
            enabled = settings.showLive
            monitor.setNeeded(enabled, by: .live)
        }
        if tintChanged { updateArtwork(force: true) }
        lyricsOffset = settings.lyricsOffset
        let lyricsWanted = settings.showLiveLyrics && settings.showLyrics
        if lyricsWanted != lyricsEnabled {
            lyricsEnabled = lyricsWanted
            if !lyricsWanted { dropLyrics() }
        }
        requestLyricsIfNeeded()
        evaluate()
    }

    /// The closed pill without wings, for laying the wings out beside it.
    func setCollapsedSize(_ size: CGSize, scale: CGFloat, maxLyricWidth: CGFloat) {
        let layout = LiveLayout(collapsed: size)
        guard layout != self.layout || scale != self.scale || maxLyricWidth != self.maxLyricWidth else { return }
        self.layout = layout
        self.scale = scale
        self.maxLyricWidth = max(layout.size.width, maxLyricWidth)
        scene?.root.removeFromSuperlayer()
        scene = nil
        if isActive {
            buildScene()
            if !isIslandOpen { scene?.show(true) }
        }
        evaluate()
    }

    var size: CGSize? { layout?.size }

    func setPillWidth(_ width: CGFloat, animated: Bool) { scene?.setPillWidth(width, animated: animated) }

    /// While a task alert has the strip, the line steps aside; it comes back when the alert ends.
    func setLyricHeld(_ held: Bool) {
        guard held != lyricHeld else { return }
        lyricHeld = held
        updateLyric(animated: true)
    }

    func islandWillOpen() {
        isIslandOpen = true
        scene?.show(false)
        lyricTimer?.invalidate()
        lyricTimer = nil
    }

    func islandWillClose() {
        isIslandOpen = false
        // After the pill has narrowed most of the way back to the wings.
        if isActive { scene?.show(true, delay: 0.22) }
        updateLyric(animated: false)
    }

    private func receive(_ update: NowPlaying) {
        let wasPlaying = nowPlaying.isPlaying && nowPlaying.hasTrack
        nowPlaying = update
        let playing = update.isPlaying && update.hasTrack
        if playing {
            pausedSince = nil
            pauseTimer?.invalidate()
            pauseTimer = nil
        } else if wasPlaying {
            pausedSince = Date().timeIntervalSince1970
            let timer = Timer(timeInterval: LiveRule.pauseGrace + 0.1, target: self, selector: #selector(pauseEnded), userInfo: nil, repeats: false)
            timer.tolerance = 0.5
            RunLoop.main.add(timer, forMode: .common)
            pauseTimer = timer
        }
        updateArtwork(force: false)
        scene?.setPlaying(playing)
        requestLyricsIfNeeded()
        evaluate()
    }

    // MARK: Lyric line

    private func requestLyricsIfNeeded() {
        // Without an artist this is usually a browser tab, whose title is neither a song nor ours to send.
        guard enabled, lyricsEnabled, nowPlaying.hasTrack, !nowPlaying.artist.isEmpty else { return }
        #if DEBUG
        if monitor.isDemo {
            if lyrics == nil { lyrics = Lyrics(synced: (0..<60).map { LyricLine(start: Double($0) * 4, text: "Sample lyric line number \($0)") }) }
            return
        }
        #endif
        let key = "\(nowPlaying.artist)\n\(nowPlaying.title)"
        guard key != lyricsKey else { return }
        lyricsKey = key
        lyrics = nil
        lyricsSource.lookUp(nowPlaying) { [weak self] found in
            guard let self, lyricsKey == key else { return }
            lyrics = found
            evaluate()
        }
    }

    private func dropLyrics() {
        lyricsSource.cancel()
        lyrics = nil
        lyricsKey = nil
    }

    private var currentLyric: String? {
        LiveRule.lyricLine(lyrics, position: nowPlaying.position(at: Date()), offset: lyricsOffset)
    }

    /// Shows the current line and waits, in one one-shot timer, for the next.
    private func updateLyric(animated: Bool) {
        lyricTimer?.invalidate()
        lyricTimer = nil
        let line = showsLyric && !lyricHeld ? currentLyric : nil
        let measured = line.map { ($0 as NSString).size(withAttributes: [.font: NSFont.systemFont(ofSize: LiveLayout.lyricFontSize, weight: .semibold)]).width } ?? 0
        let baseWidth = layout?.size.width ?? 0
        let width = line == nil ? baseWidth : LiveLayout.widthForLyric(textWidth: measured, baseWidth: baseWidth, maximum: maxLyricWidth)
        if lyricWidth != width {
            lyricWidth = width
            onShapeChange?()
        }
        scene?.setLyric(line, animated: animated)
        guard showsLyric, !lyricHeld, !isIslandOpen, nowPlaying.isPlaying, let lyrics else { return }
        let position = nowPlaying.position(at: Date())
        guard let next = lyrics.nextChange(after: position + lyricsOffset) else { return }
        let delay = max((next - lyricsOffset - position) / max(nowPlaying.rate, 0.1), 0.05)
        let timer = Timer(timeInterval: delay, repeats: false) { [unowned self] _ in
            MainActor.assumeIsolated { updateLyric(animated: true) }
        }
        timer.tolerance = 0.03
        RunLoop.main.add(timer, forMode: .common)
        lyricTimer = timer
    }

    @objc private func pauseEnded() {
        pauseTimer = nil
        evaluate()
    }

    private func evaluate() {
        let active = LiveRule.shows(nowPlaying, enabled: enabled, pausedSince: pausedSince, now: Date().timeIntervalSince1970) && layout != nil
        let lyric = active && lyricsEnabled && currentLyric != nil
        defer { updateLyric(animated: true) }
        guard active != isActive || lyric != showsLyric else { return }
        let shapeOnly = active == isActive
        isActive = active
        showsLyric = lyric
        guard !shapeOnly else {
            onShapeChange?()
            return
        }
        if active {
            releaseTimer?.invalidate()
            releaseTimer = nil
            if scene == nil { buildScene() }
            // Waits for the pill to widen.
            if !isIslandOpen { scene?.show(true, delay: 0.12) }
        } else {
            scene?.show(false)
            let timer = Timer(timeInterval: 2, target: self, selector: #selector(releaseScene), userInfo: nil, repeats: false)
            timer.tolerance = 1
            RunLoop.main.add(timer, forMode: .common)
            releaseTimer = timer
        }
        onShapeChange?()
    }

    private func buildScene() {
        guard let layout else { return }
        let scene = LiveScene(layout: layout, scale: scale, lyricWidth: maxLyricWidth)
        self.scene = scene
        shownArtwork = nil
        onSceneCreated?(scene.root)
        updateArtwork(force: true)
        scene.setPlaying(nowPlaying.isPlaying && nowPlaying.hasTrack)
    }

    /// Nothing holding an image outlives the wings.
    @objc private func releaseScene() {
        releaseTimer = nil
        guard !isActive else { return }
        scene?.root.removeFromSuperlayer()
        scene = nil
        shownArtwork = nil
        dropLyrics()
    }

    private func updateArtwork(force: Bool) {
        guard let scene, let layout, force || nowPlaying.artwork != shownArtwork else { return }
        shownArtwork = nowPlaying.artwork
        let image = nowPlaying.artwork.flatMap { ArtworkImage.decode($0, maxPixelSize: layout.artSide * scale) }
        scene.setArtwork(image, tint: tintBars ? image.flatMap(ArtworkImage.averageColor) : nil)
    }
}
