// Covers the pure layer. Plain asserts: this runs anywhere with a C++20 compiler, including on macOS.
#undef NDEBUG
#include <cassert>
#include <cmath>
#include <cstdio>

#include "island/flare.h"
#include "island/island_geometry.h"
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
    std::puts("island_tests: all passed");
    return 0;
}
