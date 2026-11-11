// Covers the pure layer. Plain asserts: this runs anywhere with a C++20 compiler, including on macOS.
#undef NDEBUG
#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>

#include "features/home/home_layout.h"
#include "features/home/home_model.h"
#include "features/live/live_layout.h"
#include "features/home/home_settings_pane.h"
#include "features/settings/settings_panes.h"
#include "features/usage/usage_settings_pane.h"
#include "features/lyrics/lyrics.h"
#include "features/now_playing/now_playing.h"
#include "features/usage/ai_limits.h"
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

void TestStageWakesOnlyForTheNextLine() {
    assert(NextHomeTick(10.25, 1, -1, false) < 0);
    assert(Near(NextHomeTick(10.25, 1, 14.25, false), 4, 1e-6));
}

void TestLyricsButtonAndStageFitThePage() {
    for (float header : {24.0f, 32.0f, 38.0f}) {
        const HomeLayout layout(IslandGeometry::ExpandedWidth(), IslandGeometry::ExpandedHeight(), header, 1);
        const HeaderLayout headerLayout(IslandGeometry::ExpandedWidth(), header, 1);
        const HomeRect button = layout.LyricsButton(true);
        assert(!button.Intersects(headerLayout.Frame(HeaderControl::Settings)));
        assert(layout.Title().Right() <= layout.LyricsButton(false).x);
        for (int slot = kFirstStageSlot; slot <= kLastStageSlot; ++slot) {
            assert(!button.Intersects(layout.StageLine(slot)));
            if (slot < kLastStageSlot) assert(layout.StageLine(slot).Bottom() <= layout.StageLine(slot + 1).y);
        }
        assert(layout.StageLine(-1).y >= header);
        assert(layout.StageLine(2).Bottom() <= layout.Height() - 12);
        const float middleY = layout.StageLine(0).y + layout.StageLine(0).height / 2;
        assert(layout.StageSlotAt(layout.Width() / 2, middleY) == 0);
        assert(layout.ControlAt(layout.Width() / 2, middleY, true) == HomeControl::None);
        assert(layout.ControlAt(button.x + 5, button.y + 5, true) == HomeControl::Lyrics);
        const HomeRect play = layout.Frame(HomeControl::PlayPause);
        assert(layout.ControlAt(play.x + 5, play.y + 5, true) == HomeControl::None);
    }
}

void TestStageShowsTheLineBeforeAndTheTwoAfter() {
    NowPlaying nowPlaying;
    nowPlaying.title = "Song";
    nowPlaying.duration = 100;
    Lyrics lyrics;
    lyrics.synced = {{1, "one"}, {2, ""}, {3, "three"}, {4, "four"}};
    const HomeSettings settings;
    const std::string note = "\xE2\x99\xAA";
    const HomeModel middle = MakeHomeModel(nowPlaying, &lyrics, false, 2.5, settings, false, true);
    assert(middle.isOnStage && !middle.stageIsStatus);
    assert(middle.stageLines[0] == "one" && middle.stageLines[1] == note && middle.stageLines[2] == "three" && middle.stageLines[3] == "four");
    const HomeModel intro = MakeHomeModel(nowPlaying, &lyrics, false, 0, settings);
    assert(intro.stageLines[0].empty() && intro.stageLines[1] == note && intro.stageLines[2] == "one" && intro.stageLines[3] == note);
    const HomeModel end = MakeHomeModel(nowPlaying, &lyrics, false, 9, settings);
    assert(end.stageLines[0] == "three" && end.stageLines[1] == "four" && end.stageLines[2].empty());
    const HomeModel loading = MakeHomeModel(nowPlaying, nullptr, true, 0, settings, false, true);
    assert(loading.stageIsStatus && loading.stageLines[1] == "Looking for lyrics\xE2\x80\xA6");
    assert(MakeHomeModel({}, nullptr, false, 0, settings).stageLines[1] == "Nothing playing");
    HomeSettings off;
    off.showLyrics = false;
    const HomeModel hidden = MakeHomeModel(nowPlaying, &lyrics, false, 2.5, off, false, true);
    assert(!hidden.isOnStage && !hidden.showsLyricsButton);
}

void TestLyricStripGrowsTheLivePillDown() {
    const LiveLayout layout(180, 32);
    assert(Near(layout.Height(false), 32, 1e-6) && Near(layout.Height(true), 32 + LiveLayout::kLyricStrip, 1e-6));
    const HomeRect lyric = layout.Lyric();
    assert(lyric.y >= 32 && lyric.Bottom() <= layout.Height(true));
    assert(lyric.x >= LiveLayout::kOuterPadding && lyric.Right() <= layout.Width() - LiveLayout::kOuterPadding);
    IslandGeometry geometry(1.5f);
    geometry.SetLive(true);
    const Pill wings = geometry.PillFor(IslandState::Collapsed);
    geometry.SetLiveLyric(true);
    const Pill tall = geometry.PillFor(IslandState::Collapsed);
    assert(Near(tall.bottom, wings.bottom + LiveLayout::kLyricStrip * 1.5f, 1e-4) && Near(tall.left, wings.left, 1e-4));
    assert(Near(geometry.PillFor(IslandState::Expanded).bottom, IslandGeometry::ExpandedHeight() * 1.5f, 1e-4));
    geometry.SetLive(false);
    assert(Near(geometry.PillFor(IslandState::Collapsed).bottom, IslandGeometry::CollapsedHeight() * 1.5f, 1e-4));
}

void TestLiveLyricLineIsTheOneBeingSung() {
    Lyrics lyrics;
    lyrics.synced = {{2, "one"}, {4, ""}, {6, "three"}};
    std::string line;
    assert(LiveRule::LyricLine(&lyrics, 0, 0, &line) && line == "\xE2\x99\xAA");
    assert(LiveRule::LyricLine(&lyrics, 2.5, 0, &line) && line == "one");
    assert(LiveRule::LyricLine(&lyrics, 4.5, 0, &line) && line == "\xE2\x99\xAA");
    assert(LiveRule::LyricLine(&lyrics, 5.5, 0.5, &line) && line == "three");
    Lyrics plain;
    plain.plain = {"plain"};
    assert(!LiveRule::LyricLine(&plain, 3, 0, &line));
    assert(!LiveRule::LyricLine(nullptr, 3, 0, &line));
}

void TestArtworkMosaicAveragesRegions() {
    // A 4x4 cover: left half red, right half blue.
    std::vector<uint32_t> cover(16);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) cover[static_cast<size_t>(y * 4 + x)] = x < 2 ? 0xFFFF0000u : 0xFF0000FFu;
    const auto mosaic = ArtworkMosaic(cover.data(), 4, 2, 1);
    assert(mosaic.size() == 2 && mosaic[0] == 0xFFFF0000u && mosaic[1] == 0xFF0000FFu);
    const auto blend = ArtworkMosaic(cover.data(), 4, 1, 1);
    assert(blend[0] == 0xFF7F007Fu);
    assert(ArtworkMosaic(cover.data(), 4, 6, 3).size() == 18);
}

void TestHomeModelShowsThePlaceholderAndFollowsLyrics() {
    const HomeSettings settings;
    assert(!MakeHomeModel({}, nullptr, false, 0, settings).hasTrack);

    NowPlaying nowPlaying;
    nowPlaying.title = "Song";
    nowPlaying.duration = 100;
    Lyrics lyrics;
    lyrics.synced = {{1, "one"}, {2, "two"}, {3, "three"}};
    const HomeModel model = MakeHomeModel(nowPlaying, &lyrics, false, 2.5, settings);
    assert(model.lyric == "two" && model.nextLyric == "three" && model.lyricIndex == 1);
    const HomeModel intro = MakeHomeModel(nowPlaying, &lyrics, false, 0.5, settings);
    assert(intro.lyric == "\xE2\x99\xAA" && intro.nextLyric == "one" && intro.lyricIndex == -1);
    const HomeModel skipping = MakeHomeModel(nowPlaying, &lyrics, false, 50, settings, true);
    assert(skipping.isSkipping && skipping.title == "Song" && skipping.elapsedText == "0:00" && skipping.lyric.empty());
    assert(model.remainingText == "-1:37");
    assert(Near(model.progress, 0.025, 1e-9));

    HomeSettings off;
    off.showLyrics = false;
    const HomeModel hidden = MakeHomeModel(nowPlaying, &lyrics, false, 2.5, off);
    assert(hidden.lyric.empty() && hidden.nextLyric.empty());

    for (float header : {24.0f, 32.0f, 38.0f}) {
        const HomeLayout layout(IslandGeometry::ExpandedWidth(), IslandGeometry::ExpandedHeight(), header, 1);
        assert(layout.Lyric().y >= layout.Artist().Bottom());
        assert(layout.NextLyric().Bottom() <= layout.SliderTrack().y - 4);
    }
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

void TestUsageNumbersReadTheWayThePageShowsThem() {
    using namespace usage_text;
    assert(Tokens(512) == "512" && Tokens(12000) == "12K" && Tokens(12430000) == "12.4M");
    assert(Dollars(8.214) == "$8.21");
    assert(Duration(2 * 3600 + 13 * 60) == "2h 13m" && Duration(30) == "1m" && Duration(3 * 86400 + 4 * 3600) == "3d 4h");
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

void TestCodexCountsEachTurnOnceAndKeepsItsLimits() {
    const auto count = [](int total, const char* time) {
        return std::string("{\"timestamp\":\"") + time + "\",\"type\":\"event_msg\",\"payload\":{\"type\":\"token_count\",\"info\":{\"total_token_usage\":{\"total_tokens\":" +
               std::to_string(total) + "},\"last_token_usage\":{\"input_tokens\":1000,\"cached_input_tokens\":600,\"output_tokens\":50,\"total_tokens\":1050}},"
               "\"rate_limits\":{\"primary\":{\"used_percent\":32.0,\"window_minutes\":300,\"resets_at\":2000000000},"
               "\"secondary\":{\"used_percent\":6.0,\"window_minutes\":10080,\"resets_at\":2000300000}}}}";
    };
    const std::string text = std::string(R"({"timestamp":"2026-11-10T01:00:00Z","type":"turn_context","payload":{"model":"gpt-5-codex"}})") + "\n" +
                             count(1050, "2026-11-10T01:00:01Z") + "\n" + count(1050, "2026-11-10T01:00:02Z") + "\n" + count(2100, "2026-11-10T01:00:03Z") + "\n";
    UsageLog log;
    CodexFileState state;
    log.ReadCodex(text, state, 0);
    assert(log.Entries().size() == 2);
    assert(log.Entries()[0].model == "gpt-5-codex");
    assert(log.Entries()[0].input == 400 && log.Entries()[0].cacheRead == 600 && log.Entries()[0].output == 50);
    assert(log.CodexSamples().size() == 3);
    assert(*log.CodexSamples().back().fiveHour == (LimitWindow{32, 2000000000.0, 18000}));
    assert(log.CodexSamples().back().week->length == 604800);
}

void TestPaceComparesUseWithTheTimeGone() {
    const LimitWindow window{60, 10000 + 3.5 * 3600, LimitWindow::kFiveHours};
    assert(PaceOf(window, 10000) == Pace::Ahead);
    assert(PaceOf({30, window.resetsAt, window.length}, 10000) == Pace::Even);
    assert(PaceOf({5, window.resetsAt, window.length}, 10000) == Pace::Under);
    assert(window.Current(*window.resetsAt + 1).usedPercent == 0);
}

void TestAFastPaceWarnsBeforeTheFiveHourReset() {
    const double now = 100000;
    ToolLimits limits;
    limits.fiveHour = LimitWindow{70, now + 2 * 3600, LimitWindow::kFiveHours};
    limits.recentRate = 1.0 / 60;
    const LimitInsight insight = InsightOf(limits, now);
    assert(insight.kind == LimitInsight::Kind::RunsOut && Near(insight.time, 1800, 1e-6) && Near(*insight.resetsIn, 7200, 1e-6));
    assert(insight.Level() == Severity::Watch);
    limits.recentRate = 0.0;
    const LimitInsight calm = InsightOf(limits, now);
    assert(calm.kind == LimitInsight::Kind::OnTrack && Near(calm.value, 70, 1e-9));
    ToolLimits reached;
    reached.fiveHour = LimitWindow{100, now + 60, LimitWindow::kFiveHours};
    assert(InsightOf(reached, now).Level() == Severity::Critical);
    assert(InsightOf(ToolLimits::Failed(LimitProblem::Expired), now).kind == LimitInsight::Kind::Problem);
}

void TestTheWeekWarnsWhenItsAveragePaceRunsOutEarly() {
    const double now = 1000000;
    ToolLimits limits;
    limits.week = LimitWindow{60, now + 5 * 86400, LimitWindow::kWeek};
    const LimitInsight insight = InsightOf(limits, now);
    assert(insight.kind == LimitInsight::Kind::RunsOut && insight.window == LimitWindow::kWeek);
    assert(Near(insight.time, 40.0 * 2 * 86400 / 60, 1));
}

void TestPacesComeFromCodexPercentagesAndClaudeCost() {
    const double resets = 50000;
    const auto sample = [&](double time, double used) { return LimitSample{time, LimitWindow{used, resets, LimitWindow::kFiveHours}, std::nullopt}; };
    const double now = 40000;
    const auto codex = CodexRate({sample(now - 2400, 10), sample(now - 600, 25), sample(now - 60, 28)}, now);
    assert(codex && Near(*codex, 18.0 / 1800, 1e-12));
    const LimitWindow window{40, 100000 + 3 * 3600, LimitWindow::kFiveHours};
    const auto entry = [](double time) {
        UsageEntry e;
        e.time = time;
        e.model = "claude-opus-5-5";
        e.output = 100000;
        return e;
    };
    const auto claude = ClaudeRate({entry(100000 - 3600), entry(100000 - 600)}, window, 100000);
    assert(claude && Near(*claude, 20.0 / 1800, 1e-12));
}

void TestUsageResponseAndStoredLoginParse() {
    const auto limits = ClaudeLimitsFromResponse(R"({"five_hour":{"utilization":72.0,"resets_at":"2026-11-10T15:00:00.123456+00:00"},"seven_day":{"utilization":41,"resets_at":null},"seven_day_opus":null})");
    assert(limits && limits->fiveHour->usedPercent == 72 && Near(*limits->fiveHour->resetsAt, 1794322800.123456, 1e-3));
    assert(limits->week->usedPercent == 41 && !limits->week->resetsAt);
    assert(!ClaudeLimitsFromResponse("{}"));
    const auto login = ClaudeLoginFromCredentials(R"({"claudeAiOauth":{"accessToken":"token-value","refreshToken":"r","expiresAt":1794326400000}})");
    assert(login && login->accessToken == "token-value" && *login->expiresAt == 1794326400);
}

void TestTheLastGoodLimitsSurviveARoundTrip() {
    ToolLimits limits;
    limits.fiveHour = LimitWindow{72.5, 1794330000.0, LimitWindow::kFiveHours};
    limits.week = LimitWindow{41, std::nullopt, LimitWindow::kWeek};
    ToolLimits weekOnly;
    weekOnly.week = limits.week;
    const std::string text = EncodeLimitCache("claude", limits, 1794322800) + "\n" + EncodeLimitCache("codex", weekOnly, 5);
    const auto claude = DecodeLimitCache(text, "claude");
    assert(claude && claude->fiveHour == limits.fiveHour && claude->week == limits.week && *claude->asOf == 1794322800);
    assert(!DecodeLimitCache(text, "codex")->fiveHour);
    assert(!DecodeLimitCache("claude nonsense", "claude"));
    // The same line the macOS app writes reads back here.
    const auto mac = DecodeLimitCache("claude 1794322800.0 72.5 1794330000.0 18000.0 41.0 - 604800.0", "claude");
    assert(mac && mac->fiveHour->usedPercent == 72.5 && !mac->week->resetsAt);
    const UsageModel model = MakeUsageModel(*claude, ToolLimits::Failed(LimitProblem::NoData), UsageSummary{}, 1794323000, [](double) { return std::string("Tue 14:00"); });
    assert(model.claude.today == "As of Tue 14:00");
    // An expired login keeps the remembered rings but says what to do.
    ToolLimits expired = *claude;
    expired.problem = LimitProblem::Expired;
    const UsageModel stale = MakeUsageModel(expired, ToolLimits::Failed(LimitProblem::NoData), UsageSummary{}, 1794323000, [](double) { return std::string("Tue 14:00"); });
    assert(stale.claude.inner && stale.claude.center == "73%" && stale.claude.today == "As of Tue 14:00");
    assert(stale.claude.headline == "Login expired" && stale.claude.detail == "Run claude in a terminal");
}

void TestThePageSaysWhatMattersInAFewWords() {
    const double now = 100000;
    ToolLimits claude;
    claude.fiveHour = LimitWindow{70, now + 2 * 3600, LimitWindow::kFiveHours};
    claude.week = LimitWindow{20, now + 4 * 86400, LimitWindow::kWeek};
    claude.recentRate = 1.0 / 60;
    const UsageModel model = MakeUsageModel(claude, ToolLimits::Failed(LimitProblem::NoData), UsageSummary{}, now, [](double) { return std::string("Thu 14:00"); });
    assert(model.claude.center == "70%" && model.claude.caption == "5h");
    assert(model.claude.headline == "5h runs out in ~30m");
    assert(model.claude.detail == "1h 30m before its reset");
    assert(model.claude.rows[1].reset == "Thu 14:00");
    assert(model.claude.inner->fraction == 0.7);
    assert(model.codex.headline == "No limits yet" && model.codex.center == "\xE2\x80\x94");
}

void TestUsagePageFitsUnderTheHeader() {
    for (float header : {24.0f, 32.0f, 38.0f}) {
        const UsageLayout layout(IslandGeometry::ExpandedWidth(), IslandGeometry::ExpandedHeight(), header, 1);
        const float bottom = IslandGeometry::ExpandedHeight() - 8;
        assert(layout.Detail(1).Bottom() <= bottom && layout.Gauge(1).Bottom() <= bottom);
        assert(layout.Gauge(0).y >= header);
        assert(layout.RowReset(0, 0).Right() <= layout.Gauge(1).x);
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
    assert(!NeighborPage(IslandPage::Home, -1, true, &next));
    assert(NeighborPage(IslandPage::Home, 1, true, &next) && next == IslandPage::Usage);
    assert(!NeighborPage(IslandPage::Home, 1, false, &next));
}

}  // namespace

void TestLyricsTimingShiftsWhichLineIsSung() {
    NowPlaying nowPlaying;
    nowPlaying.title = "Song";
    nowPlaying.duration = 100;
    Lyrics lyrics;
    lyrics.synced = {{1, "one"}, {2, "two"}, {3, "three"}};
    HomeSettings settings;
    settings.lyricsOffset = 0.5;
    assert(MakeHomeModel(nowPlaying, &lyrics, false, 1.6, settings).lyric == "two");
    settings.showNextLyric = false;
    const HomeModel single = MakeHomeModel(nowPlaying, &lyrics, false, 1.6, settings);
    assert(single.lyric == "two" && single.nextLyric.empty());
}

void TestPagesOpenWhereTheSettingsSay() {
    PageSettings pages;
    assert(pages.Opening(IslandPage::Usage) == IslandPage::Usage);
    pages.openOn = OpenPage::Home;
    assert(pages.Opening(IslandPage::Usage) == IslandPage::Home);
    pages.openOn = OpenPage::Usage;
    pages.showUsage = false;
    assert(pages.Opening(IslandPage::Usage) == IslandPage::Home && pages.Shows(IslandPage::Home));
    const HeaderLayout header(IslandGeometry::ExpandedWidth(), 32, 1, false);
    assert(header.ControlAt(22 + 45, 16) == HeaderControl::None);
}

void TestUsageSettingsPickColumnsAndWhereAmberStarts() {
    UsageSettings settings;
    assert(settings.Tools().size() == 2);
    settings.showClaude = false;
    assert(settings.Tools().size() == 1 && settings.Tools()[0] == UsageTool::Codex);
    settings.showCodex = false;
    assert(settings.Tools().size() == 1 && settings.Tools()[0] == UsageTool::Claude);
    UsageLayout single(IslandGeometry::ExpandedWidth(), IslandGeometry::ExpandedHeight(), 32, 1);
    single.SetColumns(1);
    const UsageLayout both(IslandGeometry::ExpandedWidth(), IslandGeometry::ExpandedHeight(), 32, 1);
    const float columnWidth = both.Detail(0).Right() - both.Gauge(0).x;
    assert(Near(single.Gauge(0).x + columnWidth / 2, IslandGeometry::ExpandedWidth() / 2, 0.5));
    ToolLimits limits;
    limits.fiveHour = LimitWindow{65, std::nullopt, LimitWindow::kFiveHours};
    settings.warnAt = 60;
    const auto clock = [](double) { return std::string(); };
    const UsageModel warned = MakeUsageModel(limits, ToolLimits::Failed(LimitProblem::NoData), UsageSummary{}, 0, clock, settings);
    const UsageModel calm = MakeUsageModel(limits, ToolLimits::Failed(LimitProblem::NoData), UsageSummary{}, 0, clock);
    assert(warned.claude.rows[0].severity == Severity::Watch && calm.claude.rows[0].severity == Severity::Calm);
}

// Every row a pane describes reads back what it writes, so no control is wired to the wrong setting.
void TestSettingsPanesWriteWhatTheyRead() {
    for (const SettingsPane& pane : {GeneralSettingsPane(), PagesSettingsPane(), HomeSettingsPane(), UsageSettingsPane(), AboutSettingsPane()}) {
        assert(!pane.title.empty());
        for (const SettingsSection& section : pane.sections) {
            for (const SettingsRow& row : section.rows) {
                AppSettings settings;
                if (row.kind == SettingsRow::Kind::Toggle) {
                    const bool before = row.isOn(settings);
                    row.setOn(settings, !before);
                    assert(row.isOn(settings) == !before && !(settings == AppSettings{}));
                } else if (row.kind == SettingsRow::Kind::Choice) {
                    assert(ChoiceIndex(row, row.value(settings)) >= 0);
                    for (const auto& option : row.options) {
                        row.setValue(settings, option.second);
                        assert(Near(row.value(settings), option.second, 1e-9));
                    }
                }
            }
        }
    }
    assert(SecondsLabel(0.15) == L"0.15 s" && SecondsLabel(-0.5, true) == L"\x2212" L"0.5 s" && SecondsLabel(1, true) == L"+1 s");
}

void TestGlowFadesBeforeTheTopEdge() {
    for (float header : {24.0f, 32.0f, 38.0f}) {
        const HomeLayout layout(IslandGeometry::ExpandedWidth(), IslandGeometry::ExpandedHeight(), header, 2);
        assert(layout.GlowRadiusY() >= layout.GlowCenterY());
        assert(layout.GlowCenterX() + layout.GlowRadiusX() < layout.Width());
    }
}

void TestLiveWingsFitBesideTheNotch() {
    const LiveLayout layout(IslandGeometry::CollapsedWidth(), IslandGeometry::CollapsedHeight());
    assert(layout.Width() > IslandGeometry::CollapsedWidth());
    assert(Near(layout.ArtSide(), 18, 1e-6) && Near(layout.Wing(), 35, 1e-6));
    // The artwork stays in the left wing and the bars in the right, clear of the notch.
    assert(layout.Artwork().Right() <= layout.Wing() - LiveLayout::kInnerGap + 0.01f);
    assert(layout.Bars().x >= layout.Wing() + IslandGeometry::CollapsedWidth() + LiveLayout::kInnerGap - 0.01f);
    assert(layout.Bars().Right() <= layout.Width() - LiveLayout::kOuterPadding + 0.01f);
    assert(layout.Bars().y >= 0 && layout.Bars().Bottom() <= layout.Height());
    IslandGeometry geometry(1);
    const float narrow = geometry.PillFor(IslandState::Collapsed).right - geometry.PillFor(IslandState::Collapsed).left;
    geometry.SetLive(true);
    const Pill live = geometry.PillFor(IslandState::Collapsed);
    assert(Near(live.right - live.left, layout.Width(), 1e-3) && live.right - live.left > narrow);
    assert(live.left >= geometry.FlareRadius() && live.right <= geometry.Window().width - geometry.FlareRadius());
}

void TestLiveShowsWhilePlayingAndBrieflyAfterAPause() {
    NowPlaying playing;
    playing.title = "Song";
    playing.isPlaying = true;
    assert(LiveRule::Shows(playing, true, -1, 100));
    assert(!LiveRule::Shows(playing, false, -1, 100));
    NowPlaying paused = playing;
    paused.isPlaying = false;
    assert(LiveRule::Shows(paused, true, 95, 100));
    assert(!LiveRule::Shows(paused, true, 80, 100));
    assert(!LiveRule::Shows(NowPlaying{}, true, -1, 100));
}

void TestBarsRiseAndFallSmoothlyInGradedShades() {
    const auto cycle = BarCycle(0.5, 0.3f, 1.0f);
    assert(cycle.size() == 2);
    assert(Near(BarSegmentValue(cycle[0], 0), 0.3, 1e-6) && Near(BarSegmentValue(cycle[0], 0.5), 1.0, 1e-5));
    assert(Near(BarSegmentValue(cycle[1], 0), 1.0, 1e-6) && Near(BarSegmentValue(cycle[1], 0.5), 0.3, 1e-5));
    // Flat at both ends, so the repeat joins without a kink.
    assert(cycle[0].c1 == 0 && cycle[1].c1 == 0);
    const auto bar = BarPixels(3, 12, 0xFFFFFFFF);
    assert((bar[0] >> 24) < 0xFF && (bar[1 + 6 * 3] >> 24) == 0xFF);
    for (int i = 0; i < LiveLayout::kBarCount; ++i) assert(LiveLayout::kBarWhiteness[i] == LiveLayout::kBarWhiteness[LiveLayout::kBarCount - 1 - i]);
    assert(TowardWhite(0xFF000000, 0) == 0xFF000000 && TowardWhite(0xFF000000, 1) == 0xFFFFFFFF);
    assert(TowardWhite(0xFF804020, 0.5f) == 0xFFC0A090);
    const std::vector<uint32_t> square(16 * 16, 0xFFFFFFFF);
    const auto rounded = RoundedCorners(square.data(), 16, 4);
    assert((rounded[0] >> 24) == 0 && rounded[8 * 16 + 8] == 0xFFFFFFFF);
}

int main() {
    TestTimesParseInUtcAndWithAnOffset();
    TestScannerReadsOnlyTheObjectsOwnFields();
    TestClaudeLinesGiveOneEntryPerRequest();
    TestOpusIsPricedFromTheList();
    TestCodexCountsEachTurnOnceAndKeepsItsLimits();
    TestPaceComparesUseWithTheTimeGone();
    TestAFastPaceWarnsBeforeTheFiveHourReset();
    TestTheWeekWarnsWhenItsAveragePaceRunsOutEarly();
    TestPacesComeFromCodexPercentagesAndClaudeCost();
    TestUsageResponseAndStoredLoginParse();
    TestTheLastGoodLimitsSurviveARoundTrip();
    TestThePageSaysWhatMattersInAFewWords();
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
    TestLyricStripGrowsTheLivePillDown();
    TestLiveLyricLineIsTheOneBeingSung();
    TestArtworkMosaicAveragesRegions();
    TestStageWakesOnlyForTheNextLine();
    TestLyricsButtonAndStageFitThePage();
    TestStageShowsTheLineBeforeAndTheTwoAfter();
    TestArtworkTintIsAverageAndLifted();
    TestLyricsTimingShiftsWhichLineIsSung();
    TestPagesOpenWhereTheSettingsSay();
    TestUsageSettingsPickColumnsAndWhereAmberStarts();
    TestSettingsPanesWriteWhatTheyRead();
    TestGlowFadesBeforeTheTopEdge();
    TestLiveWingsFitBesideTheNotch();
    TestLiveShowsWhilePlayingAndBrieflyAfterAPause();
    TestBarsRiseAndFallSmoothlyInGradedShades();
    std::puts("island_tests: all passed");
    return 0;
}
