#pragma once

// Zero-delay-feedback (TPT) state-variable filter: resonant low-pass and band-pass. JUCE-free, header-only.
// One instance per voice; reset() clears state (voice reuse). Self-oscillation is excluded: the minimum
// damping k = 1/Qmax stays strictly positive.

#include <algorithm>
#include <cmath>

#include "Dsp/DspUtil.h"
#include "Parameters/ParameterTable.h"

namespace snaral
{
enum class SvfMode : int { LowPass = 0, BandPass = 1 };

struct SvfParams
{
    float cutoffHz = 1200.0f;
    float resonancePercent = 20.0f;
    SvfMode mode = SvfMode::LowPass;

    static SvfParams from (const ParamSnapshot& s) noexcept
    {
        SvfParams p;
        p.cutoffHz = s[P::filter_cutoff_hz];
        p.resonancePercent = s[P::filter_resonance];
        p.mode = s.choice (P::filter_type) == 1 ? SvfMode::BandPass : SvfMode::LowPass;
        return p;
    }
};

class SvfFilter
{
public:
    static constexpr float minCutoffHz = 20.0f;
    static constexpr float maxCutoffHz = 20000.0f;
    static constexpr float minQ = 0.5f;               // resonance 0 %
    static constexpr float maxQ = 10.0f;              // resonance 80 % (+20 dB peak), tested stable
    static constexpr float maxResonancePercent = 80.0f;
    static constexpr float stateLimit = 1.0e4f;       // hard bound on integrator states (safety net)

    /// Resonance 0..80 % -> Q, exponential: Q = 0.5 * 20^(r/80).
    static float resonanceToQ (float percent) noexcept
    {
        const float r = clampf (sanitize (percent), 0.0f, maxResonancePercent) / maxResonancePercent;
        return minQ * std::pow (maxQ / minQ, r);
    }

    void prepare (double sampleRate) noexcept
    {
        sr_ = std::isfinite (sampleRate) && sampleRate > 1000.0 ? sampleRate : 44100.0;
        maxHz_ = std::min (maxCutoffHz, static_cast<float> (0.49 * sr_));
        reset();
        setResonancePercent (20.0f);
        setCutoff (1200.0f);
        dirty_ = true; // the sample rate may have changed: coefficients must be rebuilt even if cutoff/k are unchanged
    }

    /// Clears integrator state (voice reuse). Coefficients are kept.
    void reset() noexcept { ic1_ = ic2_ = 0.0f; }

    void setParams (const SvfParams& p) noexcept
    {
        setMode (p.mode);
        setResonancePercent (p.resonancePercent);
        setCutoff (p.cutoffHz);
    }

    void setMode (SvfMode m) noexcept { mode_ = m; }

    void setResonancePercent (float percent) noexcept
    {
        const float k = 1.0f / resonanceToQ (percent);
        if (k != k_)
        {
            k_ = k;
            dirty_ = true;
        }
    }

    void setCutoff (float hz) noexcept
    {
        hz = clampf (std::isfinite (hz) ? hz : 1200.0f, minCutoffHz, maxHz_);
        if (hz != cutoff_)
        {
            cutoff_ = hz;
            dirty_ = true;
        }
    }

    float process (float x) noexcept
    {
        if (dirty_)
            updateCoefficients();
        if (! std::isfinite (x))
            x = 0.0f;
        const float v3 = x - ic2_;
        const float v1 = a1_ * ic1_ + a2_ * v3;
        const float v2 = ic2_ + a2_ * ic1_ + a3_ * v3;
        ic1_ = 2.0f * v1 - ic1_;
        ic2_ = 2.0f * v2 - ic2_;
        if (! (std::fabs (ic1_) < stateLimit && std::fabs (ic2_) < stateLimit)) // also catches NaN
        {
            reset();
            return 0.0f;
        }
        ic1_ = flushDenormal (ic1_);
        ic2_ = flushDenormal (ic2_);
        return mode_ == SvfMode::LowPass ? v2 : k_ * v1; // BP normalised to unity peak gain
    }

    /// Per-sample cutoff modulation.
    float process (float x, float cutoffHz) noexcept
    {
        setCutoff (cutoffHz);
        return process (x);
    }

    float cutoffHz() const noexcept { return cutoff_; }

private:
    void updateCoefficients() noexcept
    {
        const float g = static_cast<float> (std::tan (pi * static_cast<double> (cutoff_) / sr_));
        a1_ = 1.0f / (1.0f + g * (g + k_));
        a2_ = g * a1_;
        a3_ = g * a2_;
        dirty_ = false;
    }

    double sr_ = 44100.0;
    float maxHz_ = maxCutoffHz;
    float cutoff_ = 1200.0f, k_ = 1.0f / minQ;
    float a1_ = 0.0f, a2_ = 0.0f, a3_ = 0.0f;
    float ic1_ = 0.0f, ic2_ = 0.0f;
    SvfMode mode_ = SvfMode::LowPass;
    bool dirty_ = true;
};
} // namespace snaral
