import AppKit
import QuartzCore

/// The closed island's music layers: artwork in the left wing, six thin level bars in the right. It decides
/// nothing. The bars are looping Core Animations, so while they move the render server does every
/// frame and this process does nothing; they are a rhythm, not a reading of the audio.
final class LiveScene {
    /// Hung from the top center of the pill by the view, below any page.
    let root = CALayer()

    private let layout: LiveLayout
    private let artwork = CALayer()
    private let placeholder = CAShapeLayer()
    private let bars: [CALayer]
    private var isPlaying = false
    /// Two layers take turns, so a new line rises in from below while the old one rises away.
    private let lyricLayers: [CATextLayer]
    private var lyricIndex = 0
    private var shownLyric: String?

    init(layout: LiveLayout, scale: CGFloat) {
        self.layout = layout
        // Tall enough for the lyric strip; the pill's clip shows it only while the pill has grown.
        root.bounds = CGRect(origin: .zero, size: layout.size(withLyric: true))
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

        bars = (0..<LiveLayout.barCount).map { index in
            let bar = CALayer()
            bar.frame = layout.bar(index)
            bar.cornerRadius = LiveLayout.barWidth / 2
            bar.backgroundColor = NSColor.white.cgColor
            bar.transform = CATransform3DMakeScale(1, LiveLayout.restingScale, 1)
            return bar
        }
        bars.forEach(root.addSublayer)

        lyricLayers = (0..<2).map { _ in
            let text = CATextLayer()
            text.frame = layout.lyric
            text.font = NSFont.systemFont(ofSize: LiveLayout.lyricFontSize, weight: .semibold)
            text.fontSize = LiveLayout.lyricFontSize
            text.foregroundColor = NSColor.white.cgColor
            text.alignmentMode = .center
            text.truncationMode = .end
            text.contentsScale = scale
            text.opacity = 0
            return text
        }
        lyricLayers.forEach(root.addSublayer)
    }

    /// The line being sung, or nil for none. A new line rises in from below as the old one rises
    /// away; both move only position and opacity.
    func setLyric(_ text: String?, animated: Bool) {
        guard text != shownLyric else { return }
        let hadLine = shownLyric != nil
        shownLyric = text
        let now = CACurrentMediaTime()
        let move = animated && !Motion.reduceMotion
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        let old = lyricLayers[lyricIndex]
        if let text {
            lyricIndex = 1 - lyricIndex
            let new = lyricLayers[lyricIndex]
            new.removeAllAnimations()
            new.string = text
            new.opacity = 1
            if move {
                // A first line waits for the pill to grow down to it.
                let begin = now + (hadLine ? 0.06 : 0.18)
                Motion.ease(new, "opacity", from: 0, to: 1, duration: 0.3, begin: begin)
                Motion.ease(new, "transform.translation.y", from: 7, to: 0, duration: 0.4, begin: begin)
            }
        }
        if hadLine {
            if move {
                Motion.ease(old, "opacity", from: old.presentation()?.opacity ?? old.opacity, to: 0, duration: 0.2, begin: now)
                Motion.ease(old, "transform.translation.y", from: 0, to: -6, duration: 0.3, begin: now)
            }
            old.opacity = 0
        }
        CATransaction.commit()
    }

    /// `tint` colours the bars, the outer ones fully and the middle ones mixed toward white; nil
    /// leaves them all white.
    func setArtwork(_ image: CGImage?, tint: NSColor?) {
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        let fade = CATransition()
        fade.type = .fade
        fade.duration = 0.35
        artwork.add(fade, forKey: "contents")
        artwork.contents = image
        placeholder.isHidden = image != nil
        let base = tint ?? .white
        for (index, bar) in bars.enumerated() {
            let whiteness = LiveLayout.barWhiteness[index % LiveLayout.barWhiteness.count]
            let color = (base.blended(withFraction: whiteness, of: .white) ?? base).cgColor
            Motion.ease(bar, "backgroundColor", from: (bar.presentation() ?? bar).backgroundColor as Any, to: color, duration: 0.5, begin: CACurrentMediaTime())
            bar.backgroundColor = color
        }
        CATransaction.commit()
    }

    /// Playing, each bar rises and falls on its own period, forever, in the render server. Paused,
    /// they settle low and nothing runs.
    func setPlaying(_ playing: Bool) {
        guard playing != isPlaying else { return }
        isPlaying = playing
        let now = CACurrentMediaTime()
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        for (index, bar) in bars.enumerated() {
            let current = (bar.presentation()?.value(forKeyPath: "transform.scale.y") as? CGFloat) ?? LiveLayout.restingScale
            bar.removeAnimation(forKey: "level")
            if playing && !Motion.reduceMotion {
                let period = LiveLayout.barPeriods[index % LiveLayout.barPeriods.count]
                let level = CABasicAnimation(keyPath: "transform.scale.y")
                level.fromValue = LiveLayout.restingScale
                level.toValue = 1
                level.duration = period
                level.autoreverses = true
                level.repeatCount = .infinity
                level.timingFunction = CAMediaTimingFunction(name: .easeInEaseOut)
                // Starts each bar part-way through its cycle, so they don't rise together.
                level.beginTime = now - period * Double(index) * 0.37
                bar.add(level, forKey: "level")
            } else {
                Motion.ease(bar, "transform.scale.y", from: current, to: LiveLayout.restingScale, duration: 0.3, begin: now)
            }
            bar.transform = CATransform3DMakeScale(1, LiveLayout.restingScale, 1)
        }
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
