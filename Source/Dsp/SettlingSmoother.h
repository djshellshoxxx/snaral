#pragma once

// One-pole smoother that is guaranteed to land EXACTLY on its target (no stuck state).
// DspUtil.h's Smoother keeps float state and snaps only when |current - target| < 1e-7 (absolute). With a
// slow time constant the per-step decrement drops below half a float ulp hundreds of ulps before the target
// (measured: a 20 ms trim glide to 0.063 froze at 0.0630036, and targets >= 1 can never meet the 1e-7 test).
// This variant keeps double state, so it converges to ~1e-13 and then snaps to the float target.
// Owner: engine-output.

#include "DspUtil.h"

namespace snaral
{
class SettlingSmoother
{
public:
    void prepare (double sampleRate, float timeMs) noexcept
    {
        coeff_ = timeMs <= 0.0f ? 0.0 : std::exp (-1.0 / (0.001 * timeMs * sampleRate));
    }
    void reset (float v) noexcept { current_ = target_ = v; }
    void setTarget (float v) noexcept { target_ = static_cast<double> (v); }
    float next() noexcept
    {
        if (current_ == target_) return static_cast<float> (target_);
        current_ = target_ + coeff_ * (current_ - target_);
        if (std::fabs (current_ - target_) <= 3.0e-8 * std::max (1.0, std::fabs (target_)))
            current_ = target_;
        return static_cast<float> (current_);
    }
    float current() const noexcept { return static_cast<float> (current_); }
    float target() const noexcept { return static_cast<float> (target_); }
    bool isSettled() const noexcept { return current_ == target_; }

private:
    double coeff_ = 0.0, current_ = 0.0, target_ = 0.0;
};
} // namespace snaral
