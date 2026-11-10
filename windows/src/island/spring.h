#pragma once

// Spring motion, reduced to the piecewise cubics DirectComposition can evaluate on its own.
// DWM has no spring primitive, but it runs cubic segments out of process, so the curve is computed
// once per transition here and this process does no per-frame work. No Windows headers.

#include <algorithm>
#include <array>
#include <cmath>

#include "island_geometry.h"

namespace noitch {

// x(t) = c0 + c1*t + c2*t^2 + c3*t^3, with t in seconds from the start of the segment.
struct CubicSegment {
    double offset;
    float c0;
    float c1;
    float c2;
    float c3;
};

// Same parameters as the macOS CASpringAnimation(perceptualDuration:bounce:), so the two feel alike.
struct SpringParameters {
    double response;
    double bounce;
};

// Opening is slower and softer than closing: the eye follows content in, but wants it gone fast.
inline constexpr SpringParameters kExpandSpring{0.5, 0.14};
inline constexpr SpringParameters kCollapseSpring{0.38, 0.18};

inline constexpr SpringParameters SpringFor(IslandState state) {
    return state == IslandState::Expanded ? kExpandSpring : kCollapseSpring;
}

// A critically-damped-or-softer spring toward a fixed target, solved in closed form.
class ScalarSpring {
public:
    ScalarSpring() = default;
    ScalarSpring(double from, double velocity, double target, SpringParameters parameters = kCollapseSpring)
        : parameters_(parameters), target_(target), displacement_(from - target), velocity_(velocity) {}

    double Target() const { return target_; }

    double Value(double t) const {
        return target_ + std::exp(-Decay() * t) * (displacement_ * std::cos(Frequency() * t) + SineTerm() * std::sin(Frequency() * t));
    }

    double Velocity(double t) const {
        const double c = std::cos(Frequency() * t);
        const double s = std::sin(Frequency() * t);
        return std::exp(-Decay() * t) * (Frequency() * (SineTerm() * c - displacement_ * s) - Decay() * (displacement_ * c + SineTerm() * s));
    }

    // Time until the motion stays within `tolerance` of the target.
    double SettleTime(double tolerance) const {
        const double amplitude = std::hypot(displacement_, SineTerm());
        return amplitude <= tolerance ? 0.0 : std::log(amplitude / tolerance) / Decay();
    }

    // The cubic Hermite through both endpoints: it matches value and velocity at each end, so
    // adjacent segments join without a visible seam.
    CubicSegment Segment(double begin, double end) const {
        const double h = end - begin;
        const double p0 = Value(begin);
        const double p1 = Value(end);
        const double v0 = Velocity(begin);
        const double v1 = Velocity(end);
        return {
            begin,
            static_cast<float>(p0),
            static_cast<float>(v0),
            static_cast<float>((3 * (p1 - p0) / h - 2 * v0 - v1) / h),
            static_cast<float>((2 * (p0 - p1) / h + v0 + v1) / (h * h)),
        };
    }

private:
    double NaturalFrequency() const { return 2 * 3.14159265358979323846 / parameters_.response; }
    double Damping() const { return 1 - parameters_.bounce; }
    double Decay() const { return Damping() * NaturalFrequency(); }
    double Frequency() const { return NaturalFrequency() * std::sqrt(1 - Damping() * Damping()); }
    double SineTerm() const { return (velocity_ + Decay() * displacement_) / Frequency(); }

    SpringParameters parameters_ = kCollapseSpring;
    double target_ = 0;
    double displacement_ = 0;
    double velocity_ = 0;
};

// The four animated pill properties, advanced together. Retarget continues from the current value
// and velocity, so reversing mid-flight neither jumps nor stalls.
class PillMotion {
public:
    static constexpr int kChannels = 4;
    // Keeps the fit far below a pixel: about 0.01 px for a 300 px move (see island_tests).
    static constexpr double kMaxSegmentSeconds = 1.0 / 60;
    static constexpr double kTolerancePx = 0.25;

    void Jump(const Pill& pill) {
        const auto values = Channels(pill);
        for (int i = 0; i < kChannels; ++i) springs_[i] = ScalarSpring(values[i], 0, values[i]);
        duration_ = 0;
    }

    // Each channel keeps the parameters it was started with, so a reversal hands over the old
    // spring's value and velocity even when the new direction uses a different spring.
    void Retarget(const Pill& target, double now, SpringParameters parameters) {
        const double elapsed = now - start_;
        const auto values = Channels(target);
        double duration = 0;
        for (int i = 0; i < kChannels; ++i) {
            springs_[i] = ScalarSpring(springs_[i].Value(elapsed), springs_[i].Velocity(elapsed), values[i], parameters);
            duration = std::max(duration, springs_[i].SettleTime(kTolerancePx));
        }
        start_ = now;
        duration_ = duration;
    }

    Pill ValueAt(double now) const {
        const double t = now - start_;
        return {
            static_cast<float>(springs_[0].Value(t)),
            static_cast<float>(springs_[1].Value(t)),
            static_cast<float>(springs_[2].Value(t)),
            static_cast<float>(springs_[3].Value(t)),
        };
    }

    Pill Target() const {
        return {
            static_cast<float>(springs_[0].Target()),
            static_cast<float>(springs_[1].Target()),
            static_cast<float>(springs_[2].Target()),
            static_cast<float>(springs_[3].Target()),
        };
    }

    double Duration() const { return duration_; }

    // Equal-width segments: an accumulated step could leave a near-zero last segment, whose
    // coefficients would overflow to infinity, which DirectComposition rejects.
    int SegmentCount() const { return static_cast<int>(std::ceil(duration_ / kMaxSegmentSeconds)); }

    CubicSegment Segment(int channel, int index) const {
        const double width = duration_ / SegmentCount();
        return springs_[channel].Segment(index * width, (index + 1) * width);
    }

    const ScalarSpring& Channel(int index) const { return springs_[index]; }

private:
    static std::array<float, kChannels> Channels(const Pill& pill) {
        return {pill.left, pill.right, pill.bottom, pill.radius};
    }

    std::array<ScalarSpring, kChannels> springs_;
    double start_ = 0;
    double duration_ = 0;
};

// An ease-out cubic from `from` to `to`, starting `delay` after `start`. 1 - (1 - u)^3 is itself a
// cubic, so DirectComposition runs it as one exact segment, and reversing needs only ValueAt.
struct ContentFade {
    double start = 0;
    double delay = 0;
    double duration = 0;
    float from = 0;
    float to = 0;

    double End() const { return start + delay + duration; }

    double ValueAt(double now) const {
        const double t = now - start - delay;
        if (t <= 0) return from;
        if (t >= duration) return to;
        const double rest = 1 - t / duration;
        return from + (to - from) * (1 - rest * rest * rest);
    }

    // The moving part, with t measured from the start of the delay.
    CubicSegment Curve() const {
        const double d = duration;
        const double delta = to - from;
        return {delay, from, static_cast<float>(3 * delta / d), static_cast<float>(-3 * delta / (d * d)), static_cast<float>(delta / (d * d * d))};
    }
};

}  // namespace noitch
