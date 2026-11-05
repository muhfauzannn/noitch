import Foundation
import Testing
@testable import Noitch

struct NowPlayingTests {
    private let start = Date(timeIntervalSince1970: 1_000)

    private func line(_ json: String) throws -> AdapterLine {
        try JSONDecoder().decode(AdapterLine.self, from: Data(json.utf8))
    }

    @Test func `a full update replaces everything`() throws {
        var state = NowPlaying()
        state.title = "Old"
        state.artist = "Old artist"
        let next = state.applying(try line(#"{"payload":{"title":"New","playing":true,"duration":200}}"#), now: start)
        #expect(next.title == "New")
        #expect(next.artist == "")
        #expect(next.isPlaying)
        #expect(next.duration == 200)
    }

    @Test func `a diff keeps what it does not mention`() throws {
        let full = NowPlaying().applying(try line(#"{"payload":{"title":"Song","artist":"Band","playing":true}}"#), now: start)
        let next = full.applying(try line(#"{"diff":true,"payload":{"artist":"Other"}}"#), now: start)
        #expect(next.title == "Song")
        #expect(next.artist == "Other")
        #expect(next.isPlaying)
    }

    @Test func `the clock runs from the reported position and stops at the end`() {
        var state = NowPlaying()
        state.isPlaying = true
        state.duration = 100
        state.elapsed = 10
        state.timestamp = start
        #expect(state.position(at: start.addingTimeInterval(5)) == 15)
        #expect(state.position(at: start.addingTimeInterval(500)) == 100)
    }

    @Test func `pausing freezes the clock where it had reached`() throws {
        var state = NowPlaying()
        state.title = "Song"
        state.isPlaying = true
        state.duration = 100
        state.elapsed = 10
        state.timestamp = start
        let paused = state.applying(try line(#"{"diff":true,"payload":{"playing":false}}"#), now: start.addingTimeInterval(7))
        #expect(paused.position(at: start.addingTimeInterval(60)) == 17)
    }

    @Test func `the reported timestamp anchors the clock`() throws {
        let state = NowPlaying().applying(
            try line(#"{"payload":{"title":"Song","playing":true,"elapsedTime":30,"timestamp":"1970-01-01T00:16:40.500Z"}}"#),
            now: start.addingTimeInterval(99)
        )
        #expect(state.timestamp == start.addingTimeInterval(0.5))
    }

    @Test func `empty artwork means no artwork`() throws {
        var state = NowPlaying()
        state.artwork = Data([1])
        let next = state.applying(try line(#"{"diff":true,"payload":{"artworkData":""}}"#), now: start)
        #expect(next.artwork == nil)
    }

    @Test func `shuffle and repeat use MediaRemote values`() throws {
        let state = NowPlaying().applying(try line(#"{"payload":{"shuffleMode":3,"repeatMode":2}}"#), now: start)
        #expect(state.isShuffled)
        #expect(state.repeatMode == .one)
        #expect(state.repeatMode.next == .off)
    }

    @Test func `the parent app wins, since a browser reports a helper process`() throws {
        let state = NowPlaying().applying(
            try line(#"{"payload":{"bundleIdentifier":"helper","parentApplicationBundleIdentifier":"browser"}}"#),
            now: start
        )
        #expect(state.bundleIdentifier == "browser")
    }
}
