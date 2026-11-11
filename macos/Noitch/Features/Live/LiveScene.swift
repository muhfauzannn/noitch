import AppKit
import QuartzCore

/// The closed island's music layers: artwork in the left wing, a flowing wave in the right. It
/// decides nothing. Each wave line is a fixed path slid sideways by a looping Core Animation, so
/// while it moves the render server does every frame and this process does nothing. It shows that
/// something plays; it is not a reading of the audio.
final class LiveScene {
    /// Hung from the top center of the pill by the view, below any page.
    let root = CALayer()

    private let layout: LiveLayout
    private let artwork = CALayer()
    private let placeholder = CAShapeLayer()
    private let waveBox = CALayer()
    private let lines: [CAShapeLayer]
    /// What the wave settles into when paused.
    private let flat = CALayer()
    private var isPlaying = false

    init(layout: LiveLayout, scale: CGFloat) {
        self.layout = layout
        root.bounds = CGRect(origin: .zero, size: layout.size)
        root.isGeometryFlipped = true
        root.opacity = 0

        artwork.frame = layout.artwork
        artwork.cornerRadius = 5
        artwork.cornerCurve = .continuous
        artwork.masksToBounds = true
        artwork.contentsGravity = .resizeAspectFill
        artwork.backgroundColor = NSColor.white.withAlphaComponent(0.12).cgColor
        artwork.contentsScale = scale
        root.addSublayer(artwork)
        let inset = layout.artSide * 0.24
        placeholder.frame = artwork.bounds.insetBy(dx: inset, dy: inset)
        placeholder.path = HomeIcon.music.path(side: placeholder.bounds.width)
        placeholder.fillColor = NSColor.white.withAlphaComponent(0.5).cgColor
        placeholder.contentsScale = scale
        artwork.addSublayer(placeholder)

        let box = layout.wave
        waveBox.frame = box
        waveBox.masksToBounds = true
        root.addSublayer(waveBox)
        lines = LiveLayout.waves.map { wave in
            // One extra length each side, so sliding by a length never shows an end.
            let path = CGMutablePath()
            let span = box.width + wave.length * 2
            let step: CGFloat = 0.5
            path.move(to: CGPoint(x: 0, y: layout.waveY(wave, at: -wave.length)))
            for x in stride(from: step, through: span, by: step) {
                path.addLine(to: CGPoint(x: x, y: layout.waveY(wave, at: x - wave.length)))
            }
            let line = CAShapeLayer()
            line.frame = CGRect(x: -wave.length, y: 0, width: span, height: box.height)
            line.path = path
            line.fillColor = nil
            line.strokeColor = NSColor.white.cgColor
            line.lineWidth = LiveLayout.lineWidth
            line.lineCap = .round
            line.lineJoin = .round
            line.opacity = 0
            line.contentsScale = scale
            return line
        }
        lines.forEach(waveBox.addSublayer)
        flat.frame = CGRect(x: 0, y: (box.height - LiveLayout.lineWidth) / 2, width: box.width, height: LiveLayout.lineWidth)
        flat.cornerRadius = LiveLayout.lineWidth / 2
        flat.backgroundColor = NSColor.white.cgColor
        flat.opacity = Self.flatOpacity
        waveBox.addSublayer(flat)
        // Black fading to clear over each end: the pill is black, so the lines dissolve into it.
        for leading in [true, false] {
            let fade = CAGradientLayer()
            fade.frame = CGRect(x: leading ? 0 : box.width - LiveLayout.edgeFade, y: 0, width: LiveLayout.edgeFade, height: box.height)
            fade.startPoint = CGPoint(x: leading ? 0 : 1, y: 0.5)
            fade.endPoint = CGPoint(x: leading ? 1 : 0, y: 0.5)
            fade.colors = [NSColor.black.cgColor, NSColor.black.withAlphaComponent(0).cgColor]
            waveBox.addSublayer(fade)
        }
    }

    private static let flatOpacity: Float = 0.5

    /// `tint` colours the wave; nil leaves it white.
    func setArtwork(_ image: CGImage?, tint: NSColor?) {
        let now = CACurrentMediaTime()
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        let fade = CATransition()
        fade.type = .fade
        fade.duration = 0.35
        artwork.add(fade, forKey: "contents")
        artwork.contents = image
        placeholder.isHidden = image != nil
        let color = (tint ?? .white).cgColor
        for line in lines {
            Motion.ease(line, "strokeColor", from: (line.presentation() ?? line).strokeColor as Any, to: color, duration: 0.5, begin: now)
            line.strokeColor = color
        }
        Motion.ease(flat, "backgroundColor", from: (flat.presentation() ?? flat).backgroundColor as Any, to: color, duration: 0.5, begin: now)
        flat.backgroundColor = color
        CATransaction.commit()
    }

    /// Playing, the lines drift forever in the render server. Paused, they fade into one still
    /// line and nothing runs.
    func setPlaying(_ playing: Bool) {
        guard playing != isPlaying else { return }
        isPlaying = playing
        let now = CACurrentMediaTime()
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        for (line, wave) in zip(lines, LiveLayout.waves) {
            let to: Float = playing ? wave.opacity : 0
            Motion.ease(line, "opacity", from: (line.presentation() ?? line).opacity, to: to, duration: 0.35, begin: now)
            line.opacity = to
            if playing && !Motion.reduceMotion {
                let drift = CABasicAnimation(keyPath: "transform.translation.x")
                drift.fromValue = 0
                drift.toValue = wave.direction * wave.length
                drift.duration = wave.period
                drift.repeatCount = .infinity
                drift.timingFunction = CAMediaTimingFunction(name: .linear)
                line.add(drift, forKey: "drift")
            } else if !playing {
                // Stopped where it is, so the fade doesn't jump, and nothing runs while paused.
                let offset = ((line.presentation() ?? line).value(forKeyPath: "transform.translation.x") as? CGFloat) ?? 0
                line.transform = CATransform3DMakeTranslation(offset, 0, 0)
                line.removeAnimation(forKey: "drift")
            }
        }
        let flatTo: Float = playing ? 0 : Self.flatOpacity
        Motion.ease(flat, "opacity", from: (flat.presentation() ?? flat).opacity, to: flatTo, duration: 0.35, begin: now)
        flat.opacity = flatTo
        CATransaction.commit()
    }

    /// Fades in after `delay`, once the pill has room, or out at once.
    func show(_ visible: Bool, delay: CFTimeInterval = 0) {
        let now = CACurrentMediaTime()
        let from = root.presentation()?.opacity ?? root.opacity
        let to: Float = visible ? 1 : 0
        guard from != to || root.opacity != to else { return }
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        Motion.ease(root, "opacity", from: from, to: to, duration: visible ? 0.3 : 0.12, begin: now + (visible ? delay : 0))
        root.opacity = to
        CATransaction.commit()
    }
}
