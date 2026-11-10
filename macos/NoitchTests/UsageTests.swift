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

    @Test func `numbers read the way the page shows them`() {
        #expect(UsageModel.tokens(512) == "512")
        #expect(UsageModel.duration(3 * 86400 + 4 * 3600) == "3d 4h")
        #expect(UsageModel.tokens(12_000) == "12K")
        #expect(UsageModel.tokens(12_430_000) == "12.4M")
        #expect(UsageModel.dollars(8.214) == "$8.21")
        #expect(UsageModel.duration(2 * 3600 + 13 * 60) == "2h 13m")
        #expect(UsageModel.duration(30) == "1m")
    }

    @Test func `unpriced models make the cost a lower bound`() {
        var known = UsageEntry(tool: .codex, time: 0, model: "gpt-5")
        known.output = 1_000_000
        var unknown = UsageEntry(tool: .codex, time: 0, model: "gpt-6.1-sol")
        unknown.output = 10
        #expect(UsageModel.cost(ToolUsage([known, unknown])) == "$10.00+")
        #expect(UsageModel.cost(ToolUsage([unknown])) == "—")
    }

    @Test func `codex counts each turn once and keeps its limits`() {
        var log = UsageLog()
        var state = CodexFileState()
        let context = #"{"timestamp":"2026-11-10T01:00:00Z","type":"turn_context","payload":{"model":"gpt-5-codex"}}"#
        func count(_ total: Int, time: String) -> String {
            #"{"timestamp":"\#(time)","type":"event_msg","payload":{"type":"token_count","info":{"total_token_usage":{"total_tokens":\#(total)},"last_token_usage":{"input_tokens":1000,"cached_input_tokens":600,"output_tokens":50,"total_tokens":1050}},"rate_limits":{"primary":{"used_percent":32.0,"window_minutes":300,"resets_at":2000000000},"secondary":{"used_percent":6.0,"window_minutes":10080,"resets_at":2000300000}}}}"#
        }
        let text = [context, count(1050, time: "2026-11-10T01:00:01Z"), count(1050, time: "2026-11-10T01:00:02Z"),
                    count(2100, time: "2026-11-10T01:00:03Z")].joined(separator: "\n") + "\n"
        _ = Self.readCodex(text, into: &log, state: &state)
        #expect(log.entries.count == 2)
        #expect(log.entries[0].model == "gpt-5-codex")
        #expect(log.entries[0].input == 400 && log.entries[0].cacheRead == 600 && log.entries[0].output == 50)
        #expect(log.codexSamples.count == 3)
        #expect(log.codexSamples.last?.fiveHour == LimitWindow(usedPercent: 32, resetsAt: 2_000_000_000, length: 18000))
        #expect(log.codexSamples.last?.week?.length == 604_800)
    }

    @Test func `pace compares use with the time gone`() {
        let window = LimitWindow(usedPercent: 60, resetsAt: 10_000 + 3.5 * 3600, length: LimitWindow.fiveHours)
        // 1.5 of 5 hours gone is 30%: using 60% is ahead.
        #expect(Pace(window, at: 10_000) == .ahead)
        #expect(Pace(LimitWindow(usedPercent: 30, resetsAt: window.resetsAt, length: window.length), at: 10_000) == .even)
        #expect(Pace(LimitWindow(usedPercent: 5, resetsAt: window.resetsAt, length: window.length), at: 10_000) == .under)
        #expect(window.current(at: window.resetsAt! + 1).usedPercent == 0)
    }

    @Test func `a fast pace warns before the five hour reset`() {
        let now = 100_000.0
        let five = LimitWindow(usedPercent: 70, resetsAt: now + 2 * 3600, length: LimitWindow.fiveHours)
        // 1% a minute leaves 30 minutes, long before the reset in two hours.
        let insight = LimitInsight(ToolLimits(fiveHour: five, week: nil, recentRate: 1.0 / 60), at: now)
        #expect(insight == .runsOut(window: LimitWindow.fiveHours, in: 1800, resetsIn: 7200))
        #expect(insight.severity == .watch)
        let calm = LimitInsight(ToolLimits(fiveHour: five, week: nil, recentRate: 0), at: now)
        #expect(calm == .onTrack(projected: 70))
        let reached = LimitInsight(ToolLimits(fiveHour: LimitWindow(usedPercent: 100, resetsAt: now + 60, length: LimitWindow.fiveHours)), at: now)
        #expect(reached.severity == .critical)
        #expect(LimitInsight(.failed(.expired), at: now) == .problem(.expired))
    }

    @Test func `the week warns when its average pace runs out early`() {
        let now = 1_000_000.0
        // Two days into the week and 60% used: at that average it runs out on day 3.3.
        let week = LimitWindow(usedPercent: 60, resetsAt: now + 5 * 86400, length: LimitWindow.week)
        guard case let .runsOut(length, time, _) = LimitInsight(ToolLimits(fiveHour: nil, week: week), at: now) else {
            Issue.record("expected a warning")
            return
        }
        #expect(length == LimitWindow.week)
        #expect(abs(time - 40.0 * 2 * 86400 / 60) < 1)
    }

    @Test func `codex pace comes from its own percentages`() {
        let resets = 50_000.0
        func sample(_ time: Double, _ used: Double) -> LimitSample {
            LimitSample(time: time, fiveHour: LimitWindow(usedPercent: used, resetsAt: resets, length: LimitWindow.fiveHours), week: nil)
        }
        let now = 40_000.0
        let samples = [sample(now - 2400, 10), sample(now - 600, 25), sample(now - 60, 28)]
        let rate = LimitRates.codex(samples, now: now)
        #expect(rate.map { abs($0 - 18.0 / 1800) < 1e-9 } == true)
    }

    @Test func `claude pace is its share of the window's recent cost`() throws {
        let now = 100_000.0
        let window = LimitWindow(usedPercent: 40, resetsAt: now + 3 * 3600, length: LimitWindow.fiveHours)
        func entry(_ time: Double) -> UsageEntry {
            var entry = UsageEntry(tool: .claude, time: time, model: "claude-opus-5-5")
            entry.output = 100_000
            return entry
        }
        // Half the window's cost came in the last half hour, so half of 40% did.
        let rate = try #require(LimitRates.claude([entry(now - 3600), entry(now - 600)], window: window, now: now))
        #expect(abs(rate - 20.0 / 1800) < 1e-9)
    }

    @Test func `the usage response and stored login parse`() throws {
        let response = #"{"five_hour":{"utilization":72.0,"resets_at":"2026-11-10T15:00:00.123456+00:00"},"seven_day":{"utilization":41,"resets_at":null},"seven_day_opus":null}"#
        let limits = try #require(ClaudeUsageFormat.limits(fromResponse: Array(response.utf8)[...]))
        #expect(limits.fiveHour?.usedPercent == 72)
        #expect(limits.fiveHour?.resetsAt.map { abs($0 - 1_794_322_800.123456) < 1e-3 } == true)
        #expect(limits.week == LimitWindow(usedPercent: 41, resetsAt: nil, length: LimitWindow.week))
        #expect(ClaudeUsageFormat.limits(fromResponse: Array("{}".utf8)[...]) == nil)
        let credentials = #"{"claudeAiOauth":{"accessToken":"token-value","refreshToken":"r","expiresAt":1794326400000}}"#
        let login = try #require(ClaudeUsageFormat.login(fromCredentials: Array(credentials.utf8)[...]))
        #expect(login.accessToken == "token-value" && login.expiresAt == 1_794_326_400)
    }

    @Test func `the last good limits survive a round trip`() {
        let limits = ToolLimits(fiveHour: LimitWindow(usedPercent: 72.5, resetsAt: 1_794_330_000, length: LimitWindow.fiveHours),
                                week: LimitWindow(usedPercent: 41, resetsAt: nil, length: LimitWindow.week))
        let text = LimitCache.encode("claude", limits, asOf: 1_794_322_800) + "\n" + LimitCache.encode("codex", ToolLimits(week: limits.week), asOf: 5)
        let claude = LimitCache.decode(text, tool: "claude")
        #expect(claude?.fiveHour == limits.fiveHour && claude?.week == limits.week && claude?.asOf == 1_794_322_800)
        #expect(LimitCache.decode(text, tool: "codex")?.fiveHour == nil)
        #expect(LimitCache.decode("claude nonsense", tool: "claude") == nil)
        let model = UsageModel(claude: claude!, codex: .failed(.noData), summary: UsageSummary(log: UsageLog(), now: 0, dayStart: 0), now: 1_794_323_000) { _ in "Tue 14:00" }
        #expect(model.claude.today == "As of Tue 14:00")
        // An expired login keeps the remembered rings but says what to do.
        var expired = claude!
        expired.problem = .expired
        let stale = UsageModel(claude: expired, codex: .failed(.noData), summary: UsageSummary(log: UsageLog(), now: 0, dayStart: 0), now: 1_794_323_000) { _ in "Tue 14:00" }
        #expect(stale.claude.inner != nil && stale.claude.center == "73%" && stale.claude.today == "As of Tue 14:00")
        #expect(stale.claude.headline == "Login expired" && stale.claude.detail == "Run claude in a terminal")
    }

    @Test func `the page says what matters in a few words`() {
        let now = 100_000.0
        let five = LimitWindow(usedPercent: 70, resetsAt: now + 2 * 3600, length: LimitWindow.fiveHours)
        let week = LimitWindow(usedPercent: 20, resetsAt: now + 4 * 86400, length: LimitWindow.week)
        var log = UsageLog()
        _ = Self.read(Self.claudeLine(id: "msg_1", time: "1970-01-02T03:00:00Z") + "\n", into: &log)
        let model = UsageModel(claude: ToolLimits(fiveHour: five, week: week, recentRate: 1.0 / 60), codex: .failed(.noData),
                               summary: UsageSummary(log: log, now: now, dayStart: 0), now: now) { _ in "Thu 14:00" }
        #expect(model.claude.center == "70%" && model.claude.caption == "5h")
        #expect(model.claude.headline == "5h runs out in ~30m")
        #expect(model.claude.detail == "1h 30m before its reset")
        #expect(model.claude.rows[1].reset == "Thu 14:00")
        #expect(model.claude.inner?.fraction == 0.7)
        #expect(model.codex.headline == "No limits yet" && model.codex.center == "—")
    }

    @Test(arguments: [CGFloat(24), 32, 38])
    func `the usage page fits under the header`(headerHeight: CGFloat) {
        let layout = UsageLayout(size: IslandGeometry.expandedSize, headerHeight: headerHeight)
        let bottom = IslandGeometry.expandedSize.height - 8
        #expect(layout.detail(1).maxY <= bottom && layout.gauge(1).maxY <= bottom)
        #expect(layout.gauge(0).minY >= headerHeight)
        #expect(layout.rowReset(0, 0).maxX <= layout.gauge(1).minX)
        #expect(layout.detail(1).maxX <= IslandGeometry.expandedSize.width - UsageLayout.sidePadding)
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
        #expect(IslandPage.home.neighbor(-1, in: IslandPage.allCases) == nil)
        #expect(IslandPage.home.neighbor(1, in: IslandPage.allCases) == .usage)
        #expect(IslandPage.home.neighbor(1, in: [.home]) == nil)
    }

    @Test func `the usage settings pick the columns and where amber starts`() {
        var settings = UsageSettings()
        #expect(settings.tools == [.claude, .codex])
        settings.showClaude = false
        #expect(settings.tools == [.codex])
        settings.showCodex = false
        #expect(settings.tools == [.claude])
        let single = UsageLayout(size: IslandGeometry.expandedSize, headerHeight: 32, columns: 1)
        #expect(abs(single.gauge(0).minX + single.columnWidth / 2 - IslandGeometry.expandedSize.width / 2) < 0.5)
        let limits = ToolLimits(fiveHour: LimitWindow(usedPercent: 65, resetsAt: nil, length: LimitWindow.fiveHours))
        let summary = UsageSummary(log: UsageLog(), now: 0, dayStart: 0)
        settings.warnAt = 60
        let warned = UsageModel(claude: limits, codex: .failed(.noData), summary: summary, now: 0, settings: settings) { _ in "" }
        let calm = UsageModel(claude: limits, codex: .failed(.noData), summary: summary, now: 0) { _ in "" }
        #expect(warned.claude.rows[0].severity == .watch && calm.claude.rows[0].severity == .calm)
    }
}
