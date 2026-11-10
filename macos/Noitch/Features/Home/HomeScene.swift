import AppKit
import QuartzCore

/// Home's layers. It draws what it is given and decides nothing, and it never draws per frame: text
/// changes when a value changes, and the progress bar moves in the render server.
final class HomeScene {
    private static let artworkRadius: CGFloat = 14
    private static let dimmedOpacity: Float = 0.35
    private static let idleOpacity: Float = 0.55

    /// Placed by the pager, which owns its fade and slide.
    let root = CALayer()

    private let layout: HomeLayout
    private let artwork = CALayer()
    private let artworkPlaceholder = CAShapeLayer()
    private let title = HomeScene.makeText(size: 15, weight: .semibold, opacity: 1)
    private let artist = HomeScene.makeText(size: 13, weight: .regular, opacity: 0.6)
    private let lyricLine = HomeScene.makeText(size: 12, weight: .regular, opacity: 0.8)
    private let elapsed = HomeScene.makeText(size: 11, weight: .regular, opacity: 0.6)
    private let remaining = HomeScene.makeText(size: 11, weight: .regular, opacity: 0.6)
    private let panelLines = (0..<3).map { HomeScene.makeText(size: $0 == 1 ? 16 : 13, weight: $0 == 1 ? .semibold : .regular, opacity: $0 == 1 ? 1 : 0.45) }
    private let track = CALayer()
    private let fill = CALayer()
    /// One plate that moves to whichever button is under the pointer.
    private let hoverPlate = CALayer()
    private var icons: [HomeControl: CAShapeLayer] = [:]
    /// Artwork and lyrics button, text, slider, transport: revealed one after another, so the eye
    /// reads top-left to bottom instead of everything popping in at once.
    private let groups = (0..<4).map { _ in CALayer() }
    private var shownModel: HomeModel?

    init(layout: HomeLayout, scale: CGFloat) {
        self.layout = layout
        // The pager places it and flips its geometry, so the shared y-down numbers hold.
        root.bounds = CGRect(origin: .zero, size: layout.size)

        hoverPlate.backgroundColor = NSColor.white.withAlphaComponent(0.22).cgColor
        hoverPlate.isHidden = true
        root.addSublayer(hoverPlate)
        for group in groups {
            group.frame = root.bounds
            root.addSublayer(group)
        }

        artwork.frame = layout.artwork
        artwork.cornerRadius = Self.artworkRadius
        artwork.cornerCurve = .continuous
        artwork.masksToBounds = true
        artwork.contentsGravity = .resizeAspectFill
        artwork.backgroundColor = NSColor.white.withAlphaComponent(0.08).cgColor
        groups[0].addSublayer(artwork)
        artworkPlaceholder.frame = layout.artwork.insetBy(dx: layout.artwork.width * 0.3, dy: layout.artwork.height * 0.3)
        Self.show(.music, on: artworkPlaceholder)
        artworkPlaceholder.opacity = Self.dimmedOpacity
        groups[0].addSublayer(artworkPlaceholder)

        for (layer, frame, group) in [(title, layout.title, 1), (artist, layout.artist, 1), (lyricLine, layout.lyricLine, 1), (elapsed, layout.elapsedTime, 2), (remaining, layout.remainingTime, 2)] {
            layer.frame = frame
            layer.contentsScale = scale
            groups[group].addSublayer(layer)
        }
        elapsed.alignmentMode = .left
        remaining.alignmentMode = .right
        for (index, layer) in panelLines.enumerated() {
            // Text sits at the top of a text layer, so center it in its row by hand.
            let row = layout.panelLine(index)
            let height = (layer.fontSize + 6).rounded()
            layer.frame = CGRect(x: row.minX, y: row.midY - height / 2, width: row.width, height: height)
            layer.contentsScale = scale
            groups[1].addSublayer(layer)
        }

        track.frame = layout.sliderTrack
        track.cornerRadius = layout.sliderTrack.height / 2
        track.backgroundColor = NSColor.white.withAlphaComponent(0.2).cgColor
        groups[2].addSublayer(track)
        fill.anchorPoint = .zero
        fill.position = .zero
        fill.bounds = CGRect(x: 0, y: 0, width: 0, height: layout.sliderTrack.height)
        fill.cornerRadius = layout.sliderTrack.height / 2
        fill.backgroundColor = NSColor.white.cgColor
        track.addSublayer(fill)

        for control in [HomeControl.lyrics, .shuffle, .previous, .playPause, .next, .repeatMode] {
            let icon = CAShapeLayer()
            let frame = layout.frame(of: control)
            let inset = frame.width * (control == .playPause ? 0.14 : 0.2)
            icon.frame = frame.insetBy(dx: inset, dy: inset)
            groups[control == .lyrics ? 0 : 3].addSublayer(icon)
            icons[control] = icon
        }
        setIcon(.previous, for: .previous)
        setIcon(.next, for: .next)
        setIcon(.shuffle, for: .shuffle)
    }

    func apply(_ model: HomeModel) {
        guard model != shownModel else { return }
        let previous = shownModel
        shownModel = model
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        title.string = model.title
        artist.string = model.artist
        elapsed.string = model.elapsedText
        remaining.string = model.remainingText

        let showsPanel = model.showsLyricsPanel
        for layer in [title, artist, lyricLine, elapsed, remaining, track] { layer.isHidden = showsPanel }
        for layer in panelLines { layer.isHidden = !showsPanel }
        if previous?.lyricLine != model.lyricLine {
            Self.addFade(to: lyricLine)
            lyricLine.string = model.lyricLine
        }
        if previous?.panelLines != model.panelLines {
            for (layer, text) in zip(panelLines, model.panelLines) {
                if showsPanel { Self.addFade(to: layer) }
                layer.string = text
            }
        }

        setIcon(model.isPlaying ? .pause : .play, for: .playPause)
        setIcon(model.repeatMode == .one ? .repeatOne : .repeatAll, for: .repeatMode)
        setIcon(showsPanel ? .lyricsFilled : .lyrics, for: .lyrics)
        let transportOpacity = model.hasTrack ? 1 : Self.dimmedOpacity
        icons[.previous]?.opacity = transportOpacity
        icons[.playPause]?.opacity = transportOpacity
        icons[.next]?.opacity = transportOpacity
        icons[.shuffle]?.opacity = model.isShuffled ? 1 : Self.idleOpacity
        icons[.repeatMode]?.opacity = model.repeatMode == .off ? Self.idleOpacity : 1
        icons[.shuffle]?.isHidden = !model.showsShuffleAndRepeat
        icons[.repeatMode]?.isHidden = !model.showsShuffleAndRepeat
        icons[.lyrics]?.isHidden = !model.showsLyricsButton
        CATransaction.commit()
    }

    func setArtwork(_ image: CGImage?, tint: NSColor) {
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        Self.addFade(to: artwork)
        artwork.contents = image
        artworkPlaceholder.isHidden = image != nil
        fill.backgroundColor = tint.cgColor
        CATransaction.commit()
    }

    /// Set once per change in playback, not per second: the render server moves the bar from here.
    /// `speed` is the fraction of the track that passes per second, or 0 when nothing is moving.
    func setProgress(_ fraction: Double, speed: Double) {
        let width = layout.sliderTrack.width
        let start = width * fraction
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        fill.removeAnimation(forKey: "progress")
        if speed > 0, fraction < 1 {
            let animation = CABasicAnimation(keyPath: "bounds.size.width")
            animation.fromValue = start
            animation.toValue = width
            animation.duration = (1 - fraction) / speed
            animation.timingFunction = CAMediaTimingFunction(name: .linear)
            fill.add(animation, forKey: "progress")
            fill.bounds.size.width = width
        } else {
            fill.bounds.size.width = start
        }
        CATransaction.commit()
    }

    func setHover(_ control: HomeControl?, isPressed: Bool) {
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        let plateControls: Set<HomeControl> = [.lyrics, .shuffle, .previous, .playPause, .next, .repeatMode]
        if let control, plateControls.contains(control), icons[control]?.isHidden == false {
            let frame = layout.frame(of: control)
            hoverPlate.frame = frame
            hoverPlate.cornerRadius = frame.width / 2
            hoverPlate.opacity = isPressed ? 1 : 0.55
            hoverPlate.isHidden = false
        } else {
            hoverPlate.isHidden = true
        }
        CATransaction.commit()
    }

    /// Staggers the groups in from `begin`. The pager owns the whole page's fade and slide.
    func reveal(begin: CFTimeInterval) {
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        Motion.reveal(groups, begin: begin)
        CATransaction.commit()
    }

    private func setIcon(_ icon: HomeIcon, for control: HomeControl) {
        if let layer = icons[control] { Self.show(icon, on: layer) }
    }

    /// Set when an icon changes, never animated: moving a path is the expensive end of Core Animation.
    private static func show(_ icon: HomeIcon, on layer: CAShapeLayer) {
        let white = NSColor.white.cgColor
        layer.path = icon.path(side: layer.bounds.width)
        layer.fillRule = icon.hasHole ? .evenOdd : .nonZero
        layer.fillColor = icon.isOutline ? nil : white
        layer.strokeColor = icon.isOutline ? white : nil
        layer.lineWidth = layer.bounds.width / 12
        layer.lineCap = .round
        layer.lineJoin = .round
    }

    private static func addFade(to layer: CALayer) {
        let transition = CATransition()
        transition.type = .fade
        transition.duration = 0.2
        layer.add(transition, forKey: "contentFade")
    }

    private static func makeText(size: CGFloat, weight: NSFont.Weight, opacity: Float) -> CATextLayer {
        let layer = CATextLayer()
        layer.font = NSFont.systemFont(ofSize: size, weight: weight)
        layer.fontSize = size
        layer.foregroundColor = NSColor.white.cgColor
        layer.opacity = opacity
        layer.truncationMode = .end
        layer.alignmentMode = .left
        return layer
    }
}
