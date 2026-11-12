import Testing
@testable import Noitch

struct LyricRomanizerTests {
    @Test func latinIsUnchanged() {
        #expect(LyricRomanizer.romanize("Hello, café!") == nil)
        #expect(LyricRomanizer.romanize("♪ 123") == nil)
        #expect(LyricRomanizer.romanize("") == nil)
    }

    @Test func japaneseUsesReadingsInsteadOfMandarin() {
        let reading = LyricRomanizer.romanize("君の名前を教えて") ?? ""
        #expect(reading.contains("kimi"))
        #expect(reading.contains("namae"))
        #expect(!reading.contains("jun"))
    }

    @Test func trackContextIncludesKanjiOnlyLines() {
        let lyrics = Lyrics(synced: [LyricLine(start: 1, text: "君の名前"), LyricLine(start: 2, text: "世界"), LyricLine(start: 3, text: "世界")])
        let prepared = LyricRomanizer.prepare(lyrics)
        #expect(prepared.synced[1].romanization == "sekai")
        #expect(prepared.synced[1].romanization == prepared.synced[2].romanization)
        #expect(prepared.synced[1].start == 2)
        #expect(prepared.synced[1].text == "世界")
    }

    @Test func otherScriptsAreRomanized() {
        #expect(LyricRomanizer.romanize("你好") == "ni hao")
        #expect(LyricRomanizer.romanize("사랑해") != nil)
        #expect(LyricRomanizer.romanize("Привет") == "Privet")
    }
}
