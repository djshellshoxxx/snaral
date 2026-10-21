#pragma once

// Shared TPT (zero-delay-feedback) state-variable section for the bus-level mid effects.
// JUCE-free, header-only, allocation-free. Owner: engine-fx.

#include "../DspUtil.h"

namespace snaral::fx
{
struct SvfCoeffs
{
    float a1 = 1.0f, a2 = 0.0f, a3 = 0.0f, k = 1.4142135f;

    /// g = tan(pi f / fs), k = 1 / Q (k > 0 keeps the section unconditionally stable).
    void set (float g, float kIn) noexcept
    {
        k = kIn;
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
};

struct SvfOut { float lp, bp, hp; };

struct SvfState
{
    float ic1 = 0.0f, ic2 = 0.0f;

    void reset() noexcept { ic1 = ic2 = 0.0f; }

    SvfOut process (float x, const SvfCoeffs& c) noexcept
    {
        const float v3 = x - ic2;
        const float v1 = c.a1 * ic1 + c.a2 * v3;
        const float v2 = ic2 + c.a2 * ic1 + c.a3 * v3;
        ic1 = flushDenormal (2.0f * v1 - ic1);
        ic2 = flushDenormal (2.0f * v2 - ic2);
        return { v2, v1, x - c.k * v1 - v2 };
    }
};

inline float tanG (float hz, double sampleRate) noexcept
{
    return std::tan (static_cast<float> (pi) * hz / static_cast<float> (sampleRate));
}
} // namespace snaral::fx
