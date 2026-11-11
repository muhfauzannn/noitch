import CoreGraphics
import Foundation
import Testing
@testable import Noitch

struct LiveTests {
    private let notch = CGSize(width: 185, height: 32)

    @Test func `the wings hold the artwork and six bars, clear of the notch`() {
        let layout = LiveLayout(collapsed: notch)
        #expect(layout.artSide == 18 && layout.wing == 35)
        #expect(layout.size == CGSize(width: notch.width + 70, height: notch.height))
        #expect(layout.artwork.maxX <= layout.wing - LiveLayout.innerGap)
        #expect(layout.bars.minX >= layout.wing + notch.width + LiveLayout.innerGap)
        #expect(layout.bars.maxX <= layout.size.width - LiveLayout.outerPadding)
        #expect(layout.bars.minY >= 0 && layout.bars.maxY <= notch.height)
        #expect(layout.bar(LiveLayout.barCount - 1).maxX.rounded() == layout.bars.maxX.rounded())
        #expect(LiveLayout.barPeriods.count == LiveLayout.barCount && LiveLayout.barWhiteness.count == LiveLayout.barCount)
        // Symmetric shades, so the group reads as one glow.
        #expect(LiveLayout.barWhiteness == LiveLayout.barWhiteness.reversed())
    }

    @Test func `the closed pill widens only while live`() {
        var geometry = IslandGeometry(display: .init(frame: CGRect(x: 0, y: 0, width: 1512, height: 982), notchHeight: 32, notchWidth: 185, menuBarHeight: 37))
        let narrow = geometry.panelFrame(for: .collapsed)
        geometry.isLive = true
        #expect(geometry.pillSize(for: .collapsed).width == LiveLayout(collapsed: notch).size.width)
        #expect(geometry.panelFrame(for: .collapsed).width > narrow.width)
        #expect(geometry.panelFrame(for: .collapsed).midX == narrow.midX)
        #expect(geometry.collapsedSize == notch)
        #expect(geometry.pillSize(for: .expanded) == IslandGeometry.expandedSize)
    }

    @Test func `the lyric strip grows the pill down, under the notch, without moving the wings`() {
        let layout = LiveLayout(collapsed: notch)
        #expect(layout.size(withLyric: false) == layout.size)
        #expect(layout.size(withLyric: true) == CGSize(width: layout.size.width, height: notch.height + LiveLayout.lyricStrip))
        #expect(layout.lyric.minY >= notch.height && layout.lyric.maxY <= layout.size(withLyric: true).height)
        #expect(layout.lyric.minX >= LiveLayout.outerPadding && layout.lyric.maxX <= layout.size.width - LiveLayout.outerPadding)
        var geometry = IslandGeometry(display: .init(frame: CGRect(x: 0, y: 0, width: 1512, height: 982), notchHeight: 32, notchWidth: 185, menuBarHeight: 37), isLive: true)
        let wings = geometry.panelFrame(for: .collapsed)
        geometry.showsLiveLyric = true
        let tall = geometry.panelFrame(for: .collapsed)
        #expect(tall.height == wings.height + LiveLayout.lyricStrip && tall.maxY == wings.maxY && tall.width == wings.width)
        #expect(geometry.pillSize(for: .expanded) == IslandGeometry.expandedSize)
    }

    @Test func `the lyric line is the one being sung, or none without synced lyrics`() {
        let lyrics = Lyrics(synced: [.init(start: 2, text: "one"), .init(start: 4, text: ""), .init(start: 6, text: "three")])
        #expect(LiveRule.lyricLine(lyrics, position: 0, offset: 0) == "♪")
        #expect(LiveRule.lyricLine(lyrics, position: 2.5, offset: 0) == "one")
        #expect(LiveRule.lyricLine(lyrics, position: 4.5, offset: 0) == "♪")
        #expect(LiveRule.lyricLine(lyrics, position: 5.5, offset: 0.5) == "three")
        #expect(LiveRule.lyricLine(Lyrics(plain: ["plain"]), position: 3, offset: 0) == nil)
        #expect(LiveRule.lyricLine(nil, position: 3, offset: 0) == nil)
    }

    @Test func `music shows while playing and briefly after a pause`() {
        var playing = NowPlaying()
        playing.title = "Song"
        playing.isPlaying = true
        #expect(LiveRule.shows(playing, enabled: true, pausedSince: nil, now: 100))
        #expect(!LiveRule.shows(playing, enabled: false, pausedSince: nil, now: 100))
        var paused = playing
        paused.isPlaying = false
        #expect(LiveRule.shows(paused, enabled: true, pausedSince: 95, now: 100))
        #expect(!LiveRule.shows(paused, enabled: true, pausedSince: 80, now: 100))
        #expect(!LiveRule.shows(NowPlaying(), enabled: true, pausedSince: nil, now: 100))
    }

    @Test func `players include music apps and browsers, not everything`() {
        #expect(MediaPlayers.isPlayer("com.spotify.client"))
        #expect(MediaPlayers.isPlayer("com.apple.Safari"))
        #expect(!MediaPlayers.isPlayer("com.apple.finder"))
        #expect(!MediaPlayers.isPlayer(nil))
    }
}
