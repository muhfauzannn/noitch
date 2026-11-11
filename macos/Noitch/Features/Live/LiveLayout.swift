import CoreGraphics

/// The closed island while something plays: the pill widens into a wing on each side of the notch,
/// the artwork in the left one and a flowing wave in the right. From the pill's top-left corner,
/// y growing downward. The Windows app uses the same numbers (live_layout.h).
struct LiveLayout: Equatable {
    static let outerPadding: CGFloat = 9
    static let innerGap: CGFloat = 8
    /// A gentle wave, two lines of it: a bright short one drifting left and a faint long one drifting
    /// right at another speed. Where they cross, the shape keeps changing without ever repeating
    /// visibly, and each line is only slid sideways, so the render server does all the work.
    struct Wave: Equatable {
        /// Points per crest-to-crest.
        var length: CGFloat
        /// Share of the half-height the line swings through.
        var amplitude: CGFloat
        /// Seconds to drift one length.
        var period: Double
        var opacity: Float
        /// −1 drifts left, 1 right.
        var direction: CGFloat
    }

    static let waves = [
        Wave(length: 13, amplitude: 0.75, period: 1.7, opacity: 1, direction: -1),
        Wave(length: 21, amplitude: 0.5, period: 2.9, opacity: 0.45, direction: 1),
    ]
    static let lineWidth: CGFloat = 1.6
    /// Black fades this wide at each end of the wave, so the lines dissolve into the pill.
    static let edgeFade: CGFloat = 4

    /// The pill without wings: the notch, or the virtual one.
    let collapsed: CGSize

    var artSide: CGFloat { max(collapsed.height - 14, 14) }
    var wing: CGFloat { artSide + Self.outerPadding + Self.innerGap }
    var size: CGSize { CGSize(width: collapsed.width + wing * 2, height: collapsed.height) }

    var artwork: CGRect {
        CGRect(x: Self.outerPadding, y: (collapsed.height - artSide) / 2, width: artSide, height: artSide)
    }

    /// The right wing's free space, as tall as most of the artwork.
    var wave: CGRect {
        let start = wing + collapsed.width + Self.innerGap
        let width = wing - Self.innerGap - Self.outerPadding
        let height = (artSide * 0.7).rounded()
        return CGRect(x: start, y: (collapsed.height - height) / 2, width: width, height: height)
    }

    /// Height of `wave`'s path at `x` (from its left edge), for one wave. Sampled into a path once.
    func waveY(_ wave: Wave, at x: CGFloat) -> CGFloat {
        let swing = (self.wave.height - Self.lineWidth) / 2 * wave.amplitude
        return self.wave.height / 2 + swing * sin(x / wave.length * 2 * .pi)
    }
}

/// When the closed island shows what is playing. Pure, so the rules are tested.
enum LiveRule {
    /// A pause keeps the wings this long, so a quick pause and resume doesn't flicker the pill.
    static let pauseGrace: Double = 10

    /// `pausedSince` is when playback last stopped, or nil while playing.
    static func shows(_ nowPlaying: NowPlaying, enabled: Bool, pausedSince: Double?, now: Double) -> Bool {
        guard enabled, nowPlaying.hasTrack else { return false }
        if nowPlaying.isPlaying { return true }
        guard let pausedSince else { return false }
        return now - pausedSince < pauseGrace
    }
}
