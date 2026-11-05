import Testing
@testable import Noitch

struct LyricsTests {
    @Test func `parses timestamps, skips metadata and sorts`() {
        let lines = Lyrics.parseLRC("""
        [ar:Someone]
        [00:12.50]second
        [00:02.00]first
        [01:00.00][00:30.00]chorus
        """)
        #expect(lines.map(\.text) == ["first", "second", "chorus", "chorus"])
        #expect(lines.map(\.start) == [2, 12.5, 30, 60])
    }

    @Test func `keeps blank lines, which mark instrumental gaps`() {
        #expect(Lyrics.parseLRC("[00:01.00]\n").first?.text == "")
    }

    @Test func `finds the line being sung`() {
        let lyrics = Lyrics(synced: [.init(start: 5, text: "a"), .init(start: 10, text: "b")])
        #expect(lyrics.lineIndex(at: 1) == nil)
        #expect(lyrics.lineIndex(at: 5) == 0)
        #expect(lyrics.lineIndex(at: 9.9) == 0)
        #expect(lyrics.lineIndex(at: 100) == 1)
    }

    @Test func `knows when the line next changes`() {
        let lyrics = Lyrics(synced: [.init(start: 5, text: "a"), .init(start: 10, text: "b")])
        #expect(lyrics.nextChange(after: 0) == 5)
        #expect(lyrics.nextChange(after: 7) == 10)
        #expect(lyrics.nextChange(after: 11) == nil)
    }

    @Test(arguments: [
        ("Song (feat. Someone)", "Song"),
        ("Song - Remastered 2011", "Song"),
        ("Song [Official Video]", "Song"),
        ("Song (Part 2)", "Song (Part 2)"),
        ("Love - Hate", "Love - Hate"),
    ])
    func `cleans what players add to titles`(title: String, expected: String) {
        #expect(Lyrics.cleanTitle(title) == expected)
    }

    @Test func `prefers synced lyrics, then the closest length`() {
        let plain = LyricsRecord(duration: 200, plainLyrics: "plain")
        let far = LyricsRecord(duration: 210, syncedLyrics: "[00:01.00]far")
        let near = LyricsRecord(duration: 201, syncedLyrics: "[00:01.00]near")
        #expect(LyricsRecord.best(of: [plain, far, near], duration: 200) == near)
    }

    @Test func `rejects another recording of the song`() {
        let other = LyricsRecord(duration: 300, syncedLyrics: "[00:01.00]other")
        #expect(LyricsRecord.best(of: [other], duration: 200) == nil)
    }
}
