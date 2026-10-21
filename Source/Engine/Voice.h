#pragma once

// One synth voice: sub layer (SubOscillator -> sub Adsr -> anchor gain) and mid layer (MidOscillator A/B blend + Noise/
// Metal texture -> SvfFilter modulated by Contour / LFO / Trajectory -> mid Adsr). JUCE-free, header-only, real-time
// safe (no allocation after prepare()). Owner: engine-integration. The voice is driven by SnaralEngine in control blocks
// of at most engineControlBlock samples; per-sample shared control signals (macro gains, bend, routing) arrive in a
// VoiceBlockInput.
//
// Isolation contract (docs/specs/sub-mid-engine.md): the sub signal depends only on pitch (note, transpose, glide, bend,
// master tune, sub fine tune), the sub waveform, the sub Adsr, the Sub Anchor gain, velocity-to-level and the sub gain
// (Weight, sub level, solo/mute). Nothing from the mid layer (Bite, Motion, texture, filter, Space, drive, effects)
// can reach it.

#include "Dsp/Adsr.h"
#include "Dsp/Contour.h"
#include "Dsp/DspUtil.h"
#include "Dsp/Lfo.h"
#include "Dsp/Oscillators.h"
#include "Dsp/SvfFilter.h"
#include "Dsp/Texture.h"
#include "Dsp/Trajectory.h"
#include "Engine/EngineTypes.h"
#include "Engine/NoteMath.h"
#include "Engine/SubAnchor.h"
#include "Engine/VoiceAllocator.h"
#include "Parameters/MacroMappings.h"
#include "Parameters/ParameterTable.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace snaral
{
inline constexpr int engineControlBlock = 32;

/// Everything a voice needs from one ParamSnapshot, in domain units.
struct VoiceParams
{
    SubOscParams sub;
    MidOscParams mid;
    Adsr::Params subEnv, midEnv;
    SvfMode filterMode = SvfMode::LowPass;
    ContourParams contour;
    LfoParams lfo;
    TrajectoryParams traj;
    TextureParams texture;
    SubAnchorParams anchor;
    double a4Hz = 440.0;
    double transposeSemitones = 0.0;
    float glideMs = 0.0f;
    float velToLevel = 0.0f, velToAttack = 0.0f;
    bool trajEnabled = false;
    bool lfoRetrigger = true;

    static VoiceParams from (const ParamSnapshot& s) noexcept
    {
        VoiceParams p;
        p.sub = SubOscParams::from (s);
        p.mid = MidOscParams::from (s);
        p.subEnv = { s[P::sub_amp_attack_ms], s[P::sub_amp_decay_ms], s.percent (P::sub_amp_sustain), s[P::sub_amp_release_ms] };
        p.midEnv = { s[P::mid_amp_attack_ms], s[P::mid_amp_decay_ms], s.percent (P::mid_amp_sustain), s[P::mid_amp_release_ms] };
        p.filterMode = SvfParams::from (s).mode;
        p.contour = ContourParams::from (s);
        p.lfo = LfoParams::from (s);
        p.traj = TrajectoryParams::from (s);
        p.texture = TextureParams::from (s);
        p.texture.growlGain = 1.0f; // Growl is applied once, on the voice mid output (engine midGain), not inside the texture
        p.anchor = SubAnchorParams::from (s);
        p.a4Hz = s[P::master_tune_hz];
        p.transposeSemitones = s[P::transpose_semitones];
        p.glideMs = s[P::glide_ms];
        p.velToLevel = s[P::velocity_to_level];
        p.velToAttack = s[P::velocity_to_attack];
        p.trajEnabled = s.flag (P::traj_enabled);
        p.lfoRetrigger = s.flag (P::lfo_retrigger);
        return p;
    }
};

/// Per-sample shared control signals for one control block (all arrays hold `n` valid samples).
struct VoiceBlockInput
{
    int n = 0;
    const float* bendSt = nullptr;        // smoothed pitch bend, semitones
    const float* subGain = nullptr;       // Weight^2 * sub level * audibility
    const float* midGain = nullptr;       // Growl^2 * mid level * audibility
    const float* cutoffLog2 = nullptr;    // smoothed log2(base cutoff Hz)
    const float* contourSt = nullptr;     // cutoff semitones per unit contour (Motion-scaled depth + Bite, see engine)
    const float* lfoSt = nullptr;
    const float* trajSt = nullptr;
    const float* position = nullptr;      // base source position 0..1
    const float* contourPos = nullptr;
    const float* lfoPos = nullptr;
    const float* trajPos = nullptr;
    const float* sharedLfo = nullptr;     // shared LFO (used when lfo_retrigger is off)
    float resonancePercent = 20.0f;
    float maxCutoffHz = 20000.0f;
};

struct VoiceModOut
{
    float contourSum = 0.0f, lfoSum = 0.0f;
    int count = 0;
};

class Voice
{
public:
    void prepare (double sampleRate, std::uint32_t seed)
    {
        sr_ = sampleRate;
        sub_.prepare (sampleRate);
        mid_.prepare (sampleRate);
        subEnv_.prepare (sampleRate);
        midEnv_.prepare (sampleRate);
        filter_.prepare (sampleRate);
        contour_.prepare (sampleRate);
        lfo_.prepare (sampleRate);
        traj_.prepare (sampleRate);
        texture_.prepare (sampleRate, seed);
        anchor_.prepare (sampleRate);
        glide_.prepare (sampleRate);
        glide_.reset (60.0);
        lastMid_ = MidOscParams {};
        hzValid_ = false;
        active_ = false;
    }

    /// Hard stop: silence, idle, deterministic state (no de-click: callers use it from prepare/reset only).
    void reset() noexcept
    {
        subEnv_.kill();
        midEnv_.kill();
        sub_.reset (0.0);
        mid_.reset (0.0);
        filter_.reset();
        contour_.reset();
        lfo_.reset();
        traj_.reset();
        texture_.reset();
        anchor_.reset();
        glide_.reset (60.0);
        hzValid_ = false;
        active_ = false;
        velGain_ = 1.0f;
    }

    /// Applied once per block by the engine; cheap for unchanged values.
    void setParams (const VoiceParams& p) noexcept
    {
        if (p.a4Hz != p_.a4Hz) hzValid_ = false;
        sub_.setWaveform (p.sub.wave);
        subRatio_ = p.sub.tuneRatio;
        midRatio_ = p.mid.tuneRatio;
        if (! sameMid (p.mid, lastMid_))
        {
            mid_.setParams (p.mid);
            lastMid_ = p.mid;
        }
        subEnv_.setParams (p.subEnv);
        midEnv_.setParams (p.midEnv);
        filter_.setMode (p.filterMode);
        contour_.setParams (p.contour);
        lfo_.setParams (p.lfo);
        traj_.setParams (p.traj);
        texture_.setParams (p.texture);
        anchor_.setParams (p.anchor);
        p_ = p;
    }

    void setHostTiming (const HostTiming& t) noexcept
    {
        lfo_.setHostTiming (t);
        traj_.setHostTiming (t);
    }

    // ---- voice commands (from VoiceAllocator) ----
    void start (const VoiceCommand& c) noexcept
    {
        const bool wasIdle = ! subEnv_.isActive() && ! midEnv_.isActive();
        const double p0 = transposedPitch (c.note, p_.transposeSemitones);
        note_ = c.note;
        velGain_ = velocityToLevelGain (c.velocity, p_.velToLevel);
        const float attackScale = velocityToAttackScale (c.velocity, p_.velToAttack);
        if (wasIdle)
        {
            // Deterministic start from silence: phases at zero, no stale filter/modulator state.
            sub_.reset (0.0);
            mid_.reset (0.0);
            filter_.reset();
            contour_.reset();
            lfo_.reset();
            traj_.reset();
            glide_.reset (p0);
            anchor_.snap (p0);
        }
        else
        {
            // Steal / retrigger while sounding: pitch glides only when the allocator says so; phases keep running (no click).
            glide_.setTarget (p0, p_.glideMs, c.glide);
        }
        subEnv_.noteOn (attackScale);
        midEnv_.noteOn (attackScale);
        contour_.noteOn (true);
        lfo_.noteOn();
        traj_.noteOn (true);
        texture_.noteOn (static_cast<float> (c.note), 1.0f);
        active_ = true;
    }

    /// Legato: new pitch on a gated voice, envelopes / contour / trajectory keep running.
    void retarget (const VoiceCommand& c) noexcept
    {
        note_ = c.note;
        glide_.setTarget (transposedPitch (c.note, p_.transposeSemitones), p_.glideMs, c.glide);
    }

    void release() noexcept
    {
        subEnv_.noteOff();
        midEnv_.noteOff();
        contour_.noteOff();
    }

    void kill (int declickSamples) noexcept
    {
        subEnv_.forceRelease (declickSamples);
        midEnv_.forceRelease (declickSamples);
        contour_.noteOff();
    }

    bool active() const noexcept { return active_; }
    int note() const noexcept { return note_; }
    double currentPitch() const noexcept { return glide_.current(); }

    /// Renders and ADDS into midBus / subBus. Returns true when the voice has finished (both envelopes idle).
    bool render (const VoiceBlockInput& in, float* midBus, float* subBus, VoiceModOut& mod) noexcept
    {
        if (! active_)
            return false;
        const int n = in.n;
        float midBuf[engineControlBlock], subBuf[engineControlBlock], cutoff[engineControlBlock];

        anchor_.update (glide_.current());
        filter_.setResonancePercent (in.resonancePercent);

        float lastC = 0.0f, lastL = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const double p1 = glide_.next();
            const double p2 = p1 + static_cast<double> (in.bendSt[i]);
            if (! hzValid_ || p2 != lastP2_)
            {
                hzBase_ = p_.a4Hz * std::exp2 ((p2 - 69.0) / 12.0);
                lastP2_ = p2;
                hzValid_ = true;
            }

            const float c = contour_.process();
            const float l = p_.lfoRetrigger ? lfo_.process() : in.sharedLfo[i];
            const float t = p_.trajEnabled ? traj_.process() : 0.0f;
            lastC = c;
            lastL = l;

            const float st = clampf (c * in.contourSt[i] + l * in.lfoSt[i] + t * in.trajSt[i], -maxModSemitones, maxModSemitones);
            cutoff[i] = clampf (std::exp2 (in.cutoffLog2[i] + st * (1.0f / 12.0f)), 20.0f, in.maxCutoffHz);
            const float pos = clampf (in.position[i] + c * in.contourPos[i] + l * in.lfoPos[i] + t * in.trajPos[i], 0.0f, 1.0f);

            midBuf[i] = mid_.process (static_cast<float> (hzBase_ * midRatio_), pos);
            subBuf[i] = sub_.process (static_cast<float> (hzBase_ * subRatio_));
        }

        texture_.addTo (midBuf, n); // noise + metal join the source before the filter (Texture.h contract)

        for (int i = 0; i < n; ++i)
        {
            const float y = filter_.process (midBuf[i], cutoff[i]);
            midBus[i] += y * midEnv_.next() * velGain_ * in.midGain[i];
            subBus[i] += subBuf[i] * subEnv_.next() * anchor_.next() * velGain_ * in.subGain[i];
        }

        mod.contourSum += lastC;
        mod.lfoSum += lastL;
        ++mod.count;

        if (! subEnv_.isActive() && ! midEnv_.isActive())
        {
            active_ = false;
            return true;
        }
        return false;
    }

private:
    static bool sameMid (const MidOscParams& a, const MidOscParams& b) noexcept
    {
        return a.sourceA == b.sourceA && a.sourceB == b.sourceB && a.blend == b.blend && a.position == b.position
               && a.pulseWidth == b.pulseWidth && a.tuneRatio == b.tuneRatio;
    }

    double sr_ = 48000.0;
    SubOscillator sub_;
    MidOscillator mid_;
    Adsr subEnv_, midEnv_;
    SvfFilter filter_;
    Contour contour_;
    Lfo lfo_;
    Trajectory traj_;
    TextureLayer texture_;
    SubAnchorStage anchor_;
    PitchGlide glide_;
    VoiceParams p_;
    MidOscParams lastMid_;
    double hzBase_ = 440.0, lastP2_ = 0.0;
    float subRatio_ = 1.0f, midRatio_ = 1.0f, velGain_ = 1.0f;
    int note_ = 0;
    bool hzValid_ = false;
    bool active_ = false;
};

/// LFO shared by all voices when lfo_retrigger is off (running phase). In sync mode with a valid, playing transport the
/// phase is derived from PPQ at the start of a block, so loops and repositions realign it; the 2 ms slew limiter (as in
/// Lfo) bounds the resulting step. Free mode ignores the transport.
class SharedLfo
{
public:
    void prepare (double sampleRate) noexcept
    {
        sr_ = sampleRate;
        maxSlew_ = static_cast<float> (2.0 / (0.002 * sampleRate));
        setParams (LfoParams {});
        reset();
    }
    void reset() noexcept
    {
        phase_ = 0.0;
        raw_ = lfoWaveform (params_.wave, offsetPhase());
        out_ = raw_;
        havePpq_ = false;
        samplesSince_ = 0;
    }
    void setParams (const LfoParams& p) noexcept
    {
        params_ = p;
        params_.rateHz = clampf (std::isfinite (p.rateHz) ? p.rateHz : freeRate_, 0.02f, 20.0f);
        params_.phaseDegrees = clampf (sanitize (p.phaseDegrees), 0.0f, 360.0f);
        params_.division = std::clamp (p.division, 0, lfoNumDivisions - 1);
        freeRate_ = params_.rateHz;
        updateRate();
    }
    /// bpmValid: a tempo has been seen (the last valid one is used). realign: the transport is playing with a valid PPQ.
    void setTiming (double bpm, bool bpmValid, bool playing, bool ppqValid, double ppq) noexcept
    {
        bpmValid_ = bpmValid;
        bpm_ = bpmValid ? bpm : 120.0;
        updateRate();
        if (! (params_.sync && bpmValid_ && playing && ppqValid && std::isfinite (ppq)))
        {
            havePpq_ = false;
            return;
        }
        const double expected = ppqAtBlock_ + static_cast<double> (samplesSince_) * bpm_ / (60.0 * sr_);
        if (! havePpq_ || std::fabs (ppq - expected) > 0.002)
        {
            const double cycleBeats = lfoDivisionBeats (params_.division);
            const double q = ppq / cycleBeats;
            phase_ = q - std::floor (q);
        }
        havePpq_ = true;
        ppqAtBlock_ = ppq;
        samplesSince_ = 0;
    }
    float process() noexcept
    {
        raw_ = lfoWaveform (params_.wave, offsetPhase());
        const float d = raw_ - out_;
        out_ = std::fabs (d) <= maxSlew_ ? raw_ : out_ + (d > 0.0f ? maxSlew_ : -maxSlew_);
        phase_ += rateHz_ / sr_;
        phase_ -= std::floor (phase_);
        ++samplesSince_;
        return out_;
    }
    double rateHz() const noexcept { return rateHz_; }
    double phase() const noexcept { return phase_; }

private:
    double offsetPhase() const noexcept
    {
        double q = phase_ + static_cast<double> (params_.phaseDegrees) / 360.0;
        return q - std::floor (q);
    }
    void updateRate() noexcept
    {
        if (! params_.sync)
            rateHz_ = params_.rateHz;
        else if (bpmValid_)
            rateHz_ = std::min (lfoSyncedRateHz (params_.division, bpm_), lfoMaxSyncedRateHz);
        else
            rateHz_ = freeRate_;
    }

    double sr_ = 48000.0, phase_ = 0.0, rateHz_ = 2.0, bpm_ = 120.0, ppqAtBlock_ = 0.0, freeRate_ = 2.0;
    std::int64_t samplesSince_ = 0;
    float maxSlew_ = 0.05f, raw_ = 0.0f, out_ = 0.0f;
    bool bpmValid_ = false, havePpq_ = false;
    LfoParams params_;
};
} // namespace snaral
