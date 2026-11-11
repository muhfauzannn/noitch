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
        #expect(short.frame(of: .playPause).minY >= 0)
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

    @Test func `the glow fades out before the top edge, where the flares meet the pill`() {
        for header in [CGFloat(24), 32, 38] {
            let layout = HomeLayout(size: IslandGeometry.expandedSize, headerHeight: header)
            #expect(layout.glowRadii.height >= layout.glowCenter.y)
            #expect(layout.glowCenter.x + layout.glowRadii.width < layout.size.width)
        }
    }

    @Test func `formats time`() {
        #expect(HomeModel.formatTime(0) == "0:00")
        #expect(HomeModel.formatTime(65) == "1:05")
        #expect(HomeModel.formatTime(3725) == "1:02:05")
    }

    @Test func `the next tick is the next second, or an earlier lyric`() {
        #expect(abs((HomeModel.nextTick(position: 10.25, rate: 1, nextLyric: nil) ?? 0) - 0.75) < 0.0001)
        #expect(abs((HomeModel.nextTick(position: 10.25, rate: 1, nextLyric: 10.5) ?? 0) - 0.25) < 0.0001)
        #expect(abs((HomeModel.nextTick(position: 10.5, rate: 2, nextLyric: nil) ?? 0) - 0.25) < 0.0001)
    }

    @Test func `the stage wakes only for the next line`() {
        #expect(HomeModel.nextTick(position: 10.25, rate: 1, nextLyric: nil, wholeSeconds: false) == nil)
        #expect(abs((HomeModel.nextTick(position: 10.25, rate: 1, nextLyric: 14.25, wholeSeconds: false) ?? 0) - 4) < 0.0001)
    }

    @Test(arguments: [CGFloat(24), 32, 38], [false, true])
    func `the lyrics button sits clear of everything, in both places`(header: CGFloat, calendar: Bool) {
        var layout = HomeLayout(size: IslandGeometry.expandedSize, headerHeight: header)
        layout.showsCalendar = calendar
        let headerLayout = HeaderLayout(size: layout.size, headerHeight: header)
        let pill = CGRect(origin: .zero, size: layout.size)
        for onStage in [false, true] {
            let button = layout.lyricsButton(onStage: onStage)
            #expect(pill.contains(button))
            #expect(!button.intersects(headerLayout.frame(of: .settings)))
        }
        // The stage hides while the button sits beside the calendar, so only its corner place counts.
        for slot in HomeLayout.stageSlots { #expect(!layout.lyricsButton(onStage: true).intersects(layout.stageLine(slot)), "slot \(slot)") }
        #expect(layout.title.maxX <= layout.lyricsButton(onStage: false).minX)
        #expect(layout.lyricsButton(onStage: true).maxX == layout.size.width - HomeLayout.sidePadding)
        if calendar { #expect(!layout.lyricsButton(onStage: false).intersects(layout.calendarPanel)) }
    }

    @Test(arguments: [CGFloat(24), 32, 38])
    func `the stage's lines fit the body and its glow fades before the top`(header: CGFloat) {
        let layout = HomeLayout(size: IslandGeometry.expandedSize, headerHeight: header)
        #expect(layout.stageLine(-1).minY >= header)
        #expect(layout.stageLine(2).maxY <= layout.size.height - HomeLayout.bottomPadding)
        for slot in HomeLayout.stageSlots.dropLast() { #expect(layout.stageLine(slot).maxY <= layout.stageLine(slot + 1).minY) }
        #expect(layout.stageGlowRadii.height >= layout.stageGlowCenter.y)
        #expect(layout.stageGlowCenter.x + layout.stageGlowRadii.width < layout.size.width)
        let middle = CGPoint(x: layout.size.width / 2, y: layout.stageCenterY)
        #expect(layout.stageSlot(at: middle) == 0)
        #expect(layout.control(at: middle, onStage: true) == nil)
        let button = layout.lyricsButton(onStage: true)
        #expect(layout.control(at: CGPoint(x: button.midX, y: button.midY), onStage: true) == .lyrics)
        // On the stage the transport is gone, so it doesn't answer.
        let play = layout.frame(of: .playPause)
        #expect(layout.control(at: CGPoint(x: play.midX, y: play.midY), onStage: true) == nil)
    }

    @Test func `the stage shows the line before, the one sung and the two after`() {
        var nowPlaying = NowPlaying()
        nowPlaying.title = "Song"
        nowPlaying.duration = 100
        let lyrics = Lyrics(synced: [.init(start: 1, text: "one"), .init(start: 2, text: ""), .init(start: 3, text: "three"), .init(start: 4, text: "four")])
        let middle = HomeModel(nowPlaying: nowPlaying, lyrics: lyrics, isLoadingLyrics: false, position: 2.5, settings: HomeSettings(), onStage: true)
        #expect(middle.isOnStage && middle.stageLines == ["one", "♪", "three", "four"] && !middle.stageIsStatus)
        let intro = HomeModel(nowPlaying: nowPlaying, lyrics: lyrics, isLoadingLyrics: false, position: 0, settings: HomeSettings())
        #expect(intro.stageLines == ["", "♪", "one", "♪"])
        let end = HomeModel(nowPlaying: nowPlaying, lyrics: lyrics, isLoadingLyrics: false, position: 9, settings: HomeSettings())
        #expect(end.stageLines == ["three", "four", "", ""])
        let loading = HomeModel(nowPlaying: nowPlaying, lyrics: nil, isLoadingLyrics: true, position: 0, settings: HomeSettings(), onStage: true)
        #expect(loading.stageIsStatus && loading.stageLines[1] == "Looking for lyrics…")
        #expect(HomeModel(nowPlaying: NowPlaying(), lyrics: nil, isLoadingLyrics: false, position: 0, settings: HomeSettings()).stageLines[1] == "Nothing playing")
    }

    @Test func `turning lyrics off leaves the stage and hides its button`() {
        var settings = HomeSettings()
        settings.showLyrics = false
        let model = HomeModel(nowPlaying: NowPlaying(), lyrics: nil, isLoadingLyrics: false, position: 0, settings: settings, onStage: true)
        #expect(!model.isOnStage && !model.showsLyricsButton)
    }

    @Test func `nothing playing shows the placeholder`() {
        let model = HomeModel(nowPlaying: NowPlaying(), lyrics: nil, isLoadingLyrics: false, position: 0, settings: HomeSettings())
        #expect(!model.hasTrack)
        #expect(model.title == HomeModel().title)
    }

    @Test func `the current lyric follows the position`() {
        var nowPlaying = NowPlaying()
        nowPlaying.title = "Song"
        nowPlaying.duration = 100
        let lyrics = Lyrics(synced: [.init(start: 1, text: "one"), .init(start: 2, text: "two"), .init(start: 3, text: "three")])
        let model = HomeModel(nowPlaying: nowPlaying, lyrics: lyrics, isLoadingLyrics: false, position: 2.5, settings: HomeSettings())
        #expect(model.lyric == "two" && model.nextLyric == "three" && model.lyricIndex == 1)
        #expect(model.remainingText == "-1:37")
        let intro = HomeModel(nowPlaying: nowPlaying, lyrics: lyrics, isLoadingLyrics: false, position: 0.5, settings: HomeSettings())
        #expect(intro.lyric == "♪" && intro.nextLyric == "one" && intro.lyricIndex == nil)
    }

    @Test func `a skip dims the old track and empties the clock until the new one arrives`() {
        var nowPlaying = NowPlaying()
        nowPlaying.title = "Song"
        nowPlaying.duration = 100
        let lyrics = Lyrics(synced: [.init(start: 1, text: "one")])
        let model = HomeModel(nowPlaying: nowPlaying, lyrics: lyrics, isLoadingLyrics: false, position: 50, settings: HomeSettings(), isSkipping: true)
        #expect(model.isSkipping && model.title == "Song")
        #expect(model.elapsedText == "0:00" && model.lyric.isEmpty)
    }

    @Test func `the lyric lines fit between the artist and the slider`() {
        for header in [CGFloat(24), 32, 38] {
            let layout = HomeLayout(size: IslandGeometry.expandedSize, headerHeight: header)
            #expect(layout.lyric.minY >= layout.artist.maxY)
            #expect(layout.nextLyric.maxY <= layout.sliderTrack.minY - 4)
            #expect(layout.artwork.maxY <= IslandGeometry.expandedSize.height - HomeLayout.bottomPadding)
        }
    }

    @Test func `turning lyrics off removes them everywhere`() {
        var nowPlaying = NowPlaying()
        nowPlaying.title = "Song"
        var settings = HomeSettings()
        settings.showLyrics = false
        let lyrics = Lyrics(synced: [.init(start: 0, text: "one")])
        let model = HomeModel(nowPlaying: nowPlaying, lyrics: lyrics, isLoadingLyrics: false, position: 1, settings: settings)
        #expect(model.lyric.isEmpty && model.nextLyric.isEmpty)
    }

    @Test func `settings survive a round trip and default on`() throws {
        let defaults = try #require(UserDefaults(suiteName: "noitch.tests.\(UUID().uuidString)"))
        #expect(AppSettings.load(from: defaults) == AppSettings())
        var settings = AppSettings()
        settings.general.openOnHover = false
        settings.general.hoverDelay = 0.3
        settings.general.display = .main
        settings.pages.showUsage = false
        settings.pages.openOn = .home
        settings.home.tintWithArtwork = false
        settings.home.lyricsOffset = -0.5
        settings.usage.showCodex = false
        settings.usage.refreshMinutes = 5
        settings.save(to: defaults)
        #expect(AppSettings.load(from: defaults) == settings)
    }

    @Test func `choices stored by an older version still read`() throws {
        let defaults = try #require(UserDefaults(suiteName: "noitch.tests.\(UUID().uuidString)"))
        defaults.set(false, forKey: "showLyrics")
        defaults.set(false, forKey: "openOnHover")
        defaults.set("sideways", forKey: "display")
        defaults.set(0.1, forKey: "usageRefreshMinutes")
        let settings = AppSettings.load(from: defaults)
        #expect(!settings.home.showLyrics && !settings.general.openOnHover)
        // Unknown and out-of-range values fall back to something safe.
        #expect(settings.general.display == .notch && settings.usage.refreshMinutes == 1)
    }

    @Test func `the lyrics timing shifts which line is sung`() {
        var nowPlaying = NowPlaying()
        nowPlaying.title = "Song"
        nowPlaying.duration = 100
        let lyrics = Lyrics(synced: [.init(start: 1, text: "one"), .init(start: 2, text: "two"), .init(start: 3, text: "three")])
        var settings = HomeSettings()
        settings.lyricsOffset = 0.5
        let early = HomeModel(nowPlaying: nowPlaying, lyrics: lyrics, isLoadingLyrics: false, position: 1.6, settings: settings)
        #expect(early.lyric == "two")
        settings.showNextLyric = false
        let single = HomeModel(nowPlaying: nowPlaying, lyrics: lyrics, isLoadingLyrics: false, position: 1.6, settings: settings)
        #expect(single.lyric == "two" && single.nextLyric.isEmpty)
    }

    @Test func `pages open where the settings say, and Home is always there`() {
        var pages = PageSettings()
        #expect(pages.page(opening: .usage) == .usage)
        pages.openOn = .home
        #expect(pages.page(opening: .usage) == .home)
        pages.openOn = .usage
        pages.showUsage = false
        #expect(pages.visible == [.home] && pages.page(opening: .usage) == .home)
        let header = HeaderLayout(size: IslandGeometry.expandedSize, headerHeight: 32, pages: pages.visible)
        #expect(header.control(at: CGPoint(x: HeaderLayout.sidePadding + 45, y: 16)) == nil)
    }
}
