#pragma once

// Note contour (ADSR-style, three shapes) plus MotionRouting: the fixed routing of contour / LFO /
// trajectory to filter cutoff (log domain) and mid source position. JUCE-free, header-only.

#include <algorithm>
#include <cmath>

#include "Dsp/DspUtil.h"
#include "Parameters/MacroMappings.h"
#include "Parameters/ParameterTable.h"

namespace snaral
{
enum class ContourShape : int { Linear = 0, EaseIn = 1, EaseOut = 2 };

inline constexpr float contourExpK = 4.0f; // exponential steepness of ease_in / ease_out

/// Progress shaping x in [0,1] -> [0,1]. ease_in = slow start (exponential), ease_out = fast start.
inline float contourShapeFn (ContourShape s, float x) noexcept
{
    x = clampf (x, 0.0f, 1.0f);
    const float den = std::expm1 (contourExpK);
    switch (s)
    {
        case ContourShape::Linear: return x;
        case ContourShape::EaseIn: return std::expm1 (contourExpK * x) / den;
        case ContourShape::EaseOut: return 1.0f - std::expm1 (contourExpK * (1.0f - x)) / den;
    }
    return x;
}

struct ContourParams
{
    float attackMs = 5.0f, decayMs = 300.0f, sustain01 = 0.3f, releaseMs = 200.0f;
    ContourShape shape = ContourShape::Linear;
    float depth01 = 0.5f;

    static ContourParams from (const ParamSnapshot& s) noexcept
    {
        ContourParams p;
        p.attackMs = s[P::contour_attack_ms];
        p.decayMs = s[P::contour_decay_ms];
        p.sustain01 = s.percent (P::contour_sustain);
        p.releaseMs = s[P::contour_release_ms];
        p.shape = static_cast<ContourShape> (std::clamp (s.choice (P::contour_shape), 0, 2));
        p.depth01 = s.percent (P::contour_depth);
        return p;
    }
};

/// Contour generator, output 0..1 (depth / Motion are applied by MotionRouting). Per voice.
///
/// Sample i after note-on (attack length A samples, start value v0):
///   i < A            : v0 + (1 - v0) * shape(i / A)
///   then decay D     : 1 + (S - 1) * shape(j / D),  j = 0 .. D-1   (j = 0 is exactly 1)
///   then sustain     : S (tracked through a 5 ms smoother so live edits do not click)
///   release R after note-off from the value vr: vr * (1 - shape(j / R)), then 0.
class Contour
{
public:
    enum class Stage : int { Idle, Attack, Decay, Sustain, Release };

    void prepare (double sampleRate) noexcept
    {
        sr_ = std::isfinite (sampleRate) && sampleRate > 1000.0 ? sampleRate : 44100.0;
        sustain_.prepare (sr_, 5.0f);
        setParams (ContourParams {});
        reset();
    }

    void setParams (const ContourParams& p) noexcept
    {
        params_ = p;
        params_.attackMs = clampf (sanitize (p.attackMs), 0.0f, 500.0f);
        params_.decayMs = clampf (sanitize (p.decayMs), 5.0f, 3000.0f);
        params_.sustain01 = clampf (sanitize (p.sustain01), 0.0f, 1.0f);
        params_.releaseMs = clampf (sanitize (p.releaseMs), 5.0f, 5000.0f);
        params_.depth01 = clampf (sanitize (p.depth01), 0.0f, 1.0f);
        sustain_.setTarget (params_.sustain01);
    }

    /// Deterministic reset (voice reuse): idle, value 0, sustain smoother snapped.
    void reset() noexcept
    {
        stage_ = Stage::Idle;
        value_ = 0.0f;
        n_ = len_ = 0;
        startValue_ = 0.0f;
        sustain_.reset (params_.sustain01);
    }

    /// retrigger=false is the legato hook: a note-on while the contour is held (attack/decay/sustain) is ignored.
    /// Otherwise the attack starts from the current value (no jump).
    void noteOn (bool retrigger = true) noexcept
    {
        if (! retrigger && (stage_ == Stage::Attack || stage_ == Stage::Decay || stage_ == Stage::Sustain))
            return;
        startValue_ = value_;
        beginStage (Stage::Attack);
    }

    void noteOff() noexcept
    {
        if (stage_ == Stage::Idle || stage_ == Stage::Release)
            return;
        startValue_ = value_; // release start value
        beginStage (Stage::Release);
    }

    Stage stage() const noexcept { return stage_; }
    bool isActive() const noexcept { return stage_ != Stage::Idle; }
    float value() const noexcept { return value_; }
    ContourShape shape() const noexcept { return params_.shape; }

    float process() noexcept
    {
        const float sus = sustain_.next();
        switch (stage_)
        {
            case Stage::Idle:
                value_ = 0.0f;
                break;
            case Stage::Attack:
                value_ = startValue_ + (1.0f - startValue_) * contourShapeFn (params_.shape, static_cast<float> (n_) / static_cast<float> (len_));
                advance (Stage::Decay);
                break;
            case Stage::Decay:
                value_ = 1.0f + (sus - 1.0f) * contourShapeFn (params_.shape, static_cast<float> (n_) / static_cast<float> (len_));
                advance (Stage::Sustain);
                break;
            case Stage::Sustain:
                value_ = sus;
                break;
            case Stage::Release:
                value_ = startValue_ * (1.0f - contourShapeFn (params_.shape, static_cast<float> (n_) / static_cast<float> (len_)));
                advance (Stage::Idle);
                break;
        }
        value_ = clampf (value_, 0.0f, 1.0f);
        return value_;
    }

private:
    void beginStage (Stage s) noexcept
    {
        stage_ = s;
        n_ = 0;
        const float ms = s == Stage::Attack ? params_.attackMs : (s == Stage::Decay ? params_.decayMs : params_.releaseMs);
        len_ = static_cast<int> (std::lround (static_cast<double> (ms) * 0.001 * sr_));
        if (s == Stage::Attack && len_ <= 0)
            beginStage (Stage::Decay); // zero attack: peak on the first sample
        else if (len_ < 1)
            len_ = 1;
    }

    void advance (Stage next) noexcept
    {
        if (++n_ >= len_)
        {
            if (next == Stage::Idle)
                stage_ = Stage::Idle; // the next process() call returns 0
            else
                beginStage (next);
        }
    }

    double sr_ = 44100.0;
    ContourParams params_;
    Stage stage_ = Stage::Idle;
    float value_ = 0.0f, startValue_ = 0.0f;
    int n_ = 0, len_ = 0;
    Smoother sustain_;
};

// ---------------------------------------------------------------------------------------------------------
// Fixed modulation routing. Cutoff is modulated in the log (semitone) domain; position is bounded to 0..1.

inline constexpr float contourCutoffRangeSemitones = 60.0f; // contour depth 100 % = +5 octaves peak
inline constexpr float lfoCutoffRangeSemitones = 24.0f;     // LFO depth 100 % = +-2 octaves
inline constexpr float trajCutoffRangeSemitones = 60.0f;    // trajectory depth 100 % = +5 octaves peak
inline constexpr float contourPositionRange = 0.25f;        // position span at depth 100 %
inline constexpr float lfoPositionRange = 0.25f;            // +-
inline constexpr float trajPositionRange = 0.5f;
inline constexpr float maxModSemitones = 120.0f;

/// Motion scaling helper: multiplies a depth (0..1) by the Motion macro multiplier.
inline float motionScaledDepth (float depth01, float motion01) noexcept
{
    return clampf (sanitize (depth01), 0.0f, 1.0f) * motionDepthMultiplier (motion01);
}

/// Cutoff after a semitone offset applied in the log domain, clamped to 20 Hz..20 kHz. Non-finite -> base.
inline float modulatedCutoffHz (float baseHz, float offsetSemitones) noexcept
{
    baseHz = clampf (std::isfinite (baseHz) ? baseHz : 1200.0f, 20.0f, 20000.0f);
    offsetSemitones = clampf (sanitize (offsetSemitones), -maxModSemitones, maxModSemitones);
    if (offsetSemitones == 0.0f)
        return baseHz; // exact: no log/exp round trip when nothing modulates
    const float oct = std::log2 (baseHz) + offsetSemitones / 12.0f;
    return clampf (std::exp2 (oct), 20.0f, 20000.0f);
}

inline float modulatedPosition (float base01, float offset) noexcept
{
    return clampf (sanitize (base01) + sanitize (offset), 0.0f, 1.0f);
}

struct MotionOutput
{
    float cutoffHz = 1200.0f;
    float position = 0.5f;
};

/// Per-block routing constants derived from a ParamSnapshot; evaluate() is per sample.
///
///   cutoff semitones = contour * contourSt + lfo * lfoSt + traj * trajSt
///   position         = clamp(base + contour * contourPos + lfo * lfoPos + traj * trajPos, 0, 1)
///
/// Motion multiplies contour and LFO depth (and the Bite peak offset, which rides on the contour peak).
/// When the Snarl Trajectory is enabled it replaces the contour on its destination(s); it is not scaled by Motion.
struct MotionRouting
{
    float baseCutoffHz = 1200.0f;
    float basePosition = 0.5f;
    float contourSt = 0.0f, lfoSt = 0.0f, trajSt = 0.0f;
    float contourPos = 0.0f, lfoPos = 0.0f, trajPos = 0.0f;

    static MotionRouting from (const ParamSnapshot& s) noexcept
    {
        MotionRouting r;
        r.baseCutoffHz = s[P::filter_cutoff_hz];
        r.basePosition = clampf (s.percent (P::mid_table_position), 0.0f, 1.0f);

        const float motion = s.percent (P::motion);
        const float contourDepth = motionScaledDepth (s.percent (P::contour_depth), motion);
        const float lfoDepth = motionScaledDepth (s.percent (P::lfo_depth), motion);
        const float bitePeak = biteFilterPeakOffsetSemitones (s.percent (P::bite));
        const float motionMul = motionDepthMultiplier (motion);

        const bool trajOn = s.flag (P::traj_enabled);
        const int dest = s.choice (P::traj_dest); // 0 cutoff, 1 source_position, 2 both
        const bool trajCutoff = trajOn && (dest == 0 || dest == 2);
        const bool trajPosition = trajOn && (dest == 1 || dest == 2);
        const float trajDepth = clampf (s.percent (P::traj_depth), 0.0f, 1.0f);

        r.contourSt = trajCutoff ? 0.0f : motionMul * (s.percent (P::contour_depth) * contourCutoffRangeSemitones + bitePeak);
        r.contourPos = trajPosition ? 0.0f : contourDepth * contourPositionRange;
        r.lfoSt = lfoDepth * lfoCutoffRangeSemitones;
        r.lfoPos = lfoDepth * lfoPositionRange;
        r.trajSt = trajCutoff ? trajDepth * trajCutoffRangeSemitones + bitePeak : 0.0f;
        r.trajPos = trajPosition ? trajDepth * trajPositionRange : 0.0f;
        return r;
    }

    /// contour01 0..1, lfoBipolar -1..1, traj01 0..1 (use 0 when the trajectory is off).
    MotionOutput evaluate (float contour01, float lfoBipolar, float traj01) const noexcept
    {
        contour01 = clampf (sanitize (contour01), 0.0f, 1.0f);
        lfoBipolar = clampf (sanitize (lfoBipolar), -1.0f, 1.0f);
        traj01 = clampf (sanitize (traj01), 0.0f, 1.0f);
        MotionOutput o;
        o.cutoffHz = modulatedCutoffHz (baseCutoffHz, contour01 * contourSt + lfoBipolar * lfoSt + traj01 * trajSt);
        o.position = modulatedPosition (basePosition, contour01 * contourPos + lfoBipolar * lfoPos + traj01 * trajPos);
        return o;
    }
};
} // namespace snaral
