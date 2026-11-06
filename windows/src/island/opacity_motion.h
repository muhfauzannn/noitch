#pragma once

// An opacity ramp, reduced to the cubic segments DirectComposition can evaluate on its own, like the
// spring is. It is ease-out: strong at the start, so a change registers at once. No Windows headers.
//
// DWM can't be asked where an animation has reached, so the value at any time is computed here. A
// ramp that starts while another is running continues from the value the first had reached.

#include <algorithm>

#include "spring.h"

namespace noitch {

class OpacityMotion {
public:
    void Jump(float value) {
        from_ = to_ = value;
        start_ = 0;
        duration_ = 0;
    }

    // From wherever the last ramp had reached at `now`.
    void Retarget(float to, double duration, double now) { Start(Value(now), to, duration, now); }

    // From an explicit value, for a change that should begin at a set level.
    void Start(float from, float to, double duration, double now) {
        from_ = from;
        to_ = to;
        start_ = now;
        duration_ = duration;
    }

    float Value(double now) const {
        const double t = now - start_;
        if (duration_ <= 0 || t >= duration_) return to_;
        if (t <= 0) return from_;
        const double u = t / duration_;
        const double eased = 1 - (1 - u) * (1 - u) * (1 - u);
        return static_cast<float>(from_ + (to_ - from_) * eased);
    }

    float Target() const { return to_; }
    double Duration() const { return duration_; }

    // x(t) = from + delta * (3u - 3u^2 + u^3), with u = t / duration.
    CubicSegment Segment() const {
        const double delta = static_cast<double>(to_) - from_;
        return {
            0,
            from_,
            static_cast<float>(3 * delta / duration_),
            static_cast<float>(-3 * delta / (duration_ * duration_)),
            static_cast<float>(delta / (duration_ * duration_ * duration_)),
        };
    }

private:
    float from_ = 0;
    float to_ = 0;
    double start_ = 0;
    double duration_ = 0;
};

}  // namespace noitch
