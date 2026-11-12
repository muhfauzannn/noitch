import Foundation

/// Runs once when lyrics arrive; scenes only consume the resulting strings.
enum LyricRomanizer {
    static func romanize(_ text: String, japanese: Bool? = nil) -> String? {
        guard text.unicodeScalars.contains(where: { $0.properties.isAlphabetic && $0.value > 0x024F }) else { return nil }
        let hasKana = text.unicodeScalars.contains { (0x3040...0x30FF).contains($0.value) }

        var reading = text
        if japanese ?? hasKana {
            let source = text as CFString
            guard let tokenizer = CFStringTokenizerCreate(nil, source,
                CFRange(location: 0, length: CFStringGetLength(source)), kCFStringTokenizerUnitWord,
                CFLocaleCreate(nil, CFLocaleIdentifier(rawValue: "ja" as CFString))) else { return nil }
            let original = text as NSString
            var parts: [String] = []
            var end = 0
            while CFStringTokenizerAdvanceToNextToken(tokenizer).rawValue != 0 {
                let range = CFStringTokenizerGetCurrentTokenRange(tokenizer)
                if range.location > end {
                    parts.append(original.substring(with: NSRange(location: end, length: range.location - end)))
                }
                let word = CFStringTokenizerCopyCurrentTokenAttribute(tokenizer,
                    kCFStringTokenizerAttributeLatinTranscription) as? String
                parts.append(word ?? original.substring(with: NSRange(location: range.location, length: range.length)))
                end = range.location + range.length
            }
            if end < original.length { parts.append(original.substring(from: end)) }
            reading = parts.joined(separator: " ")
            guard !reading.unicodeScalars.contains(where: { (0x3400...0x9FFF).contains($0.value) }) else { return nil }
        }
        guard let latin = reading.applyingTransform(.toLatin, reverse: false)?
            .applyingTransform(.stripCombiningMarks, reverse: false) else { return nil }
        let result = latin.split(whereSeparator: \.isWhitespace).joined(separator: " ")
        return result.isEmpty || result == text ? nil : result
    }

    static func prepare(_ lyrics: Lyrics) -> Lyrics {
        var result = lyrics
        let japanese = lyrics.synced.contains { line in
            line.text.unicodeScalars.contains { (0x3040...0x30FF).contains($0.value) }
        }
        var cache: [String: String] = [:]
        for index in result.synced.indices {
            let text = result.synced[index].text
            if cache[text] == nil { cache[text] = romanize(text, japanese: japanese) ?? "" }
            result.synced[index].romanization = cache[text] ?? ""
        }
        return result
    }
}
