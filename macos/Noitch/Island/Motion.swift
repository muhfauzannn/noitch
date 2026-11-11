import AppKit
import QuartzCore

/// The few curves the island's content uses, so every page moves alike. Each call hands one
/// animation to the render server; nothing here runs per frame.
enum Motion {
    /// A strong ease-out: a fast start so nothing lags the pill, and a long soft landing.
    static let easeOut = CAMediaTimingFunction(controlPoints: 0.22, 1, 0.36, 1)

    /// When the first group appears after opening: the pill needs a head start, or content would
    /// land outside its edge.
    static let firstRevealDelay: CFTimeInterval = 0.06
    static let revealStagger: CFTimeInterval = 0.045
    /// Negative is up: content layouts are y-down. Groups drop gently out of the notch.
    static let revealLift: CGFloat = -8

    /// The model value must already be `to`; `.backwards` holds `from` until `begin`.
    static func ease(_ layer: CALayer, _ keyPath: String, from: Any, to: Any, duration: CFTimeInterval, begin: CFTimeInterval) {
        let animation = CABasicAnimation(keyPath: keyPath)
        animation.fromValue = from
        animation.toValue = to
        animation.duration = duration
        animation.beginTime = begin
        animation.fillMode = .backwards
        animation.timingFunction = easeOut
        layer.add(animation, forKey: keyPath)
    }

    /// Fades each group in and drops it into place, one after another from `begin`. Groups in
    /// `rising` come up from below instead: lyrics always enter from below, as each new line does.
    static func reveal(_ groups: [CALayer], begin: CFTimeInterval, rising: [CALayer] = []) {
        let lift = NSWorkspace.shared.accessibilityDisplayShouldReduceMotion ? 0 : revealLift
        for (index, group) in groups.enumerated() {
            let start = begin + Double(index) * revealStagger
            let from = rising.contains { $0 === group } ? -lift : lift
            ease(group, "opacity", from: 0, to: 1, duration: 0.3, begin: start)
            ease(group, "transform.translation.y", from: from, to: 0, duration: 0.42, begin: start)
        }
    }

    /// Fades a layer's new text in as it rises the last few points into place.
    static func riseIn(_ layer: CALayer, from offset: CGFloat = 6, duration: CFTimeInterval = 0.35, begin: CFTimeInterval) {
        let fade = CATransition()
        fade.type = .fade
        fade.duration = duration * 0.8
        layer.add(fade, forKey: "contentFade")
        guard !reduceMotion else { return }
        ease(layer, "transform.translation.y", from: offset, to: 0, duration: duration, begin: begin)
    }

    static var reduceMotion: Bool { NSWorkspace.shared.accessibilityDisplayShouldReduceMotion }
}
