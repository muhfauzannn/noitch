import CoreGraphics
import Foundation
import Testing
@testable import Noitch

struct HomeTests {
    private let layout = HomeLayout(size: IslandGeometry.expandedSize, headerHeight: 38)

    @Test func `no two controls overlap, so a click has one meaning`() {
        for first in HomeControl.allCases {
            for second in HomeControl.allCases where first != second {
                #expect(!layout.frame(of: first).intersects(layout.frame(of: second)), "\(first) overlaps \(second)")
            }
        }
    }

    @Test func `everything stays inside the pill`() {
        let pill = CGRect(origin: .zero, size: IslandGeometry.expandedSize)
        for control in HomeControl.allCases {
            #expect(pill.contains(layout.frame(of: control)), "\(control)")
        }
    }

    @Test func `header buttons fit on a display without a notch`() {
        let short = HomeLayout(size: IslandGeometry.expandedSize, headerHeight: 24)
        #expect(short.frame(of: .lyrics).minY >= 0)
    }

    @Test func `points find their control`() {
        let center = CGPoint(x: layout.frame(of: .playPause).midX, y: layout.frame(of: .playPause).midY)
        #expect(layout.control(at: center) == .playPause)
        #expect(layout.control(at: CGPoint(x: 2, y: 2)) == nil)
    }

    @Test func `the slider maps to a clamped fraction`() {
        let track = layout.sliderTrack
        #expect(layout.sliderFraction(at: track.minX) == 0)
        #expect(layout.sliderFraction(at: track.midX) == 0.5)
        #expect(layout.sliderFraction(at: track.maxX + 50) == 1)
    }

    @Test func `icons fill their grid without leaving it`() {
        let grid = CGRect(x: 0, y: 0, width: 24, height: 24)
        for icon in HomeIcon.all {
            let bounds = icon.path(side: 24).boundingBoxOfPath
            #expect(grid.contains(bounds), "\(icon)")
            #expect(bounds.width > 8 && bounds.height > 8, "\(icon)")
        }
    }

    @Test func `formats time`() {
        #expect(HomeModel.formatTime(0) == "0:00")
        #expect(HomeModel.formatTime(65) == "1:05")
        #expect(HomeModel.formatTime(3725) == "1:02:05")
    }

    @Test func `the next tick is the next second, or an earlier lyric`() {
        #expect(abs(HomeModel.nextTick(position: 10.25, rate: 1, nextLyric: nil) - 0.75) < 0.0001)
        #expect(abs(HomeModel.nextTick(position: 10.25, rate: 1, nextLyric: 10.5) - 0.25) < 0.0001)
        #expect(abs(HomeModel.nextTick(position: 10.5, rate: 2, nextLyric: nil) - 0.25) < 0.0001)
    }

    @Test func `nothing playing shows the placeholder`() {
        let model = HomeModel(nowPlaying: NowPlaying(), lyrics: nil, isLoadingLyrics: false, position: 0, showsPanel: false, settings: AppSettings())
        #expect(!model.hasTrack)
        #expect(model.title == HomeModel().title)
    }

    @Test func `the current lyric follows the position`() {
        var nowPlaying = NowPlaying()
        nowPlaying.title = "Song"
        nowPlaying.duration = 100
        let lyrics = Lyrics(synced: [.init(start: 1, text: "one"), .init(start: 2, text: "two"), .init(start: 3, text: "three")])
        let model = HomeModel(nowPlaying: nowPlaying, lyrics: lyrics, isLoadingLyrics: false, position: 2.5, showsPanel: true, settings: AppSettings())
        #expect(model.lyricLine == "two")
        #expect(model.panelLines == ["one", "two", "three"])
        #expect(model.remainingText == "-1:37")
    }

    @Test func `turning lyrics off removes them everywhere`() {
        var nowPlaying = NowPlaying()
        nowPlaying.title = "Song"
        var settings = AppSettings()
        settings.showLyrics = false
        let lyrics = Lyrics(synced: [.init(start: 0, text: "one")])
        let model = HomeModel(nowPlaying: nowPlaying, lyrics: lyrics, isLoadingLyrics: false, position: 1, showsPanel: true, settings: settings)
        #expect(model.lyricLine == "")
        #expect(!model.showsLyricsPanel)
        #expect(!model.showsLyricsButton)
    }

    @Test func `settings survive a round trip and default on`() throws {
        let defaults = try #require(UserDefaults(suiteName: "noitch.tests.\(UUID().uuidString)"))
        #expect(AppSettings.load(from: defaults) == AppSettings())
        var settings = AppSettings()
        settings.openOnHover = false
        settings.tintWithArtwork = false
        settings.save(to: defaults)
        #expect(AppSettings.load(from: defaults) == settings)
    }
}
