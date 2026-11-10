// Covers the pure layer. Plain asserts: this runs anywhere with a C++20 compiler, including on macOS.
#undef NDEBUG
#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>

#include "features/home/home_layout.h"
#include "features/home/home_model.h"
#include "features/lyrics/lyrics.h"
#include "features/now_playing/now_playing.h"
#include "features/usage/ai_usage.h"
#include "features/usage/usage_layout.h"
#include "features/usage/usage_model.h"
#include "island/flare.h"
#include "island/island_geometry.h"
#include "island/island_page.h"
#include "island/spring.h"

using namespace noitch;

namespace {

bool Near(double a, double b, double tolerance) { return std::fabs(a - b) <= tolerance; }

double Evaluate(const CubicSegment& s, double t) {
    return s.c0 + t * (s.c1 + t * (s.c2 + t * s.c3));
}

void TestPillStaysCenteredAndInsideTheWindow() {
    for (float scale : {1.0f, 1.25f, 1.5f, 2.0f, 3.5f}) {
        const IslandGeometry geometry(scale);
        const WindowSize window = geometry.Window();
        for (IslandState state : {IslandState::Collapsed, IslandState::Expanded}) {
            const Pill pill = geometry.PillFor(state);
            assert(Near((pill.left + pill.right) / 2, window.width / 2.0, 0.01));
            assert(pill.left >= 0 && pill.right <= window.width);
            assert(pill.bottom <= window.height);
        }
    }
}

void TestSpringStartsWhereItWasAskedTo() {
    const ScalarSpring spring(10, 250, 100);
    assert(Near(spring.Value(0), 10, 1e-9));
    assert(Near(spring.Velocity(0), 250, 1e-9));
}

void TestSpringSettlesOnTarget() {
    const ScalarSpring spring(0, 0, 300);
    const double settle = spring.SettleTime(0.25);
    assert(settle > 0.2 && settle < 1.5);
    assert(Near(spring.Value(settle), 300, 0.25));
    assert(Near(spring.Value(settle * 4), 300, 1e-3));
}

void TestSpringOvershootFitsTheWindowMargin() {
    for (const SpringParameters parameters : {kExpandSpring, kCollapseSpring}) {
        const ScalarSpring spring(0, 0, 130, parameters);  // The largest half-width travel, at scale 1.
        double peak = 0;
        for (double t = 0; t < 1.5; t += 0.001) peak = std::fmax(peak, spring.Value(t));
        assert(peak > 130);
        assert(peak - 130 < 16);
    }
}

void TestOpeningIsSlowerThanClosing() {
    const ScalarSpring open(0, 0, 300, kExpandSpring);
    const ScalarSpring close(0, 0, 300, kCollapseSpring);
    assert(open.SettleTime(0.25) > close.SettleTime(0.25));
    assert(open.Value(0.1) < close.Value(0.1));
}

void TestContentFadeCurveMatchesItsValues() {
    const ContentFade fade{2.0, 0.06, 0.3, -16, 0};
    assert(Near(fade.ValueAt(1.0), -16, 1e-9));
    assert(Near(fade.ValueAt(2.06), -16, 1e-9));
    assert(Near(fade.ValueAt(fade.End()), 0, 1e-9));
    const CubicSegment s = fade.Curve();
    assert(Near(s.offset, 0.06, 1e-9));
    for (double t = 0; t <= 0.3; t += 0.01) {
        const double cubic = s.c0 + s.c1 * t + s.c2 * t * t + s.c3 * t * t * t;
        assert(Near(cubic, fade.ValueAt(2.06 + t), 1e-3));
    }
}

void TestVelocityIsTheDerivativeOfValue() {
    const ScalarSpring spring(5, -40, 220);
    for (double t = 0.01; t < 0.8; t += 0.05) {
        const double h = 1e-6;
        const double numeric = (spring.Value(t + h) - spring.Value(t - h)) / (2 * h);
        assert(Near(spring.Velocity(t), numeric, 1e-3));
    }
}

void TestCubicSegmentsTrackTheSpring() {
    const IslandGeometry geometry(2.0f);
    PillMotion motion;
    motion.Jump(geometry.PillFor(IslandState::Collapsed));
    motion.Retarget(geometry.PillFor(IslandState::Expanded), 0, kExpandSpring);

    const int count = motion.SegmentCount();
    assert(count > 0);
    const double width = motion.Duration() / count;
    assert(width <= PillMotion::kMaxSegmentSeconds);

    double worst = 0;
    for (int channel = 0; channel < PillMotion::kChannels; ++channel) {
        for (int i = 0; i < count; ++i) {
            const CubicSegment segment = motion.Segment(channel, i);
            assert(Near(segment.offset, i * width, 1e-12));
            for (int step = 0; step <= 8; ++step) {
                const double t = width * step / 8;
                worst = std::fmax(worst, std::fabs(Evaluate(segment, t) - motion.Channel(channel).Value(segment.offset + t)));
            }
        }
    }
    assert(worst < 0.02);  // A small fraction of a pixel, at 200% scale.
}

void TestEverySegmentIsAcceptedByDirectComposition() {
    // AddCubic fails on non-finite coefficients and on offsets that do not strictly increase.
    for (double duration : {0.0167, 0.5, 0.5 + 1e-13, 1.0 / 60 * 31}) {
        const ScalarSpring spring(0, 0, 300);
        const int count = static_cast<int>(std::ceil(duration / PillMotion::kMaxSegmentSeconds));
        double previous = -1;
        for (int i = 0; i < count; ++i) {
            const double width = duration / count;
            const CubicSegment s = spring.Segment(i * width, (i + 1) * width);
            assert(s.offset > previous);
            assert(std::isfinite(s.c0) && std::isfinite(s.c1) && std::isfinite(s.c2) && std::isfinite(s.c3));
            previous = s.offset;
        }
    }
}

void TestRetargetContinuesFromCurrentMotion() {
    const IslandGeometry geometry;
    PillMotion motion;
    motion.Jump(geometry.PillFor(IslandState::Collapsed));
    motion.Retarget(geometry.PillFor(IslandState::Expanded), 10.0, kExpandSpring);

    const Pill before = motion.ValueAt(10.1);
    const double velocity = motion.Channel(2).Velocity(0.1);
    motion.Retarget(geometry.PillFor(IslandState::Collapsed), 10.1, kCollapseSpring);
    const Pill after = motion.ValueAt(10.1);

    assert(Near(before.left, after.left, 1e-3));
    assert(Near(before.right, after.right, 1e-3));
    assert(Near(before.bottom, after.bottom, 1e-3));
    assert(Near(before.radius, after.radius, 1e-3));
    assert(Near(motion.Channel(2).Velocity(0), velocity, 1e-6));
}

void TestFlaresFitBesideTheExpandedPill() {
    for (float scale : {1.0f, 1.25f, 1.5f, 2.0f, 3.5f}) {
        const IslandGeometry geometry(scale);
        const Pill expanded = geometry.PillFor(IslandState::Expanded);
        assert(expanded.left - geometry.FlareRadius() >= 0);
        assert(expanded.right + geometry.FlareRadius() <= geometry.Window().width);
    }
}

void TestFlareTapersFromThePillToTheTopEdge() {
    const int radius = 16;
    assert(FlareCoverage(radius, radius - 1, 0) == 1.0f);   // Against the pill, at the top
    assert(FlareCoverage(radius, 0, radius - 1) == 0.0f);   // Far corner: the removed quarter circle
    float previous = 0;
    bool sawPartialPixel = false;
    for (int x = 0; x < radius; ++x) {                      // Along the top edge, toward the pill
        const float coverage = FlareCoverage(radius, x, 0);
        assert(coverage >= previous);
        sawPartialPixel = sawPartialPixel || (coverage > 0 && coverage < 1);
        previous = coverage;
    }
    assert(sawPartialPixel);                                // Antialiased, not a hard step
    for (int y = 0; y < radius; ++y) assert(FlareCoverage(radius, radius, y) == 1.0f);  // Overlap column
}

void TestFlarePixelsArePremultipliedBlackAndMirror() {
    constexpr int radius = 12;
    constexpr int width = radius + kFlareOverlapPx;
    std::uint32_t left[width * radius];
    std::uint32_t right[width * radius];
    FillFlarePixels(radius, false, left);
    FillFlarePixels(radius, true, right);
    for (int y = 0; y < radius; ++y) {
        for (int x = 0; x < width; ++x) {
            assert((left[y * width + x] & 0x00FFFFFF) == 0);
            assert(left[y * width + x] == right[y * width + (width - 1 - x)]);
        }
    }
}

void TestJumpLeavesNothingToAnimate() {
    PillMotion motion;
    motion.Jump(IslandGeometry().PillFor(IslandState::Expanded));
    assert(motion.Duration() == 0);
}

void TestClockRunsFromTheReportedPositionAndStopsAtTheEnd() {
    NowPlaying state;
    state.isPlaying = true;
    state.duration = 100;
    state.elapsed = 10;
    state.timestamp = 50;
    assert(Near(state.Position(55), 15, 1e-9));
    assert(Near(state.Position(500), 100, 1e-9));
    state.isPlaying = false;
    assert(Near(state.Position(500), 10, 1e-9));
}

void TestRepeatCyclesThroughEveryMode() {
    assert(NextRepeatMode(RepeatMode::Off) == RepeatMode::All);
    assert(NextRepeatMode(RepeatMode::All) == RepeatMode::One);
    assert(NextRepeatMode(RepeatMode::One) == RepeatMode::Off);
}

void TestLrcParsesTimestampsSkipsMetadataAndSorts() {
    const auto lines = ParseLrc("[ar:Someone]\n[00:12.50]second\r\n[00:02.00]first\n[01:00.00][00:30.00]chorus\n");
    assert(lines.size() == 4);
    assert(lines[0].text == "first" && Near(lines[0].start, 2, 1e-9));
    assert(lines[1].text == "second" && Near(lines[1].start, 12.5, 1e-9));
    assert(lines[2].text == "chorus" && Near(lines[2].start, 30, 1e-9));
    assert(lines[3].text == "chorus" && Near(lines[3].start, 60, 1e-9));
}

void TestLyricsFindTheCurrentAndNextLine() {
    Lyrics lyrics;
    lyrics.synced = {{5, "a"}, {10, "b"}};
    assert(lyrics.LineIndex(1) == -1);
    assert(lyrics.LineIndex(5) == 0);
    assert(lyrics.LineIndex(100) == 1);
    assert(Near(lyrics.NextChange(0), 5, 1e-9));
    assert(Near(lyrics.NextChange(7), 10, 1e-9));
    assert(lyrics.NextChange(11) < 0);
}

void TestTitlesLoseWhatPlayersAdd() {
    assert(CleanTitle("Song (feat. Someone)") == "Song");
    assert(CleanTitle("Song - Remastered 2011") == "Song");
    assert(CleanTitle("Song [Official Video]") == "Song");
    assert(CleanTitle("Song (Part 2)") == "Song (Part 2)");
    assert(CleanTitle("Love - Hate") == "Love - Hate");
}

void TestLyricsMatchingPrefersSyncedThenClosestLength() {
    LyricsRecord plain{200, false, "plain", ""};
    LyricsRecord far{210, false, "", "[00:01.00]far"};
    LyricsRecord near{201, false, "", "[00:01.00]near"};
    LyricsRecord other{300, false, "", "[00:01.00]other"};
    const auto best = BestLyricsRecord({plain, far, near, other}, 200);
    assert(best && best->syncedLyrics == near.syncedLyrics);
    assert(!BestLyricsRecord({other}, 200));
}

void TestJsonReadsOneRecordOrAnArray() {
    const auto one = ParseLyricsRecords(R"({"id":1,"nested":{"a":[1,2,{"b":null}]},"duration":12.5,"instrumental":false,"plainLyrics":null,"syncedLyrics":"[00:01.00]hi\nthere"})");
    assert(one.size() == 1);
    assert(Near(one[0].duration, 12.5, 1e-9));
    assert(one[0].syncedLyrics == "[00:01.00]hi\nthere");
    const auto many = ParseLyricsRecords(R"([{"instrumental":true},{"plainLyrics":"caf\u00e9 \ud83c\udfb5"}])");
    assert(many.size() == 2);
    assert(many[0].instrumental);
    assert(many[1].plainLyrics == "caf\xC3\xA9 \xF0\x9F\x8E\xB5");
    assert(ParseLyricsRecords("[]").empty());
    assert(ParseLyricsRecords("not json").empty());
}

void TestPercentEncoding() {
    assert(PercentEncode("a b&c/\xC3\xA9-_.~") == "a%20b%26c%2F%C3%A9-_.~");
}

void TestHomeControlsDoNotOverlapAndStayInsideThePill() {
    for (float scale : {1.0f, 1.5f, 2.0f}) {
        const HomeLayout layout(IslandGeometry::ExpandedWidth(), IslandGeometry::ExpandedHeight(), IslandGeometry::CollapsedHeight(), scale);
        for (const HomeControl first : kHomeControls) {
            const HomeRect frame = layout.Frame(first);
            assert(frame.x >= 0 && frame.y >= 0 && frame.Right() <= layout.Width() && frame.Bottom() <= layout.Height());
            for (const HomeControl second : kHomeControls) {
                assert(first == second || !frame.Intersects(layout.Frame(second)));
            }
        }
    }
}

void TestHomePointsFindTheirControl() {
    const HomeLayout layout(580, 180, 32, 1);
    const HomeRect play = layout.Frame(HomeControl::PlayPause);
    assert(layout.ControlAt(play.x + 5, play.y + 5) == HomeControl::PlayPause);
    assert(layout.ControlAt(1, 1) == HomeControl::None);
    const HomeRect track = layout.SliderTrack();
    assert(Near(layout.SliderFraction(track.x), 0, 1e-6));
    assert(Near(layout.SliderFraction(track.x + track.width / 2), 0.5, 1e-6));
    assert(Near(layout.SliderFraction(track.Right() + 50), 1, 1e-6));
}

void TestFormatTime() {
    assert(FormatTime(0) == "0:00");
    assert(FormatTime(65) == "1:05");
    assert(FormatTime(3725) == "1:02:05");
}

void TestNextTickIsTheNextSecondOrAnEarlierLyric() {
    assert(Near(NextHomeTick(10.25, 1, -1), 0.75, 1e-6));
    assert(Near(NextHomeTick(10.25, 1, 10.5), 0.25, 1e-6));
    assert(Near(NextHomeTick(10.5, 2, -1), 0.25, 1e-6));
}

void TestHomeModelShowsThePlaceholderAndFollowsLyrics() {
    const AppSettings settings;
    assert(!MakeHomeModel({}, nullptr, false, 0, false, settings).hasTrack);

    NowPlaying nowPlaying;
    nowPlaying.title = "Song";
    nowPlaying.duration = 100;
    Lyrics lyrics;
    lyrics.synced = {{1, "one"}, {2, "two"}, {3, "three"}};
    const HomeModel model = MakeHomeModel(nowPlaying, &lyrics, false, 2.5, true, settings);
    assert(model.lyricLine == "two");
    assert(model.panelLines[0] == "one" && model.panelLines[2] == "three");
    assert(model.remainingText == "-1:37");
    assert(Near(model.progress, 0.025, 1e-9));

    AppSettings off;
    off.showLyrics = false;
    const HomeModel hidden = MakeHomeModel(nowPlaying, &lyrics, false, 2.5, true, off);
    assert(hidden.lyricLine.empty() && !hidden.showsLyricsPanel && !hidden.showsLyricsButton);
}

void TestArtworkTintIsAverageAndLifted() {
    const uint32_t dark[2] = {0xFF000010, 0xFF000030};
    assert((ArtworkTint(dark, 2) & 0xFF) == 191);
    const uint32_t bright[1] = {0xFFFF0000};
    assert(ArtworkTint(bright, 1) == 0xFFFF0000);
    assert(ArtworkTint(nullptr, 0) == 0xFFFFFFFF);
}

std::string ClaudeLine(const std::string& id, const std::string& time) {
    return "{\"parentUuid\":\"x\",\"message\":{\"model\":\"claude-opus-5-5\",\"id\":\"" + id +
           "\",\"content\":[{\"type\":\"text\",\"text\":\"a \\\"usage\\\" {\\\"input_tokens\\\":999}\"}],"
           "\"usage\":{\"input_tokens\":10,\"cache_creation_input_tokens\":40,\"cache_read_input_tokens\":100,\"output_tokens\":20,"
           "\"cache_creation\":{\"ephemeral_1h_input_tokens\":30,\"ephemeral_5m_input_tokens\":10},"
           "\"iterations\":[{\"input_tokens\":5,\"output_tokens\":5}]}},\"requestId\":\"req_1\",\"type\":\"assistant\",\"timestamp\":\"" + time + "\"}";
}

void TestTimesParseInUtcAndWithAnOffset() {
    assert(*ParseTime("1970-01-01T00:00:00Z") == 0);
    assert(*ParseTime("2026-11-10T08:26:36.5Z") == 1794299196.5);
    assert(*ParseTime("2026-11-10T09:26:36+01:00") == 1794299196);
    assert(!ParseTime("yesterday"));
}

void TestScannerReadsOnlyTheObjectsOwnFields() {
    const std::string json = R"({"a":{"input_tokens":1},"text":"\"input_tokens\":2","input_tokens":3})";
    assert(json_scan::Int("input_tokens", json) == 3);
    assert(!json_scan::Value("missing", json));
}

void TestClaudeLinesGiveOneEntryPerRequest() {
    UsageLog log;
    const std::string text = ClaudeLine("msg_1", "2026-11-10T08:00:00Z") + "\n" + ClaudeLine("msg_1", "2026-11-10T08:00:01Z") + "\n" +
                             ClaudeLine("msg_2", "2026-11-10T08:01:00Z") + "\n" + ClaudeLine("msg_3", "2026-11-10T08:01:00Z").substr(0, 40);
    assert(log.ReadClaude(text, 0) == text.rfind('\n') + 1);
    assert(log.Entries().size() == 2);
    const UsageEntry& entry = log.Entries()[0];
    assert(entry.input == 10 && entry.output == 20 && entry.cacheRead == 100);
    assert(entry.cacheWrite1h == 30 && entry.cacheWrite5m == 10 && entry.Tokens() == 170);
    UsageLog old;
    old.ReadClaude(ClaudeLine("msg_1", "1970-01-01T00:00:10Z") + "\n", 100);
    assert(old.Entries().empty());
}

void TestOpusIsPricedFromTheList() {
    UsageEntry entry;
    entry.model = "claude-opus-5-5";
    entry.input = entry.output = entry.cacheRead = entry.cacheWrite5m = entry.cacheWrite1h = 1000000;
    assert(Near(*entry.Cost(), 4 + 20 + 0.2 + 5 + 8, 1e-9));
    assert(FindPrice("claude-opus-5")->input == 5);
    entry.model = "gpt-6.1-sol";
    assert(!entry.Cost());
}

void TestCodexCountsEachTurnOnceAndSplitsCachedInput() {
    const auto count = [](int total, const char* time) {
        return std::string("{\"timestamp\":\"") + time + "\",\"type\":\"event_msg\",\"payload\":{\"type\":\"token_count\",\"info\":{\"total_token_usage\":{\"total_tokens\":" +
               std::to_string(total) + "},\"last_token_usage\":{\"input_tokens\":1000,\"cached_input_tokens\":600,\"output_tokens\":50,\"total_tokens\":1050}},"
               "\"rate_limits\":{\"primary\":{\"used_percent\":32.0,\"window_minutes\":300,\"resets_at\":2000000000}}}}";
    };
    const std::string text = std::string(R"({"timestamp":"2026-11-10T01:00:00Z","type":"turn_context","payload":{"model":"gpt-5-codex"}})") + "\n" +
                             count(1050, "2026-11-10T01:00:01Z") + "\n" + count(1050, "2026-11-10T01:00:02Z") + "\n" + count(2100, "2026-11-10T01:00:03Z") + "\n";
    UsageLog log;
    CodexFileState state;
    log.ReadCodex(text, state, 0);
    assert(log.Entries().size() == 2);
    assert(log.Entries()[0].model == "gpt-5-codex");
    assert(log.Entries()[0].input == 400 && log.Entries()[0].cacheRead == 600 && log.Entries()[0].output == 50);
    assert(log.CodexLimit() && log.CodexLimit()->usedPercent == 32 && log.CodexLimit()->resetsAt == 2000000000);
    assert(state.lastTotal == 2100);
}

void TestBlockStartsOnTheHourAndEndsFiveHoursLater() {
    const double hour = 3600;
    const auto entry = [](double time) {
        UsageEntry e;
        e.time = time;
        e.model = "claude-opus-5-5";
        e.output = 1;
        return e;
    };
    const auto block = CurrentBlock({entry(10 * hour + 900), entry(11 * hour)}, 12 * hour);
    assert(block && block->start == 10 * hour && block->End() == 15 * hour && block->usage.tokens == 2);
    const auto next = CurrentBlock({entry(10 * hour), entry(15 * hour + 60)}, 16 * hour);
    assert(next && next->start == 15 * hour && next->usage.tokens == 1);
    assert(!CurrentBlock({entry(hour)}, 7 * hour));
}

void TestUsageNumbersReadTheWayThePageShowsThem() {
    using namespace usage_text;
    assert(Tokens(512) == "512" && Tokens(12000) == "12K" && Tokens(12430000) == "12.4M");
    assert(Dollars(8.214) == "$8.21");
    assert(Duration(2 * 3600 + 13 * 60) == "2h 13m" && Duration(30) == "1m");
    assert(DisplayName("claude-opus-5-5") == "Opus 5.5");
    assert(DisplayName("claude-sonnet-4-5-20250929") == "Sonnet 4.5");
    assert(DisplayName("gpt-5-codex") == "gpt-5-codex");
    UsageEntry known;
    known.model = "gpt-5";
    known.output = 1000000;
    UsageEntry unknown;
    unknown.model = "gpt-6.1-sol";
    unknown.output = 10;
    const auto all = [](const UsageEntry&) { return true; };
    assert(Cost(ToolUsage::From({known, unknown}, all)) == "$10.00+");
    assert(Cost(ToolUsage::From({unknown}, all)) == "\xE2\x80\x94");
}

void TestUsagePageFitsUnderTheHeader() {
    for (float header : {24.0f, 32.0f, 38.0f}) {
        const UsageLayout layout(IslandGeometry::ExpandedWidth(), IslandGeometry::ExpandedHeight(), header, 1);
        assert(layout.Row(1, kUsageRows - 1).Bottom() <= IslandGeometry::ExpandedHeight() - 12);
        assert(layout.Title(0).Right() < layout.Title(1).x);
    }
}

void TestHeaderControlsStayClearOfHomesControls() {
    const HeaderLayout header(IslandGeometry::ExpandedWidth(), 32, 1);
    const HomeLayout home(IslandGeometry::ExpandedWidth(), IslandGeometry::ExpandedHeight(), 32, 1);
    for (const HomeControl control : kHomeControls) {
        for (const HeaderControl tab : {HeaderControl::HomeTab, HeaderControl::UsageTab, HeaderControl::Settings}) {
            assert(!header.Frame(tab).Intersects(home.Frame(control)));
        }
    }
    const HomeRect usage = header.Frame(HeaderControl::UsageTab);
    assert(header.ControlAt(usage.x + 1, usage.y + 1) == HeaderControl::UsageTab);
}

void TestSwipeStepsOnePageHoweverLongItRuns() {
    PageSwipe swipe;
    assert(swipe.Add(20, 0) == 0);
    assert(swipe.Add(25, 0.02) == 1);
    assert(swipe.Add(200, 0.04) == 0);
    assert(swipe.Add(-50, 0.5) == -1);
    IslandPage next = IslandPage::Home;
    assert(!NeighborPage(IslandPage::Home, -1, &next));
    assert(NeighborPage(IslandPage::Home, 1, &next) && next == IslandPage::Usage);
}

}  // namespace

int main() {
    TestTimesParseInUtcAndWithAnOffset();
    TestScannerReadsOnlyTheObjectsOwnFields();
    TestClaudeLinesGiveOneEntryPerRequest();
    TestOpusIsPricedFromTheList();
    TestCodexCountsEachTurnOnceAndSplitsCachedInput();
    TestBlockStartsOnTheHourAndEndsFiveHoursLater();
    TestUsageNumbersReadTheWayThePageShowsThem();
    TestUsagePageFitsUnderTheHeader();
    TestHeaderControlsStayClearOfHomesControls();
    TestSwipeStepsOnePageHoweverLongItRuns();
    TestPillStaysCenteredAndInsideTheWindow();
    TestSpringStartsWhereItWasAskedTo();
    TestSpringSettlesOnTarget();
    TestSpringOvershootFitsTheWindowMargin();
    TestOpeningIsSlowerThanClosing();
    TestContentFadeCurveMatchesItsValues();
    TestVelocityIsTheDerivativeOfValue();
    TestCubicSegmentsTrackTheSpring();
    TestEverySegmentIsAcceptedByDirectComposition();
    TestRetargetContinuesFromCurrentMotion();
    TestFlaresFitBesideTheExpandedPill();
    TestFlareTapersFromThePillToTheTopEdge();
    TestFlarePixelsArePremultipliedBlackAndMirror();
    TestJumpLeavesNothingToAnimate();
    TestClockRunsFromTheReportedPositionAndStopsAtTheEnd();
    TestRepeatCyclesThroughEveryMode();
    TestLrcParsesTimestampsSkipsMetadataAndSorts();
    TestLyricsFindTheCurrentAndNextLine();
    TestTitlesLoseWhatPlayersAdd();
    TestLyricsMatchingPrefersSyncedThenClosestLength();
    TestJsonReadsOneRecordOrAnArray();
    TestPercentEncoding();
    TestHomeControlsDoNotOverlapAndStayInsideThePill();
    TestHomePointsFindTheirControl();
    TestFormatTime();
    TestNextTickIsTheNextSecondOrAnEarlierLyric();
    TestHomeModelShowsThePlaceholderAndFollowsLyrics();
    TestArtworkTintIsAverageAndLifted();
    std::puts("island_tests: all passed");
    return 0;
}
