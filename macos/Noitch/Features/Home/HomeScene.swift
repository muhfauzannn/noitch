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
    /// The stage's lines by slot: the one before, the one being sung, the two after.
    private static let stageOpacity: [Int: Float] = [-1: 0.3, 0: 1, 1: 0.55, 2: 0.26]
    private static let stageHoverOpacity: Float = 0.85
    /// Home shrinks back a little as the stage takes over, so the lyrics seem to come forward.
    private static let recededScale: CGFloat = 0.96

    /// Placed by the pager, which owns its fade and slide.
    let root = CALayer()

    private let layout: HomeLayout
    /// A soft wash of the artwork's colour from the left, so the page takes on the track's mood.
    private let backdrop = CAGradientLayer()
    /// The stage's background: one soft wash of the artwork's colour rising from the bottom edge and
    /// gone before the top, where the pill meets the flares. Shown only on the stage.
    private let stageWash = CAGradientLayer()
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
    private var groups = (0..<4).map { _ in CALayer() }
    /// Everything but the lyrics button and the stage, so the two modes cross over as wholes.
    private let music = CALayer()
    /// Home's own two lyric lines, apart from the title so they can rise in from below.
    private let lyricGroup = CALayer()
    private let buttonGroup = CALayer()
    private let lyricsButton = CALayer()
    /// A white disc behind the icon while the stage shows, the way a pressed-in switch looks.
    private let lyricsDisc = CALayer()
    private let lyricsIcon = CAShapeLayer()
    /// The stage sits in a group of its own: the reveal animates the group, the mode the stage.
    private let stageGroup = CALayer()
    private let stage = CALayer()
    private let romanization = HomeScene.makeText(size: 12, weight: .medium, opacity: 0.7)
    /// Five lines take turns: four in slots -1 to 2, and one about to rise in from below.
    private let stageLayers = (0..<5).map { _ in HomeScene.makeText(size: 20, weight: .bold, opacity: 0) }
    private var stageOrder = [0, 1, 2, 3]
    private var stageSpare = 4
    private var stageHover: Int?
    private var stageIsStatus = false
    private(set) var isOnStage = false
    private var shownModel: HomeModel?
    private var tint = NSColor.clear

    init(layout: HomeLayout, scale: CGFloat) {
        self.layout = layout
        // The pager places it and flips its geometry, so the shared y-down numbers hold.
        root.bounds = CGRect(origin: .zero, size: layout.size)

        stageWash.frame = root.bounds
        stageWash.opacity = 0
        stageWash.locations = [0, 0.4, 1]
        stageWash.colors = Self.stageWash(.clear)
        root.addSublayer(stageWash)

        backdrop.frame = root.bounds
        backdrop.type = .radial
        // Set once the stored properties are in place: `glowPoints` needs `self`.
        backdrop.locations = [0, 0.45, 1]
        backdrop.colors = Self.wash(.clear)
        root.addSublayer(backdrop)
        hoverPlate.backgroundColor = NSColor.white.withAlphaComponent(0.22).cgColor
        hoverPlate.isHidden = true
        root.addSublayer(hoverPlate)
        music.frame = root.bounds
        root.addSublayer(music)
        for group in groups {
            group.frame = root.bounds
            music.addSublayer(group)
        }
        lyricGroup.frame = root.bounds
        music.addSublayer(lyricGroup)
        for layer in [buttonGroup, stageGroup] {
            layer.frame = root.bounds
            root.addSublayer(layer)
        }
        // Anchored at the top edge, where the page grows from, so `reveal` can hold them at their own
        // size while it grows. Layouts here are y-down.
        for layer in [lyricGroup, stageGroup] {
            layer.anchorPoint = CGPoint(x: 0.5, y: 0)
            layer.position = CGPoint(x: root.bounds.midX, y: 0)
        }

        lyricsButton.frame = layout.lyricsButton(onStage: false)
        buttonGroup.addSublayer(lyricsButton)
        lyricsDisc.frame = lyricsButton.bounds
        lyricsDisc.cornerRadius = lyricsButton.bounds.width / 2
        lyricsDisc.backgroundColor = NSColor.white.cgColor
        lyricsDisc.opacity = 0
        lyricsButton.addSublayer(lyricsDisc)
        lyricsIcon.frame = lyricsButton.bounds.insetBy(dx: 4, dy: 4)
        Self.show(.lyrics, on: lyricsIcon)
        lyricsIcon.opacity = 0.75
        lyricsButton.addSublayer(lyricsIcon)

        stage.frame = root.bounds
        stage.opacity = 0
        stageGroup.addSublayer(stage)
        romanization.alignmentMode = .center
        romanization.contentsScale = scale
        romanization.frame = CGRect(x: layout.stageLine(0).minX, y: layout.stageLine(0).maxY - 6, width: layout.stageLine(0).width, height: 18)
        stage.addSublayer(romanization)
        let line = layout.stageLine(0)
        for layer in stageLayers {
            layer.bounds = CGRect(x: 0, y: 0, width: line.width, height: HomeLayout.stageLineHeight)
            layer.alignmentMode = .center
            layer.contentsScale = scale
            stage.addSublayer(layer)
        }
        for (slot, index) in zip(HomeLayout.stageSlots, stageOrder) { placeOnStage(stageLayers[index], slot: slot) }
        placeOnStage(stageLayers[stageSpare], slot: 3)

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
            lyricGroup.addSublayer(layer)
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
        let glow = glowPoints()
        backdrop.startPoint = glow.start
        backdrop.endPoint = glow.end
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
        buttonGroup.isHidden = !model.showsLyricsButton
        showStage(model, previous: previous, now: now)
        if model.isOnStage != isOnStage { setOnStage(model.isOnStage, animated: previous != nil, now: now) }

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
            Motion.riseIn(current, begin: now)
            Motion.riseIn(below, begin: now + 0.04)
            current.string = model.lyric
            below.string = model.nextLyric
        }
        // A status message is quieter than a sung line.
        lyricLayers[bright].opacity = model.lyricIsStatus ? 0.5 : 1
    }

    // MARK: Stage

    /// When the song moves on one line, every line rises a slot: the one being sung grows into the
    /// middle, the one before drifts up and away, and the next rises in from below. Only position,
    /// scale and opacity move. Anything else, such as a seek, crossfades.
    private func showStage(_ model: HomeModel, previous: HomeModel?, now: CFTimeInterval) {
        let readingChanged = (romanization.string as? String ?? "") != model.stageRomanization
        romanization.string = model.stageRomanization
        let shown = stageOrder.map { stageLayers[$0].string as? String ?? "" }
        guard shown != model.stageLines || stageIsStatus != model.stageIsStatus || readingChanged else { return }
        stageIsStatus = model.stageIsStatus
        let steppedForward = isOnStage && !Motion.reduceMotion && previous != nil && !model.stageIsStatus
            && model.lyricIndex != nil && (previous?.lyricIndex ?? -1) + 1 == model.lyricIndex && shown[2] == model.stageLines[1]
        if steppedForward {
            let order = stageOrder
            let incoming = stageLayers[stageSpare]
            incoming.string = model.stageLines[3]
            placeOnStage(incoming, slot: 3)
            moveOnStage(stageLayers[order[0]], to: -2, begin: now, duration: 0.4)
            moveOnStage(stageLayers[order[1]], to: -1, begin: now, duration: 0.5)
            moveOnStage(stageLayers[order[2]], to: 0, begin: now, duration: 0.55)
            moveOnStage(stageLayers[order[3]], to: 1, begin: now + 0.04, duration: 0.55)
            moveOnStage(incoming, to: 2, begin: now + 0.1, duration: 0.55)
            stageOrder = [order[1], order[2], order[3], stageSpare]
            stageSpare = order[0]
        } else {
            for (offset, index) in stageOrder.enumerated() {
                let layer = stageLayers[index]
                placeOnStage(layer, slot: offset - 1)
                if isOnStage { Motion.riseIn(layer, from: 8, duration: 0.45, begin: now + Double(offset) * 0.04) }
                layer.string = model.stageLines[offset]
            }
            placeOnStage(stageLayers[stageSpare], slot: 3)
        }
    }

    private func stageTarget(_ slot: Int) -> (position: CGPoint, scale: CGFloat, opacity: Float) {
        let line = layout.stageLine(slot)
        let position = CGPoint(x: line.midX, y: line.midY - (slot == 0 && !(romanization.string as? String ?? "").isEmpty ? 7 : 0))
        if slot == 0 { return stageIsStatus ? (position, 0.8, 0.55) : (position, 1, 1) }
        var opacity = Self.stageOpacity[slot] ?? 0
        if slot == stageHover, opacity > 0 { opacity = Self.stageHoverOpacity }
        return (position, HomeLayout.stageScale, opacity)
    }

    private func placeOnStage(_ layer: CALayer, slot: Int) {
        let target = stageTarget(slot)
        layer.removeAllAnimations()
        layer.position = target.position
        layer.transform = CATransform3DMakeScale(target.scale, target.scale, 1)
        layer.opacity = target.opacity
    }

    private func moveOnStage(_ layer: CALayer, to slot: Int, begin: CFTimeInterval, duration: CFTimeInterval) {
        let target = stageTarget(slot)
        let shown = layer.presentation() ?? layer
        Motion.ease(layer, "position", from: NSValue(point: shown.position), to: NSValue(point: target.position), duration: duration, begin: begin)
        Motion.ease(layer, "transform.scale", from: shown.value(forKeyPath: "transform.scale") ?? 1, to: target.scale, duration: duration, begin: begin)
        Motion.ease(layer, "opacity", from: shown.opacity, to: target.opacity, duration: duration, begin: begin)
        layer.position = target.position
        layer.transform = CATransform3DMakeScale(target.scale, target.scale, 1)
        layer.opacity = target.opacity
    }

    /// Brightens a line the pointer is over, a hint that clicking it jumps there.
    func setStageHover(_ slot: Int?) {
        guard slot != stageHover else { return }
        let changed = [stageHover, slot].compactMap { $0 }
        stageHover = slot
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        let now = CACurrentMediaTime()
        for slot in changed where slot != 0 {
            let layer = stageLayers[stageOrder[slot + 1]]
            let opacity = stageTarget(slot).opacity
            Motion.ease(layer, "opacity", from: layer.presentation()?.opacity ?? layer.opacity, to: opacity, duration: 0.18, begin: now)
            layer.opacity = opacity
        }
        CATransaction.commit()
    }

    /// Home recedes and fades as the stage rises in, its glow gives way to the stage's wash, and
    /// the lyrics button glides to the corner and lights up. Each starts from what is on screen, so a
    /// quick second click reverses smoothly.
    private func setOnStage(_ onStage: Bool, animated: Bool, now: CFTimeInterval) {
        isOnStage = onStage
        stageHover = nil
        let musicScale: CGFloat = onStage ? Self.recededScale : 1
        let button = layout.lyricsButton(onStage: onStage)
        let buttonPosition = CGPoint(x: button.midX, y: button.midY)
        let iconColor = (onStage ? NSColor.black : NSColor.white).cgColor
        if animated && !Motion.reduceMotion {
            let shownMusic = music.presentation() ?? music
            let shownStage = stage.presentation() ?? stage
            let shownButton = lyricsButton.presentation() ?? lyricsButton
            let shownDisc = lyricsDisc.presentation() ?? lyricsDisc
            Motion.ease(music, "opacity", from: shownMusic.opacity, to: onStage ? 0 : 1, duration: onStage ? 0.25 : 0.35, begin: onStage ? now : now + 0.08)
            Motion.ease(music, "transform.scale", from: shownMusic.value(forKeyPath: "transform.scale") ?? 1, to: musicScale, duration: 0.5, begin: now)
            Motion.ease(stage, "opacity", from: shownStage.opacity, to: onStage ? 1 : 0, duration: onStage ? 0.35 : 0.2, begin: onStage ? now + 0.08 : now)
            if onStage { Motion.ease(stage, "transform.translation.y", from: 12, to: 0, duration: 0.55, begin: now + 0.08) }
            Motion.ease(lyricsButton, "position", from: NSValue(point: shownButton.position), to: NSValue(point: buttonPosition), duration: 0.5, begin: now)
            Motion.ease(lyricsDisc, "opacity", from: shownDisc.opacity, to: onStage ? 1 : 0, duration: 0.25, begin: now)
            let shownColor: CGColor = lyricsIcon.presentation()?.fillColor ?? lyricsIcon.fillColor ?? iconColor
            Motion.ease(lyricsIcon, "fillColor", from: shownColor, to: iconColor, duration: 0.25, begin: now)
            Motion.ease(backdrop, "opacity", from: backdrop.presentation()?.opacity ?? backdrop.opacity, to: onStage ? 0 : 1, duration: 0.5, begin: now)
            Motion.ease(stageWash, "opacity", from: stageWash.presentation()?.opacity ?? stageWash.opacity, to: onStage ? 1 : 0, duration: 0.6, begin: now)
        }
        music.opacity = onStage ? 0 : 1
        music.transform = CATransform3DMakeScale(musicScale, musicScale, 1)
        stage.opacity = onStage ? 1 : 0
        lyricsButton.position = buttonPosition
        lyricsDisc.opacity = onStage ? 1 : 0
        lyricsIcon.fillColor = iconColor
        lyricsIcon.opacity = onStage ? 1 : 0.75
        backdrop.opacity = onStage ? 0 : 1
        stageWash.opacity = onStage ? 1 : 0
        // Lines left lit by a hover go back to their own opacity.
        for (slot, index) in zip(HomeLayout.stageSlots, stageOrder) where slot != 0 {
            stageLayers[index].opacity = stageTarget(slot).opacity
        }
    }

    /// The gradient's centre and the point at its edge, as fractions of the page.
    private func glowPoints() -> (start: CGPoint, end: CGPoint) {
        let center = layout.glowCenter
        let radii = layout.glowRadii
        let size = layout.size
        return (CGPoint(x: center.x / size.width, y: center.y / size.height),
                CGPoint(x: (center.x + radii.width) / size.width, y: (center.y + radii.height) / size.height))
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

    /// `barTint` colours the progress bar, and `glow` the wash behind the page and the stage's; nil
    /// turns them back to white and none.
    func setArtwork(_ image: CGImage?, barTint: NSColor?, glow: NSColor?) {
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        let stageColors = Self.stageWash(glow ?? .clear)
        Motion.ease(stageWash, "colors", from: stageWash.presentation()?.colors ?? stageWash.colors as Any, to: stageColors, duration: 0.6, begin: CACurrentMediaTime())
        stageWash.colors = stageColors
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
        let plateControls: Set<HomeControl> = [.shuffle, .previous, .playPause, .next, .repeatMode, .lyrics]
        let pressable: CALayer? = control == .lyrics ? (buttonGroup.isHidden ? nil : lyricsButton) : control.flatMap { icons[$0] }
        if let control, plateControls.contains(control), let pressable, !pressable.isHidden {
            // The lyrics button's plate is a halo a little wider than its disc.
            let frame = control == .lyrics ? layout.lyricsButton(onStage: isOnStage).insetBy(dx: -3, dy: -3) : layout.frame(of: control)
            let appearing = hoverPlate.isHidden
            hoverPlate.frame = frame
            hoverPlate.cornerRadius = frame.width / 2
            hoverPlate.opacity = isPressed ? 1 : 0.55
            hoverPlate.isHidden = false
            if appearing { Self.addFade(to: hoverPlate, duration: 0.15) }
            // A press sinks the icon a little; letting go springs it back.
            let scale: CGFloat = isPressed ? 0.88 : 1
            Motion.ease(pressable, "transform.scale", from: pressable.presentation()?.value(forKeyPath: "transform.scale") ?? 1, to: scale, duration: 0.2, begin: CACurrentMediaTime())
            pressable.transform = CATransform3DMakeScale(scale, scale, 1)
        } else {
            hoverPlate.isHidden = true
            for icon in Array(icons.values) + [lyricsButton] where icon.transform.m11 != 1 { icon.transform = CATransform3DIdentity }
        }
        CATransaction.commit()
    }

    /// Adds a panel, such as the calendar, that reveals after Home's own groups.
    func addGroup(_ layer: CALayer) {
        music.addSublayer(layer)
        groups.append(layer)
    }

    /// Staggers the groups in from `begin`. The pager owns the whole page's fade and slide. On the
    /// stage only the lyrics and the button show, so they come in first.
    func reveal(begin: CFTimeInterval) {
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        let order = isOnStage ? [stageGroup, buttonGroup] : [groups[0], buttonGroup, groups[1], lyricGroup] + groups.dropFirst(2) + [stageGroup]
        Motion.reveal(order, begin: begin, rising: [lyricGroup, stageGroup])
        // The page grows from its top edge with a bouncy spring, which would carry the lyrics down
        // a few points, and back, while they rise: down first, then up. Scaled the other way on the
        // same spring, they only rise.
        let spring = IslandGeometry.expandSpring
        for layer in [lyricGroup, stageGroup] {
            let hold = CASpringAnimation(perceptualDuration: spring.response, bounce: spring.bounce)
            hold.keyPath = "transform.scale"
            hold.fromValue = 1 / PagerScene.hiddenScale
            hold.toValue = 1
            hold.duration = hold.settlingDuration
            layer.add(hold, forKey: "holdScale")
        }
        CATransaction.commit()
    }

    private func setIcon(_ icon: HomeIcon, for control: HomeControl) {
        if let layer = icons[control] { Self.show(icon, on: layer) }
    }

    /// Gone at the top edge, faint through the lines, strongest along the bottom.
    private static func stageWash(_ tint: NSColor) -> [CGColor] {
        [0, 0.1, 0.32].map { tint.withAlphaComponent(tint.alphaComponent * $0).cgColor }
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
