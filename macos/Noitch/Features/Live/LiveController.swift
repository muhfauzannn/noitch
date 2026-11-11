import AppKit

/// Owns the closed island's music state: whether the wings show, and what is in them. The island
/// asks it for `isActive` to size the closed pill and is told when that changes. Everything here is
/// driven by now playing updates and one one-shot timer for the end of a pause.
final class LiveController {
    /// Set by the island, which owns the panel and resizes the pill.
    var onActiveChange: ((Bool) -> Void)?
    var onSceneCreated: ((CALayer) -> Void)?

    private(set) var isActive = false
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

    init(monitor: NowPlayingMonitor, settings: HomeSettings) {
        self.monitor = monitor
        enabled = settings.showLive
        tintBars = settings.tintWithArtwork
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
        evaluate()
    }

    /// The closed pill without wings, for laying the wings out beside it.
    func setCollapsedSize(_ size: CGSize, scale: CGFloat) {
        let layout = LiveLayout(collapsed: size)
        guard layout != self.layout || scale != self.scale else { return }
        self.layout = layout
        self.scale = scale
        scene?.root.removeFromSuperlayer()
        scene = nil
        if isActive {
            buildScene()
            if !isIslandOpen { scene?.show(true) }
        }
        evaluate()
    }

    var size: CGSize? { layout?.size }

    func islandWillOpen() {
        isIslandOpen = true
        scene?.show(false)
    }

    func islandWillClose() {
        isIslandOpen = false
        // After the pill has narrowed most of the way back to the wings.
        if isActive { scene?.show(true, delay: 0.22) }
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
        evaluate()
    }

    @objc private func pauseEnded() {
        pauseTimer = nil
        evaluate()
    }

    private func evaluate() {
        let active = LiveRule.shows(nowPlaying, enabled: enabled, pausedSince: pausedSince, now: Date().timeIntervalSince1970) && layout != nil
        guard active != isActive else { return }
        isActive = active
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
        onActiveChange?(active)
    }

    private func buildScene() {
        guard let layout else { return }
        let scene = LiveScene(layout: layout, scale: scale)
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
    }

    private func updateArtwork(force: Bool) {
        guard let scene, let layout, force || nowPlaying.artwork != shownArtwork else { return }
        shownArtwork = nowPlaying.artwork
        let image = nowPlaying.artwork.flatMap { ArtworkImage.decode($0, maxPixelSize: layout.artSide * scale) }
        scene.setArtwork(image, tint: tintBars ? image.flatMap(ArtworkImage.averageColor) : nil)
    }
}
