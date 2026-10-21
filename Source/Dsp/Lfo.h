#pragma once

// Motion LFO: free (0.02-20 Hz) or host-synced (18 divisions), sine/triangle/saw/pulse, phase offset,
// retrigger on/off, tempo fallback with a UI status flag. JUCE-free, header-only. One instance per
// voice (retrigger) or one shared instance (retrigger off); the engine decides.
//
// Output is bipolar -1..1 (depth and Motion scaling are applied by MotionRouting in Contour.h).
// A slew limiter (full scale in 2 ms) only touches discontinuities (saw reset, pulse edges, phase or
// waveform jumps, retrigger); smooth waveforms at <= 20 Hz are not affected. rawValue() is the unslewed value.

#include <algorithm>
#include <cmath>

#include "Dsp/DspUtil.h"
#include "Engine/EngineTypes.h"
#include "Parameters/ParameterTable.h"

namespace snaral
{
enum class LfoWave : int { Sine = 0, Triangle = 1, Saw = 2, Pulse = 3 };

inline constexpr int lfoNumDivisions = 18;
inline constexpr double lfoMaxSyncedRateHz = 100.0;

/// Division choice index (lfo_sync_division ordering: 1/1, 1/1d, 1/1t, 1/2, ... 1/32t) -> length in quarter notes.
inline double lfoDivisionBeats (int division) noexcept
{
    const int d = std::clamp (division, 0, lfoNumDivisions - 1);
    constexpr double straight[6] = { 4.0, 2.0, 1.0, 0.5, 0.25, 0.125 }; // 1/1 .. 1/32
    const int kind = d % 3;                                              // 0 straight, 1 dotted, 2 triplet
    const double mult = kind == 0 ? 1.0 : (kind == 1 ? 1.5 : 2.0 / 3.0);
    return straight[d / 3] * mult;
}

inline double lfoSyncedRateHz (int division, double bpm) noexcept
{
    return bpm / (60.0 * lfoDivisionBeats (division));
}

/// Bipolar waveform of a phase in [0,1). Sine/triangle start at 0 rising, saw ramps -1 -> +1, pulse is +1 first half.
inline float lfoWaveform (LfoWave w, double phase) noexcept
{
    switch (w)
    {
        case LfoWave::Sine: return static_cast<float> (std::sin (twoPi * phase));
        case LfoWave::Triangle:
        {
            double q = phase + 0.25;
            q -= std::floor (q);
            return static_cast<float> (1.0 - 4.0 * std::abs (q - 0.5));
        }
        case LfoWave::Saw: return static_cast<float> (2.0 * phase - 1.0);
        case LfoWave::Pulse: return phase < 0.5 ? 1.0f : -1.0f;
    }
    return 0.0f;
}

struct LfoParams
{
    bool sync = false;
    float rateHz = 2.0f;
    int division = 6;
    LfoWave wave = LfoWave::Sine;
    float phaseDegrees = 0.0f;
    float depth01 = 0.5f;
    bool retrigger = true;

    static LfoParams from (const ParamSnapshot& s) noexcept
    {
        LfoParams p;
        p.sync = s.choice (P::lfo_mode) == 1;
        p.rateHz = s[P::lfo_rate_hz];
        p.division = s.choice (P::lfo_sync_division);
        p.wave = static_cast<LfoWave> (std::clamp (s.choice (P::lfo_waveform), 0, 3));
        p.phaseDegrees = s[P::lfo_phase];
        p.depth01 = s.percent (P::lfo_depth);
        p.retrigger = s.flag (P::lfo_retrigger);
        return p;
    }
};

class Lfo
{
public:
    static constexpr const char* fallbackMessage = "Host tempo unavailable - using free rate";

    void prepare (double sampleRate) noexcept
    {
        sr_ = std::isfinite (sampleRate) && sampleRate > 1000.0 ? sampleRate : 44100.0;
        maxSlewPerSample_ = static_cast<float> (2.0 / (0.002 * sr_));
        setParams (LfoParams {});
        reset();
    }

    void setParams (const LfoParams& p) noexcept
    {
        params_ = p;
        params_.rateHz = clampf (std::isfinite (p.rateHz) ? p.rateHz : lastFreeRateHz_, 0.02f, 20.0f); // NaN keeps the last valid rate
        params_.phaseDegrees = clampf (sanitize (p.phaseDegrees), 0.0f, 360.0f);
        params_.depth01 = clampf (sanitize (p.depth01), 0.0f, 1.0f);
        params_.division = std::clamp (p.division, 0, lfoNumDivisions - 1);
        lastFreeRateHz_ = params_.rateHz; // "last valid free rate" used when the host tempo is missing
        updateRate();
    }

    void setHostTiming (const HostTiming& t) noexcept
    {
        bpmValid_ = t.bpmValid && std::isfinite (t.bpm) && t.bpm > 0.0;
        bpm_ = bpmValid_ ? std::clamp (t.bpm, 10.0, 999.0) : 120.0;
        updateRate();
    }

    /// Deterministic reset: phase 0, output state snapped to the waveform at the phase offset.
    void reset() noexcept
    {
        phase_ = 0.0;
        raw_ = lfoWaveform (params_.wave, offsetPhase());
        out_ = raw_;
    }

    /// Note-on: resets phase only when Retrigger is on; otherwise the phase continues.
    void noteOn() noexcept
    {
        if (params_.retrigger)
            phase_ = 0.0;
    }

    /// True when sync is selected but no valid host tempo exists (UI shows fallbackMessage).
    bool usingFallbackRate() const noexcept { return params_.sync && ! bpmValid_; }
    double currentRateHz() const noexcept { return rateHz_; }
    double phase() const noexcept { return phase_; }
    float rawValue() const noexcept { return raw_; }
    const LfoParams& params() const noexcept { return params_; }

    /// One sample, -1..1.
    float process() noexcept
    {
        raw_ = lfoWaveform (params_.wave, offsetPhase());
        const float d = raw_ - out_;
        out_ = std::fabs (d) <= maxSlewPerSample_ ? raw_ : out_ + (d > 0.0f ? maxSlewPerSample_ : -maxSlewPerSample_);
        phase_ += rateHz_ / sr_;
        phase_ -= std::floor (phase_);
        return out_;
    }

private:
    double offsetPhase() const noexcept
    {
        double q = phase_ + static_cast<double> (params_.phaseDegrees) / 360.0;
        q -= std::floor (q);
        return q;
    }

    void updateRate() noexcept
    {
        if (! params_.sync)
            rateHz_ = params_.rateHz;
        else if (bpmValid_)
            rateHz_ = std::min (lfoSyncedRateHz (params_.division, bpm_), lfoMaxSyncedRateHz);
        else
            rateHz_ = lastFreeRateHz_;
    }

    double sr_ = 44100.0, phase_ = 0.0, rateHz_ = 2.0, bpm_ = 120.0, lastFreeRateHz_ = 2.0;
    bool bpmValid_ = false;
    float maxSlewPerSample_ = 0.05f, raw_ = 0.0f, out_ = 0.0f;
    LfoParams params_;
};
} // namespace snaral
