#pragma once

// Sub Anchor range compensation (docs/specs/expansion-sub-anchor.md). JUCE-free, header-only, real-time safe.
//
//   gainDb = -12 * s * tanh(d / 12),  d = p - a,  s = anchor_strength / 100
//
// p is the voice's continuous played pitch in MIDI note units AFTER transpose and glide and BEFORE pitch bend and
// the per-layer fine tune. At d == 0 the gain factor is exactly 1.0f. Non-finite input gives 1.0f. The gain only
// multiplies the sub signal (sub osc -> sub amp ADSR -> anchor gain -> layer sum); the mid layer never sees it.

#include "Dsp/DspUtil.h"
#include "Dsp/SettlingSmoother.h"
#include "Parameters/ParameterTable.h"

#include <cmath>

namespace snaral
{
struct SubAnchorParams
{
    bool enabled = false;
    int note = 36;           // 24..60
    float strength = 0.5f;   // 0..1

    static SubAnchorParams from (const ParamSnapshot& s) noexcept
    {
        SubAnchorParams p;
        p.enabled = s.flag (P::anchor_enabled);
        p.note = static_cast<int> (std::lround (clampf (sanitize (s[P::anchor_note]), 24.0f, 60.0f)));
        p.strength = clampf (sanitize (s.percent (P::anchor_strength)), 0.0f, 1.0f);
        return p;
    }

    bool active() const noexcept { return enabled && strength > 0.0f; }
};

/// Closed-form compensation in dB (always |result| < 12 * strength). Odd, strictly monotonic for strength > 0.
inline double subAnchorGainDb (double pitchNote, int anchorNote, double strength01) noexcept
{
    if (! std::isfinite (pitchNote) || ! std::isfinite (strength01))
        return 0.0;
    const double s = strength01 < 0.0 ? 0.0 : (strength01 > 1.0 ? 1.0 : strength01);
    const double d = pitchNote - static_cast<double> (anchorNote);
    return -12.0 * s * std::tanh (d / 12.0);
}

/// Linear gain factor. Exactly 1.0f at d == 0 (and for non-finite input or strength 0).
inline float subAnchorGain (double pitchNote, int anchorNote, double strength01) noexcept
{
    if (! std::isfinite (pitchNote) || ! std::isfinite (strength01) || strength01 <= 0.0
        || pitchNote == static_cast<double> (anchorNote))
        return 1.0f;
    return static_cast<float> (std::pow (10.0, subAnchorGainDb (pitchNote, anchorNote, strength01) / 20.0));
}

/// Per-voice gain stage: target recomputed at control rate from the continuous pitch, 10 ms one-pole smoothing.
/// While disabled the smoothed gain rests at exactly 1.0f, so multiplying by it leaves the sub bit-identical.
class SubAnchorStage
{
public:
    static constexpr float smoothMs = 10.0f;

    void prepare (double sampleRate) noexcept { gain_.prepare (sampleRate, smoothMs); gain_.reset (1.0f); }
    void setParams (const SubAnchorParams& p) noexcept { params_ = p; }

    /// Control-rate update (at most every 32 samples). pitchNote = transposed + glided pitch, before bend.
    void update (double pitchNote) noexcept
    {
        lastGain_ = params_.active() ? subAnchorGain (pitchNote, params_.note, params_.strength) : 1.0f;
        gain_.setTarget (lastGain_);
    }
    /// Snap to the gain for this pitch (note start: no audible ramp from a stale value).
    void snap (double pitchNote) noexcept
    {
        lastGain_ = params_.active() ? subAnchorGain (pitchNote, params_.note, params_.strength) : 1.0f;
        gain_.reset (lastGain_);
    }
    void reset() noexcept { gain_.reset (1.0f); lastGain_ = 1.0f; }

    float next() noexcept { return gain_.next(); }
    float targetGain() const noexcept { return lastGain_; }
    float currentGain() const noexcept { return gain_.current(); }

private:
    SubAnchorParams params_;
    SettlingSmoother gain_;
    float lastGain_ = 1.0f;
};
} // namespace snaral
