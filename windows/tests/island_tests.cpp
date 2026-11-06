// Covers the pure layer. Plain asserts: this runs anywhere with a C++20 compiler, including on macOS.
#undef NDEBUG
#include <cassert>
#include <cmath>
#include <cstdio>

#include "features/home/home_layout.h"
#include "features/home/home_model.h"
#include "features/lyrics/lyrics.h"
#include "features/now_playing/now_playing.h"
#include "island/flare.h"
#include "island/island_geometry.h"
#include "island/opacity_motion.h"
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
    const ScalarSpring spring(0, 0, 130);  // The largest half-width travel, at scale 1.
    double peak = 0;
    for (double t = 0; t < 1; t += 0.001) peak = std::fmax(peak, spring.Value(t));
    assert(peak > 130);
    assert(peak - 130 < 16);
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
    motion.Retarget(geometry.PillFor(IslandState::Expanded), 0);

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
    motion.Retarget(geometry.PillFor(IslandState::Expanded), 10.0);

    const Pill before = motion.ValueAt(10.1);
    const double velocity = motion.Channel(2).Velocity(0.1);
    motion.Retarget(geometry.PillFor(IslandState::Collapsed), 10.1);
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

void TestOpacityRampsFromStartToTargetAndEasesOut() {
    OpacityMotion motion;
    motion.Jump(0);
    motion.Start(0.35f, 1, 0.15, 10);
    assert(Near(motion.Value(9), 0.35, 1e-6));
    assert(Near(motion.Value(10), 0.35, 1e-6));
    assert(Near(motion.Value(10.15), 1, 1e-6));
    assert(Near(motion.Value(99), 1, 1e-6));
    // Ease-out: more than half of the change has happened by half of the time.
    assert(motion.Value(10.075) > 0.35 + 0.65 / 2);
}

void TestOpacityCubicSegmentMatchesTheCurve() {
    OpacityMotion motion;
    motion.Start(0.2f, 0.9f, 0.1, 0);
    const CubicSegment segment = motion.Segment();
    for (double t : {0.0, 0.02, 0.05, 0.08, 0.1}) {
        assert(Near(Evaluate(segment, t), motion.Value(t), 1e-5));
    }
}

void TestOpacityRetargetContinuesFromWhereItWas() {
    OpacityMotion motion;
    motion.Start(0, 1, 0.1, 0);
    const float midway = motion.Value(0.04);
    motion.Retarget(0, 0.1, 0.04);
    assert(Near(motion.Value(0.04), midway, 1e-6));
    assert(Near(motion.Value(0.2), 0, 1e-6));
}

void TestReducedBounceSettlesWithoutVisibleOvershoot() {
    const ScalarSpring spring(0, 0, 300, kReducedBounce);
    double peak = 0;
    for (double t = 0; t < 1.5; t += 0.001) peak = std::max(peak, spring.Value(t));
    assert(peak - 300 < 0.5);
    assert(Near(spring.Value(1.5), 300, 0.01));
    // Full bounce does overshoot, or the test above proves nothing.
    const ScalarSpring bouncy(0, 0, 300);
    double bouncyPeak = 0;
    for (double t = 0; t < 1.5; t += 0.001) bouncyPeak = std::max(bouncyPeak, bouncy.Value(t));
    assert(bouncyPeak - 300 > 1);
}

void TestPillMotionTakesTheBounceItIsGiven() {
    PillMotion plain;
    PillMotion reduced;
    const IslandGeometry geometry;
    plain.Jump(geometry.PillFor(IslandState::Collapsed));
    reduced.Jump(geometry.PillFor(IslandState::Collapsed));
    plain.Retarget(geometry.PillFor(IslandState::Expanded), 0);
    reduced.Retarget(geometry.PillFor(IslandState::Expanded), 0, kReducedBounce);
    assert(plain.Channel(1).Value(plain.Duration() / 3) != reduced.Channel(1).Value(reduced.Duration() / 3));
}

void TestPlatesBelongToButtonsThatExist() {
    HomeModel model;
    assert(ControlHasPlate(HomeControl::PlayPause, model));
    assert(!ControlHasPlate(HomeControl::Slider, model));
    assert(!ControlHasPlate(HomeControl::Artwork, model));
    model.showsLyricsButton = false;
    model.showsShuffleAndRepeat = false;
    assert(!ControlHasPlate(HomeControl::Lyrics, model));
    assert(!ControlHasPlate(HomeControl::Repeat, model));
    assert(ControlHasPlate(HomeControl::Settings, model));
}

}  // namespace

int main() {
    TestPillStaysCenteredAndInsideTheWindow();
    TestSpringStartsWhereItWasAskedTo();
    TestSpringSettlesOnTarget();
    TestSpringOvershootFitsTheWindowMargin();
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
    TestOpacityRampsFromStartToTargetAndEasesOut();
    TestOpacityCubicSegmentMatchesTheCurve();
    TestOpacityRetargetContinuesFromWhereItWas();
    TestReducedBounceSettlesWithoutVisibleOvershoot();
    TestPillMotionTakesTheBounceItIsGiven();
    TestPlatesBelongToButtonsThatExist();
    std::puts("island_tests: all passed");
    return 0;
}
