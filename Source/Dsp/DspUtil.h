#pragma once

// Small shared DSP helpers. JUCE-free, header-only, real-time safe. Owner: foundation (do not fork; extend by appending).

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace snaral
{
inline constexpr double pi = 3.14159265358979323846;
inline constexpr double twoPi = 6.28318530717958647692;

inline float dbToGain (float db) noexcept { return std::pow (10.0f, db * 0.05f); }
inline float gainToDb (float gain) noexcept { return 20.0f * std::log10 (std::max (gain, 1.0e-9f)); }
inline float midiNoteToHz (float note, float a4Hz = 440.0f) noexcept { return a4Hz * std::exp2 ((note - 69.0f) / 12.0f); }
inline float clampf (float x, float lo, float hi) noexcept { return x < lo ? lo : (x > hi ? hi : x); }
inline float sanitize (float x) noexcept { return std::isfinite (x) ? x : 0.0f; }
inline float flushDenormal (float x) noexcept { return std::fabs (x) < 1.0e-20f ? 0.0f : x; }

/// One-pole exponential smoother (time constant in ms to ~63%); snaps when within epsilon.
class Smoother
{
public:
    void prepare (double sampleRate, float timeMs) noexcept
    {
        coeff_ = timeMs <= 0.0f ? 0.0f : static_cast<float> (std::exp (-1.0 / (0.001 * timeMs * sampleRate)));
    }
    void reset (float value) noexcept { current_ = target_ = value; }
    void setTarget (float value) noexcept { target_ = value; }
    float next() noexcept
    {
        current_ = target_ + coeff_ * (current_ - target_);
        if (std::fabs (current_ - target_) < 1.0e-7f) current_ = target_;
        return current_;
    }
    float current() const noexcept { return current_; }
    float target() const noexcept { return target_; }
    bool isSettled() const noexcept { return current_ == target_; }

private:
    float coeff_ = 0.0f, current_ = 0.0f, target_ = 0.0f;
};
} // namespace snaral
