import CoreGraphics

/// The closed island while something plays: the pill widens into a wing on each side of the notch,
/// the artwork in the left one and six thin level bars in the right. From the pill's top-left corner,
/// y growing downward. The Windows app uses the same numbers (live_layout.h).
struct LiveLayout: Equatable {
    static let outerPadding: CGFloat = 9
    static let innerGap: CGFloat = 8
    /// Six thin bars, our own look: thinner and more of them than the usual four.
    static let barCount = 6
    static let barWidth: CGFloat = 1.5
    static let barGap: CGFloat = 1.5
    /// Each bar's resting height as a share of the tallest it reaches.
    static let restingScale: CGFloat = 0.3

    /// The pill without wings: the notch, or the virtual one.
    let collapsed: CGSize

    var artSide: CGFloat { max(collapsed.height - 14, 14) }
    var wing: CGFloat { artSide + Self.outerPadding + Self.innerGap }
    var size: CGSize { CGSize(width: collapsed.width + wing * 2, height: collapsed.height) }

    var artwork: CGRect {
        CGRect(x: Self.outerPadding, y: (collapsed.height - artSide) / 2, width: artSide, height: artSide)
    }

    var barsWidth: CGFloat { CGFloat(Self.barCount) * Self.barWidth + CGFloat(Self.barCount - 1) * Self.barGap }

    /// Centered in the right wing's free space, as tall as most of the artwork.
    var bars: CGRect {
        let start = wing + collapsed.width + Self.innerGap
        let space = wing - Self.innerGap - Self.outerPadding
        let height = (artSide * 0.83).rounded()
        return CGRect(x: start + (space - barsWidth) / 2, y: (collapsed.height - height) / 2, width: barsWidth, height: height)
    }

    func bar(_ index: Int) -> CGRect {
        let area = bars
        return CGRect(x: area.minX + CGFloat(index) * (Self.barWidth + Self.barGap), y: area.minY, width: Self.barWidth, height: area.height)
    }

    /// Seconds per rise and fall for each bar. Unequal and not multiples of each other, so the four
    /// never line up into a visible loop.
    static let barPeriods: [Double] = [0.52, 0.43, 0.61, 0.47, 0.56, 0.40]

    /// How far each bar's colour is mixed toward white: the artwork's colour at the edges, nearly
    /// white in the middle, so the group reads as one glow rather than six sticks.
    static let barWhiteness: [CGFloat] = [0, 0.4, 0.7, 0.7, 0.4, 0]
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
