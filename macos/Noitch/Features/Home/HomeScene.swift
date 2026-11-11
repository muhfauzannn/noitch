import AppKit
import QuartzCore

/// Home's layers. It draws what it is given and decides nothing, and it never draws per frame: text
/// changes when a value changes, and the progress bar and every transition move in the render server.
final class HomeScene {
    private static let artworkRadius: CGFloat = 14
    private static let dimmedOpacity: Float = 0.35
    private static let idleOpacity: Float = 0.55
    private static let nextLyricOpacity: Float = 0.38
    private static let skippingOpacity: Float = 0.4

    /// Placed by the pager, which owns its fade and slide.
    let root = CALayer()

    private let layout: HomeLayout
    /// A soft wash of the artwork's colour from the left, so the page takes on the track's mood.
    private let backdrop = CAGradientLayer()
    private let artwork = CALayer()
    private let artworkPlaceholder = CAShapeLayer()
    /// Title and artist move together when the track changes.
    private let heading = CALayer()
    private let title = HomeScene.makeText(size: 15, weight: .semibold, opacity: 1)
    private let artist = HomeScene.makeText(size: 13, weight: .regular, opacity: 0.6)
    /// Three layers take turns as the bright line, the dim line and the one about to rise into view.
    private let lyricLayers = (0..<3).map { _ in HomeScene.makeText(size: 14, weight: .semibold, opacity: 1) }
    private var bright = 0
    private var dim = 1
    private var spare = 2
    private let elapsed = HomeScene.makeText(size: 11, weight: .regular, opacity: 0.6)
    private let remaining = HomeScene.makeText(size: 11, weight: .regular, opacity: 0.6)
    private let track = CALayer()
    private let fill = CALayer()
    /// One plate that moves to whichever button is under the pointer.
    private let hoverPlate = CALayer()
    private var icons: [HomeControl: CAShapeLayer] = [:]
    /// Artwork, text, slider, transport: revealed one after another, so the eye reads top-left to
    /// bottom instead of everything popping in at once.
    private let groups = (0..<4).map { _ in CALayer() }
    private var shownModel: HomeModel?
    private var tint = NSColor.clear

    init(layout: HomeLayout, scale: CGFloat) {
        self.layout = layout
        // The pager places it and flips its geometry, so the shared y-down numbers hold.
        root.bounds = CGRect(origin: .zero, size: layout.size)

        backdrop.frame = root.bounds
        backdrop.type = .radial
        let center = layout.glowCenter
        let radii = layout.glowRadii
        backdrop.startPoint = CGPoint(x: center.x / layout.size.width, y: center.y / layout.size.height)
        backdrop.endPoint = CGPoint(x: (center.x + radii.width) / layout.size.width, y: (center.y + radii.height) / layout.size.height)
        backdrop.locations = [0, 0.45, 1]
        backdrop.colors = Self.wash(.clear)
        root.addSublayer(backdrop)
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

        heading.frame = root.bounds
        groups[1].addSublayer(heading)
        for (layer, frame) in [(title, layout.title), (artist, layout.artist)] {
            layer.frame = frame
            layer.contentsScale = scale
            heading.addSublayer(layer)
        }
        for layer in lyricLayers {
            // Anchored at the left edge, so the dim line shrinks toward where its text starts.
            layer.anchorPoint = CGPoint(x: 0, y: 0.5)
            layer.bounds = CGRect(origin: .zero, size: layout.lyric.size)
            layer.contentsScale = scale
            groups[1].addSublayer(layer)
        }
        place(lyricLayers[bright], slot: 0)
        place(lyricLayers[dim], slot: 1)
        place(lyricLayers[spare], slot: 2)

        for (layer, frame) in [(elapsed, layout.elapsedTime), (remaining, layout.remainingTime)] {
            layer.frame = frame
            layer.contentsScale = scale
            groups[2].addSublayer(layer)
        }
        elapsed.alignmentMode = .left
        remaining.alignmentMode = .right
        track.frame = layout.sliderTrack
        track.cornerRadius = layout.sliderTrack.height / 2
        track.backgroundColor = NSColor.white.withAlphaComponent(0.18).cgColor
        groups[2].addSublayer(track)
        fill.anchorPoint = .zero
        fill.position = .zero
        fill.bounds = CGRect(x: 0, y: 0, width: 0, height: layout.sliderTrack.height)
        fill.cornerRadius = layout.sliderTrack.height / 2
        fill.backgroundColor = NSColor.white.cgColor
        track.addSublayer(fill)

        for control in [HomeControl.shuffle, .previous, .playPause, .next, .repeatMode] {
            let icon = CAShapeLayer()
            let frame = layout.frame(of: control)
            let inset = frame.width * (control == .playPause ? 0.12 : 0.2)
            icon.frame = frame.insetBy(dx: inset, dy: inset)
            groups[3].addSublayer(icon)
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
        let now = CACurrentMediaTime()
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        if previous.map({ $0.title != model.title || $0.artist != model.artist }) ?? false, model.hasTrack {
            // A new track rises into place, the way the lyrics do.
            Self.addFade(to: title, duration: 0.3)
            Self.addFade(to: artist, duration: 0.3)
            Motion.ease(heading, "transform.translation.y", from: 8, to: 0, duration: 0.45, begin: now)
        }
        title.string = model.title
        artist.string = model.artist
        let headingOpacity: Float = model.isSkipping ? Self.skippingOpacity : 1
        if heading.opacity != headingOpacity {
            Motion.ease(heading, "opacity", from: heading.presentation()?.opacity ?? heading.opacity, to: headingOpacity, duration: 0.25, begin: now)
            heading.opacity = headingOpacity
            Motion.ease(artwork, "opacity", from: artwork.presentation()?.opacity ?? artwork.opacity, to: headingOpacity, duration: 0.25, begin: now)
            artwork.opacity = headingOpacity
        }
        elapsed.string = model.elapsedText
        remaining.string = model.remainingText
        showLyrics(model, previous: previous, now: now)

        setIcon(model.isPlaying ? .pause : .play, for: .playPause)
        setIcon(model.repeatMode == .one ? .repeatOne : .repeatAll, for: .repeatMode)
        let transportOpacity = model.hasTrack ? 1 : Self.dimmedOpacity
        icons[.previous]?.opacity = transportOpacity
        icons[.playPause]?.opacity = transportOpacity
        icons[.next]?.opacity = transportOpacity
        icons[.shuffle]?.opacity = model.isShuffled ? 1 : Self.idleOpacity
        icons[.repeatMode]?.opacity = model.repeatMode == .off ? Self.idleOpacity : 1
        icons[.shuffle]?.isHidden = !model.showsShuffleAndRepeat
        icons[.repeatMode]?.isHidden = !model.showsShuffleAndRepeat
        CATransaction.commit()
    }

    /// When the song moves on one line, the bright line rises and fades, the dim line grows into its
    /// place, and the following line rises in below. Any other change crossfades. Only position,
    /// scale and opacity move, so no text is redrawn during the motion.
    private func showLyrics(_ model: HomeModel, previous: HomeModel?, now: CFTimeInterval) {
        let current = lyricLayers[bright]
        let below = lyricLayers[dim]
        let currentText = current.string as? String ?? ""
        let belowText = below.string as? String ?? ""
        guard currentText != model.lyric || belowText != model.nextLyric || previous?.lyricIsStatus != model.lyricIsStatus else { return }
        // A step is one line on, with the dim line becoming the bright one, including from the intro.
        let steppedForward = !Motion.reduceMotion && previous != nil && model.lyricIndex != nil
            && (previous?.lyricIndex ?? -1) + 1 == model.lyricIndex && belowText == model.lyric && !belowText.isEmpty
        if steppedForward {
            let incoming = lyricLayers[spare]
            incoming.string = model.nextLyric
            move(current, to: -1, now: now, duration: 0.35)
            move(below, to: 0, now: now, duration: 0.5)
            place(incoming, slot: 2)
            move(incoming, to: 1, now: now + 0.08, duration: 0.5)
            (bright, dim, spare) = (dim, spare, bright)
        } else {
            place(current, slot: 0)
            place(below, slot: 1)
            place(lyricLayers[spare], slot: 2)
            Self.addFade(to: current, duration: 0.25)
            Self.addFade(to: below, duration: 0.25)
            current.string = model.lyric
            below.string = model.nextLyric
        }
        // A status message is quieter than a sung line.
        lyricLayers[bright].opacity = model.lyricIsStatus ? 0.5 : 1
    }

    /// Slot -1 is above the bright line, 0 the bright line, 1 the dim line, 2 below it.
    private func slot(_ index: Int) -> (position: CGPoint, scale: CGFloat, opacity: Float) {
        let y = layout.lyric.midY + CGFloat(index) * HomeLayout.lyricStep
        let position = CGPoint(x: layout.lyric.minX, y: y)
        switch index {
        case 0: return (position, 1, 1)
        case 1: return (position, HomeLayout.nextLyricScale, Self.nextLyricOpacity)
        default: return (position, index < 0 ? 1 : HomeLayout.nextLyricScale, 0)
        }
    }

    private func place(_ layer: CALayer, slot index: Int) {
        let target = slot(index)
        layer.removeAllAnimations()
        layer.position = target.position
        layer.transform = CATransform3DMakeScale(target.scale, target.scale, 1)
        layer.opacity = target.opacity
    }

    private func move(_ layer: CALayer, to index: Int, now: CFTimeInterval, duration: CFTimeInterval) {
        let target = slot(index)
        let shown = layer.presentation() ?? layer
        Motion.ease(layer, "position", from: NSValue(point: shown.position), to: NSValue(point: target.position), duration: duration, begin: now)
        Motion.ease(layer, "transform.scale", from: shown.value(forKeyPath: "transform.scale") ?? 1, to: target.scale, duration: duration, begin: now)
        Motion.ease(layer, "opacity", from: shown.opacity, to: target.opacity, duration: duration, begin: now)
        layer.position = target.position
        layer.transform = CATransform3DMakeScale(target.scale, target.scale, 1)
        layer.opacity = target.opacity
    }

    /// `barTint` colours the progress bar and `glow` the wash behind the page; nil turns them back to
    /// white and none.
    func setArtwork(_ image: CGImage?, barTint: NSColor?, glow: NSColor?) {
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        Self.addFade(to: artwork, duration: 0.3)
        artwork.contents = image
        artworkPlaceholder.isHidden = image != nil
        fill.backgroundColor = (barTint ?? .white).cgColor
        let wash = Self.wash(glow ?? .clear)
        let animation = CABasicAnimation(keyPath: "colors")
        animation.fromValue = backdrop.presentation()?.colors ?? backdrop.colors
        animation.toValue = wash
        animation.duration = 0.6
        animation.timingFunction = Motion.easeOut
        backdrop.colors = wash
        backdrop.add(animation, forKey: "colors")
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
        let plateControls: Set<HomeControl> = [.shuffle, .previous, .playPause, .next, .repeatMode]
        if let control, plateControls.contains(control), icons[control]?.isHidden == false {
            let frame = layout.frame(of: control)
            let appearing = hoverPlate.isHidden
            hoverPlate.frame = frame
            hoverPlate.cornerRadius = frame.width / 2
            hoverPlate.opacity = isPressed ? 1 : 0.55
            hoverPlate.isHidden = false
            if appearing { Self.addFade(to: hoverPlate, duration: 0.15) }
            // A press sinks the icon a little; letting go springs it back.
            if let icon = icons[control] {
                let scale: CGFloat = isPressed ? 0.88 : 1
                Motion.ease(icon, "transform.scale", from: icon.presentation()?.value(forKeyPath: "transform.scale") ?? 1, to: scale, duration: 0.2, begin: CACurrentMediaTime())
                icon.transform = CATransform3DMakeScale(scale, scale, 1)
            }
        } else {
            hoverPlate.isHidden = true
            for icon in icons.values where icon.transform.m11 != 1 { icon.transform = CATransform3DIdentity }
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

    /// Strongest behind the artwork, gone by the ellipse's edge.
    private static func wash(_ tint: NSColor) -> [CGColor] {
        [tint.withAlphaComponent(tint.alphaComponent * 0.55).cgColor, tint.withAlphaComponent(tint.alphaComponent * 0.2).cgColor, NSColor.clear.cgColor]
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

    private static func addFade(to layer: CALayer, duration: CFTimeInterval) {
        let transition = CATransition()
        transition.type = .fade
        transition.duration = duration
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
