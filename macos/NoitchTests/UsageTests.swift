import CoreGraphics
import Foundation
import Testing
@testable import Noitch

struct UsageTests {
    private static func claudeLine(id: String, request: String = "req_1", time: String, model: String = "claude-opus-5-5",
                                   input: Int = 10, output: Int = 20, read: Int = 100, written: Int = 40, hour: Int = 30) -> String {
        """
        {"parentUuid":"x","message":{"model":"\(model)","id":"\(id)","content":[{"type":"text","text":"a \\"usage\\" {\\"input_tokens\\":999}"}],\
        "usage":{"input_tokens":\(input),"cache_creation_input_tokens":\(written),"cache_read_input_tokens":\(read),"output_tokens":\(output),\
        "cache_creation":{"ephemeral_1h_input_tokens":\(hour),"ephemeral_5m_input_tokens":\(written - hour)},\
        "iterations":[{"input_tokens":5,"output_tokens":5}]}},"requestId":"\(request)","type":"assistant","timestamp":"\(time)"}
        """
    }

    private static func read(_ text: String, into log: inout UsageLog, since cutoff: Double = 0) -> Int {
        Array(text.utf8).withUnsafeBytes { log.readClaude($0, since: cutoff) }
    }

    private static func readCodex(_ text: String, into log: inout UsageLog, state: inout CodexFileState) -> Int {
        Array(text.utf8).withUnsafeBytes { log.readCodex($0, state: &state, since: 0) }
    }

    @Test func `times parse in UTC and with an offset`() {
        #expect(UsageLog.parseTime("1970-01-01T00:00:00Z") == 0)
        #expect(UsageLog.parseTime("2026-11-10T08:26:36.5Z") == 1_794_299_196.5)
        #expect(UsageLog.parseTime("2026-11-10T09:26:36+01:00") == 1_794_299_196)
        #expect(UsageLog.parseTime("yesterday") == nil)
    }

    @Test func `the scanner reads only the object's own fields`() {
        let json = Array(#"{"a":{"input_tokens":1},"text":"\"input_tokens\":2","input_tokens":3}"#.utf8)[...]
        #expect(JSONScan.value("input_tokens", in: json).flatMap(JSONScan.number) == 3)
        #expect(JSONScan.value("missing", in: json) == nil)
    }

    @Test func `claude lines give one entry per request`() {
        var log = UsageLog()
        let text = Self.claudeLine(id: "msg_1", time: "2026-11-10T08:00:00Z") + "\n"
            + Self.claudeLine(id: "msg_1", time: "2026-11-10T08:00:01Z") + "\n"
            + Self.claudeLine(id: "msg_2", time: "2026-11-10T08:01:00Z") + "\n"
        #expect(Self.read(text, into: &log) == text.utf8.count)
        #expect(log.entries.count == 2)
        let entry = log.entries[0]
        #expect(entry.input == 10 && entry.output == 20 && entry.cacheRead == 100)
        #expect(entry.cacheWrite1h == 30 && entry.cacheWrite5m == 10)
        #expect(entry.tokens == 170)
    }

    @Test func `a half-written last line waits for the next read`() {
        var log = UsageLog()
        let whole = Self.claudeLine(id: "msg_1", time: "2026-11-10T08:00:00Z") + "\n"
        let partial = String(Self.claudeLine(id: "msg_2", time: "2026-11-10T08:00:00Z").prefix(40))
        #expect(Self.read(whole + partial, into: &log) == whole.utf8.count)
        #expect(log.entries.count == 1)
    }

    @Test func `lines before the cutoff are skipped`() {
        var log = UsageLog()
        _ = Self.read(Self.claudeLine(id: "msg_1", time: "1970-01-01T00:00:10Z") + "\n", into: &log, since: 100)
        #expect(log.entries.isEmpty)
    }

    @Test func `opus 5.5 is priced from the list`() throws {
        var entry = UsageEntry(tool: .claude, time: 0, model: "claude-opus-5-5")
        entry.input = 1_000_000
        entry.output = 1_000_000
        entry.cacheRead = 1_000_000
        entry.cacheWrite5m = 1_000_000
        entry.cacheWrite1h = 1_000_000
        let cost = try #require(entry.cost)
        #expect(abs(cost - (4 + 20 + 0.2 + 5 + 8)) < 1e-9)
        #expect(ModelPrice.find("claude-opus-5")?.input == 5)
        #expect(UsageEntry(tool: .codex, time: 0, model: "gpt-6.1-sol").cost == nil)
    }

    @Test func `codex counts each turn once and splits cached input`() {
        var log = UsageLog()
        var state = CodexFileState()
        let context = #"{"timestamp":"2026-11-10T01:00:00Z","type":"turn_context","payload":{"model":"gpt-5-codex"}}"#
        func count(_ total: Int, time: String) -> String {
            #"{"timestamp":"\#(time)","type":"event_msg","payload":{"type":"token_count","info":{"total_token_usage":{"total_tokens":\#(total)},"last_token_usage":{"input_tokens":1000,"cached_input_tokens":600,"output_tokens":50,"total_tokens":1050}},"rate_limits":{"primary":{"used_percent":32.0,"window_minutes":300,"resets_at":2000000000}}}}"#
        }
        let text = [context, count(1050, time: "2026-11-10T01:00:01Z"), count(1050, time: "2026-11-10T01:00:02Z"),
                    count(2100, time: "2026-11-10T01:00:03Z")].joined(separator: "\n") + "\n"
        _ = Self.readCodex(text, into: &log, state: &state)
        #expect(log.entries.count == 2)
        #expect(log.entries[0].model == "gpt-5-codex")
        #expect(log.entries[0].input == 400 && log.entries[0].cacheRead == 600 && log.entries[0].output == 50)
        #expect(log.codexLimit == RateLimit(usedPercent: 32, resetsAt: 2_000_000_000))
        #expect(state.lastTotal == 2100)
    }

    @Test func `a block starts on the hour and ends five hours later`() throws {
        let hour = 3600.0
        func entry(_ time: Double) -> UsageEntry {
            var entry = UsageEntry(tool: .claude, time: time, model: "claude-opus-5-5")
            entry.output = 1
            return entry
        }
        let block = try #require(UsageSummary.currentBlock([entry(10 * hour + 900), entry(11 * hour)], now: 12 * hour))
        #expect(block.start == 10 * hour)
        #expect(block.end == 15 * hour)
        #expect(block.usage.tokens == 2)
        // Past its end, a new request starts a new block on its own hour.
        let next = try #require(UsageSummary.currentBlock([entry(10 * hour), entry(15 * hour + 60)], now: 16 * hour))
        #expect(next.start == 15 * hour && next.usage.tokens == 1)
        #expect(UsageSummary.currentBlock([entry(hour)], now: 7 * hour) == nil)
    }

    @Test func `numbers read the way the page shows them`() {
        #expect(UsageModel.tokens(512) == "512")
        #expect(UsageModel.tokens(12_000) == "12K")
        #expect(UsageModel.tokens(12_430_000) == "12.4M")
        #expect(UsageModel.dollars(8.214) == "$8.21")
        #expect(UsageModel.duration(2 * 3600 + 13 * 60) == "2h 13m")
        #expect(UsageModel.duration(30) == "1m")
        #expect(UsageModel.displayName("claude-opus-5-5") == "Opus 5.5")
        #expect(UsageModel.displayName("claude-sonnet-4-5-20250929") == "Sonnet 4.5")
        #expect(UsageModel.displayName("gpt-5-codex") == "gpt-5-codex")
    }

    @Test func `unpriced models make the cost a lower bound`() {
        var known = UsageEntry(tool: .codex, time: 0, model: "gpt-5")
        known.output = 1_000_000
        var unknown = UsageEntry(tool: .codex, time: 0, model: "gpt-6.1-sol")
        unknown.output = 10
        #expect(UsageModel.cost(ToolUsage([known, unknown])) == "$10.00+")
        #expect(UsageModel.cost(ToolUsage([unknown])) == "—")
    }

    @Test(arguments: [CGFloat(24), 32, 38])
    func `the usage page fits under the header`(headerHeight: CGFloat) {
        let layout = UsageLayout(size: IslandGeometry.expandedSize, headerHeight: headerHeight)
        let last = layout.row(1, UsageModel.maxRows - 1)
        #expect(last.maxY <= IslandGeometry.expandedSize.height - 12)
        #expect(layout.title(0).maxX < layout.title(1).minX)
        #expect(layout.title(1).maxX <= IslandGeometry.expandedSize.width - UsageLayout.sidePadding)
    }

    @Test func `header controls stay clear of Home's controls`() {
        let size = IslandGeometry.expandedSize
        let header = HeaderLayout(size: size, headerHeight: 32)
        let home = HomeLayout(size: size, headerHeight: 32)
        for control in HomeControl.allCases {
            for page in IslandPage.allCases {
                #expect(!header.frame(of: .tab(page)).intersects(home.frame(of: control)))
            }
            #expect(!header.frame(of: .settings).intersects(home.frame(of: control)))
        }
        #expect(header.control(at: CGPoint(x: header.frame(of: .tab(.usage)).midX, y: 16)) == .tab(.usage))
    }

    @Test func `a swipe steps one page however long it runs`() {
        var swipe = PageSwipe()
        #expect(swipe.add(20, at: 0) == 0)
        #expect(swipe.add(25, at: 0.02) == 1)
        #expect(swipe.add(200, at: 0.04) == 0)
        // A pause starts a new gesture.
        #expect(swipe.add(-50, at: 0.5) == -1)
        #expect(IslandPage.home.neighbor(-1) == nil)
        #expect(IslandPage.home.neighbor(1) == .usage)
    }
}
