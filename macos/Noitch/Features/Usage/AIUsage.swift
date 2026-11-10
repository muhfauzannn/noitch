import Foundation

/// Token usage read from the local logs of AI coding tools. Pure values in, values out: the source
/// hands it bytes and the time, so all of it is unit-tested. The Windows app uses the same rules.
enum AITool: Equatable, Sendable {
    case claude
    case codex
}

struct UsageEntry: Equatable, Sendable {
    var tool: AITool
    /// Seconds since 1970.
    var time: Double
    var model: String
    /// Input not read from or written to a cache.
    var input = 0
    var output = 0
    var cacheWrite5m = 0
    var cacheWrite1h = 0
    var cacheRead = 0

    var tokens: Int { input + output + cacheWrite5m + cacheWrite1h + cacheRead }

    /// In US dollars, or nil for a model without a known price.
    var cost: Double? {
        guard let price = ModelPrice.find(model) else { return nil }
        let million = 1_000_000.0
        return (Double(input) * price.input + Double(output) * price.output + Double(cacheRead) * price.cacheRead
            + Double(cacheWrite5m) * price.input * 1.25 + Double(cacheWrite1h) * price.input * 2) / million
    }
}

/// Codex reports its own five-hour limit, so it isn't estimated.
struct RateLimit: Equatable, Sendable {
    var usedPercent: Double
    var resetsAt: Double
}

/// Dollars per million tokens. Cache writes cost 1.25× input for five minutes and 2× for an hour.
struct ModelPrice: Equatable, Sendable {
    var input: Double
    var output: Double
    var cacheRead: Double

    /// Most specific first: "opus-5" would otherwise match "opus-5-5". Anthropic API list prices as of
    /// 2026-11; OpenAI only for models whose price is known. Anything else shows no cost.
    static let table: [(String, ModelPrice)] = [
        ("fable-5-2", ModelPrice(input: 8, output: 40, cacheRead: 0.2)),
        ("mythos-5-2", ModelPrice(input: 8, output: 40, cacheRead: 0.2)),
        ("fable-5", ModelPrice(input: 10, output: 50, cacheRead: 1)),
        ("mythos-5", ModelPrice(input: 10, output: 50, cacheRead: 1)),
        ("opus-5-5", ModelPrice(input: 4, output: 20, cacheRead: 0.2)),
        ("opus-5", ModelPrice(input: 5, output: 25, cacheRead: 0.5)),
        ("opus-4-8", ModelPrice(input: 5, output: 25, cacheRead: 0.5)),
        ("opus-4-7", ModelPrice(input: 5, output: 25, cacheRead: 0.5)),
        ("opus-4-6", ModelPrice(input: 5, output: 25, cacheRead: 0.5)),
        ("opus-4-5", ModelPrice(input: 5, output: 25, cacheRead: 0.5)),
        ("opus-4", ModelPrice(input: 15, output: 75, cacheRead: 1.5)),
        ("sonnet-5-5", ModelPrice(input: 2, output: 10, cacheRead: 0.1)),
        ("sonnet-5", ModelPrice(input: 2, output: 10, cacheRead: 0.2)),
        ("sonnet-4", ModelPrice(input: 3, output: 15, cacheRead: 0.3)),
        ("haiku-5-5", ModelPrice(input: 0.1, output: 0.5, cacheRead: 0.01)),
        ("haiku-4-5", ModelPrice(input: 1, output: 5, cacheRead: 0.1)),
        ("gpt-5-mini", ModelPrice(input: 0.25, output: 2, cacheRead: 0.025)),
        ("gpt-5-nano", ModelPrice(input: 0.05, output: 0.4, cacheRead: 0.005)),
        ("gpt-5-codex", ModelPrice(input: 1.25, output: 10, cacheRead: 0.125)),
        ("gpt-5.1-codex-mini", ModelPrice(input: 0.25, output: 2, cacheRead: 0.025)),
        ("gpt-5.1", ModelPrice(input: 1.25, output: 10, cacheRead: 0.125)),
    ]

    static func find(_ model: String) -> ModelPrice? {
        if model == "gpt-5" { return ModelPrice(input: 1.25, output: 10, cacheRead: 0.125) }
        return table.first { model.contains($0.0) }?.1
    }
}

/// What Codex remembers between lines of one file.
struct CodexFileState: Equatable, Sendable {
    var model = ""
    /// Codex repeats a usage event when only the limits change; a total that didn't grow is a repeat.
    var lastTotal = 0
}

/// Entries from the last day, kept small: a few numbers per request, never the log text.
struct UsageLog: Sendable {
    static let blockLength: Double = 5 * 3600

    private(set) var entries: [UsageEntry] = []
    private(set) var codexLimit: RateLimit?
    private var codexLimitTime = -Double.infinity
    /// Claude Code writes one line per content block, each repeating the request's usage.
    private var seenClaude: [String: Double] = [:]

    /// Reads whole lines and returns how many bytes it used, so a half-written last line is read later.
    mutating func readClaude(_ bytes: UnsafeRawBufferPointer, since cutoff: Double) -> Int {
        Self.forEachLine(bytes, containing: ["\"usage\""]) { line, _ in
            guard let (entry, key) = Self.parseClaude(line), entry.time >= cutoff, entry.tokens > 0 else { return }
            if let key {
                guard seenClaude[key] == nil else { return }
                seenClaude[key] = entry.time
            }
            entries.append(entry)
        }
    }

    mutating func readCodex(_ bytes: UnsafeRawBufferPointer, state: inout CodexFileState, since cutoff: Double) -> Int {
        Self.forEachLine(bytes, containing: ["\"turn_context\"", "\"token_count\""]) { line, pattern in
            if pattern == 0 {
                if let payload = JSONScan.value("payload", in: line),
                   let model = JSONScan.value("model", in: payload).flatMap(JSONScan.string) {
                    state.model = model
                }
                return
            }
            guard let payload = JSONScan.value("payload", in: line),
                  let time = JSONScan.value("timestamp", in: line).flatMap(JSONScan.string).flatMap(Self.parseTime) else { return }
            if let limits = JSONScan.value("rate_limits", in: payload),
               let primary = JSONScan.value("primary", in: limits),
               let used = JSONScan.value("used_percent", in: primary).flatMap(JSONScan.number),
               let resets = JSONScan.value("resets_at", in: primary).flatMap(JSONScan.number),
               time >= codexLimitTime {
                codexLimitTime = time
                codexLimit = RateLimit(usedPercent: used, resetsAt: resets)
            }
            guard let info = JSONScan.value("info", in: payload),
                  let total = JSONScan.value("total_token_usage", in: info)
                      .flatMap({ JSONScan.value("total_tokens", in: $0) }).flatMap(JSONScan.number).map(Int.init),
                  total > state.lastTotal,
                  let last = JSONScan.value("last_token_usage", in: info) else { return }
            state.lastTotal = total
            guard time >= cutoff else { return }
            let input = Self.int("input_tokens", in: last)
            let cached = Self.int("cached_input_tokens", in: last)
            var entry = UsageEntry(tool: .codex, time: time, model: state.model.isEmpty ? "codex" : state.model)
            // OpenAI counts cached input inside input; split it out to price it.
            entry.input = max(input - cached, 0)
            entry.cacheRead = cached
            entry.cacheWrite5m = Self.int("cache_write_input_tokens", in: last)
            entry.output = Self.int("output_tokens", in: last)
            if entry.tokens > 0 { entries.append(entry) }
        }
    }

    mutating func prune(before cutoff: Double) {
        entries.removeAll { $0.time < cutoff }
        seenClaude = seenClaude.filter { $0.value >= cutoff }
    }

    typealias Line = Slice<UnsafeBufferPointer<UInt8>>

    /// Claude Code lines carry the whole response, often hundreds of kilobytes, and only a few
    /// fields are needed. Quotes inside JSON strings are always escaped, so `"key":` can only match a
    /// real key, and `memmem` finds each one without walking the text.
    private static func parseClaude(_ line: Line) -> (UsageEntry, String?)? {
        guard let usage = rawValue(after: "\"usage\":", in: line),
              let time = stringValue(after: "\"timestamp\":\"", in: line).flatMap(parseTime) else { return nil }
        let model = stringValue(after: "\"model\":\"", in: line) ?? "claude"
        guard model != "<synthetic>" else { return nil }
        var entry = UsageEntry(tool: .claude, time: time, model: model)
        entry.input = int("input_tokens", in: usage)
        entry.output = int("output_tokens", in: usage)
        entry.cacheRead = int("cache_read_input_tokens", in: usage)
        let written = int("cache_creation_input_tokens", in: usage)
        // The split by lifetime is newer than the total; without it, price all of it as five minutes.
        let longLived = JSONScan.value("cache_creation", in: usage).map { int("ephemeral_1h_input_tokens", in: $0) } ?? 0
        entry.cacheWrite1h = min(longLived, written)
        entry.cacheWrite5m = written - entry.cacheWrite1h
        let id = stringValue(after: "\"id\":\"msg_", in: line)
        let request = stringValue(after: "\"requestId\":\"", in: line)
        let key = id.map { "\($0):\(request ?? "")" }
        return (entry, key)
    }

    private static func find(_ pattern: StaticString, in line: Line) -> Int? {
        guard let base = line.base.baseAddress else { return nil }
        let match = memmem(base + line.startIndex, line.count, pattern.utf8Start, pattern.utf8CodeUnitCount)
        return match.map { UnsafeRawPointer($0) - UnsafeRawPointer(base) + pattern.utf8CodeUnitCount }
    }

    /// The text of a string whose opening quote ends `pattern`, up to its closing quote.
    private static func stringValue(after pattern: StaticString, in line: Line) -> String? {
        guard let start = find(pattern, in: line) else { return nil }
        var end = start
        while end < line.endIndex, line[end] != UInt8(ascii: "\"") {
            end += line[end] == UInt8(ascii: "\\") ? 2 : 1
        }
        return end < line.endIndex ? String(decoding: line[start..<end], as: UTF8.self) : nil
    }

    private static func rawValue(after pattern: StaticString, in line: Line) -> Line? {
        guard let start = find(pattern, in: line) else { return nil }
        let object = line[start...]
        return JSONScan.valueEnd(object, start).map { line[start..<$0] }
    }

    /// ISO 8601 in UTC or with an offset, as both tools write it.
    static func parseTime(_ text: String) -> Double? {
        let b = Array(text.utf8)
        func digits(_ start: Int, _ count: Int) -> Int? {
            guard start + count <= b.count else { return nil }
            var value = 0
            for i in start..<start + count {
                guard b[i] >= 48, b[i] <= 57 else { return nil }
                value = value * 10 + Int(b[i] - 48)
            }
            return value
        }
        guard let year = digits(0, 4), let month = digits(5, 2), let day = digits(8, 2),
              let hour = digits(11, 2), let minute = digits(14, 2), let second = digits(17, 2) else { return nil }
        var i = 19
        var fraction = 0.0
        if i < b.count, b[i] == UInt8(ascii: ".") {
            var scale = 0.1
            i += 1
            while i < b.count, b[i] >= 48, b[i] <= 57 {
                fraction += Double(b[i] - 48) * scale
                scale /= 10
                i += 1
            }
        }
        var offset = 0
        if i < b.count, b[i] == UInt8(ascii: "+") || b[i] == UInt8(ascii: "-") {
            guard let h = digits(i + 1, 2), let m = digits(i + 4, 2) else { return nil }
            offset = (h * 3600 + m * 60) * (b[i] == UInt8(ascii: "+") ? 1 : -1)
        }
        // Days from the civil calendar, valid for any Gregorian date.
        let y = month <= 2 ? year - 1 : year
        let era = (y >= 0 ? y : y - 399) / 400
        let yearOfEra = y - era * 400
        let dayOfYear = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1
        let dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear
        let days = era * 146_097 + dayOfEra - 719_468
        return Double(days * 86400 + hour * 3600 + minute * 60 + second - offset) + fraction
    }

    private static func int<C: ByteSlice>(_ key: String, in object: C) -> Int {
        JSONScan.value(key, in: object).flatMap(JSONScan.number).map { Int($0) } ?? 0
    }

    /// Calls `body` for each complete line holding one of `patterns`, in file order, with the index
    /// of the first pattern found. `memchr` and `memmem` skip the rest at memory speed: most of a log
    /// is tool output. Returns the bytes up to the last newline.
    private static func forEachLine(
        _ bytes: UnsafeRawBufferPointer,
        containing patterns: [StaticString],
        _ body: (Slice<UnsafeBufferPointer<UInt8>>, Int) -> Void
    ) -> Int {
        let buffer = bytes.bindMemory(to: UInt8.self)
        guard let base = buffer.baseAddress else { return 0 }
        var start = 0
        while start < buffer.count, let newline = memchr(base + start, 10, buffer.count - start) {
            let end = UnsafeRawPointer(newline) - UnsafeRawPointer(base)
            for (index, pattern) in patterns.enumerated()
            where memmem(base + start, end - start, pattern.utf8Start, pattern.utf8CodeUnitCount) != nil {
                body(buffer[start..<end], index)
                break
            }
            start = end + 1
        }
        return start
    }
}

/// What the page shows for one tool.
struct ToolUsage: Equatable, Sendable {
    struct Model: Equatable, Sendable {
        var name: String
        var tokens: Int
        var cost: Double?
    }

    var tokens = 0
    var cost = 0.0
    /// Tokens from models without a price, so the total cost is a lower bound.
    var unpricedTokens = 0
    var models: [Model] = []

    init(_ entries: some Sequence<UsageEntry>) {
        var byModel: [String: Model] = [:]
        for entry in entries {
            tokens += entry.tokens
            let cost = entry.cost
            if let cost { self.cost += cost } else { unpricedTokens += entry.tokens }
            var model = byModel[entry.model] ?? Model(name: entry.model, tokens: 0, cost: cost == nil ? nil : 0)
            model.tokens += entry.tokens
            if let cost { model.cost = (model.cost ?? 0) + cost }
            byModel[entry.model] = model
        }
        models = byModel.values.sorted { $0.tokens != $1.tokens ? $0.tokens > $1.tokens : $0.name < $1.name }
    }
}

struct UsageBlock: Equatable, Sendable {
    var start: Double
    var usage: ToolUsage
    var end: Double { start + UsageLog.blockLength }
}

struct UsageSummary: Equatable, Sendable {
    var claudeToday: ToolUsage
    var codexToday: ToolUsage
    /// Claude Code's five-hour window, estimated the way ccusage does: a block starts on the hour of
    /// its first request and ends five hours later, or after five idle hours.
    var claudeBlock: UsageBlock?
    var codexLimit: RateLimit?

    init(log: UsageLog, now: Double, dayStart: Double) {
        let today = log.entries.filter { $0.time >= dayStart && $0.time <= now }
        claudeToday = ToolUsage(today.lazy.filter { $0.tool == .claude })
        codexToday = ToolUsage(today.lazy.filter { $0.tool == .codex })
        claudeBlock = Self.currentBlock(log.entries.filter { $0.tool == .claude }, now: now)
        codexLimit = log.codexLimit.flatMap { $0.resetsAt > now ? $0 : nil }
    }

    static func currentBlock(_ entries: [UsageEntry], now: Double) -> UsageBlock? {
        var start: Double?
        var last = -Double.infinity
        var members: [UsageEntry] = []
        for entry in entries.sorted(by: { $0.time < $1.time }) where entry.time <= now {
            if start == nil || entry.time >= start! + UsageLog.blockLength || entry.time - last >= UsageLog.blockLength {
                start = (entry.time / 3600).rounded(.down) * 3600
                members = []
            }
            members.append(entry)
            last = entry.time
        }
        guard let start, now < start + UsageLog.blockLength else { return nil }
        return UsageBlock(start: start, usage: ToolUsage(members))
    }
}

/// Bytes from a log line: an array slice in tests, a mapped file in the app.
protocol ByteSlice: RandomAccessCollection where Element == UInt8, Index == Int, SubSequence == Self {}
extension ArraySlice<UInt8>: ByteSlice {}
extension Slice<UnsafeBufferPointer<UInt8>>: ByteSlice {}

/// Finds one field of a JSON object without building the whole object. Log lines can hold large
/// tool output, and only a few numbers are needed from each.
enum JSONScan {
    /// The raw value of `key` among the object's own fields, not those of nested objects.
    static func value<C: ByteSlice>(_ key: String, in object: C) -> C? {
        let key = Array(key.utf8)
        var i = skipSpace(object, object.startIndex)
        guard i < object.endIndex, object[i] == UInt8(ascii: "{") else { return nil }
        i += 1
        while true {
            i = skipSpace(object, i)
            guard i < object.endIndex, object[i] == UInt8(ascii: "\"") , let keyEnd = stringEnd(object, i) else { return nil }
            let name = object[(i + 1)..<keyEnd]
            i = skipSpace(object, keyEnd + 1)
            guard i < object.endIndex, object[i] == UInt8(ascii: ":") else { return nil }
            let start = skipSpace(object, i + 1)
            guard let end = valueEnd(object, start) else { return nil }
            if name.elementsEqual(key) { return object[start..<end] }
            i = skipSpace(object, end)
            guard i < object.endIndex, object[i] == UInt8(ascii: ",") else { return nil }
            i += 1
        }
    }

    /// A string value without escapes resolved: model names, ids and timestamps have none.
    static func string<C: ByteSlice>(_ value: C) -> String? {
        guard value.count >= 2, value.first == UInt8(ascii: "\""), value.last == UInt8(ascii: "\"") else { return nil }
        return String(decoding: value.dropFirst().dropLast(), as: UTF8.self)
    }

    static func number<C: ByteSlice>(_ value: C) -> Double? {
        guard let first = value.first, first == UInt8(ascii: "-") || (first >= 48 && first <= 57) else { return nil }
        return Double(String(decoding: value, as: UTF8.self))
    }

    private static func skipSpace<C: ByteSlice>(_ bytes: C, _ index: Int) -> Int {
        var i = index
        while i < bytes.endIndex, bytes[i] == 32 || bytes[i] == 9 || bytes[i] == 10 || bytes[i] == 13 { i += 1 }
        return i
    }

    /// The index of the closing quote of the string that opens at `index`.
    private static func stringEnd<C: ByteSlice>(_ bytes: C, _ index: Int) -> Int? {
        var i = index + 1
        while i < bytes.endIndex {
            if bytes[i] == UInt8(ascii: "\\") { i += 2; continue }
            if bytes[i] == UInt8(ascii: "\"") { return i }
            i += 1
        }
        return nil
    }

    /// One past the end of the value that starts at `index`.
    static func valueEnd<C: ByteSlice>(_ bytes: C, _ index: Int) -> Int? {
        guard index < bytes.endIndex else { return nil }
        let open = bytes[index]
        if open == UInt8(ascii: "\"") { return stringEnd(bytes, index).map { $0 + 1 } }
        if open == UInt8(ascii: "{") || open == UInt8(ascii: "[") {
            var depth = 0
            var i = index
            while i < bytes.endIndex {
                switch bytes[i] {
                case UInt8(ascii: "\""):
                    guard let end = stringEnd(bytes, i) else { return nil }
                    i = end
                case UInt8(ascii: "{"), UInt8(ascii: "["):
                    depth += 1
                case UInt8(ascii: "}"), UInt8(ascii: "]"):
                    depth -= 1
                    if depth == 0 { return i + 1 }
                default:
                    break
                }
                i += 1
            }
            return nil
        }
        var i = index
        while i < bytes.endIndex, bytes[i] != UInt8(ascii: ","), bytes[i] != UInt8(ascii: "}"), bytes[i] != UInt8(ascii: "]"),
              bytes[i] != 32, bytes[i] != 10, bytes[i] != 13, bytes[i] != 9 {
            i += 1
        }
        return i
    }
}
