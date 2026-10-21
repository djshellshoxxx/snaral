#pragma once

// Formant Furnace: bus-level MONO mid effect. Spec: docs/specs/expansion-effect-formant-furnace.md
// JUCE-free, header-only, no allocation / locks / I/O in process(). Owner: engine-fx.

#include "FxSvf.h"
#include "../../Parameters/MacroMappings.h"
#include "../../Parameters/ParameterTable.h"
#include "../../Engine/EngineTypes.h"

namespace snaral
{
struct FormantFurnaceParams
{
    bool enabled = false;
    float vowel = 0.25f;      // 0..1 position along the vowel table
    float spacing = 0.5f;     // 0..1 (0.5 = natural spacing)
    float resonance = 0.4f;   // 0..1
    float envAmount = 0.0f;   // 0..1 contour -> peak bank shift
    float lfoAmount = 0.0f;   // 0..1 LFO -> peak bank shift
    float mix = 0.5f;         // 0..1

    static FormantFurnaceParams from (const ParamSnapshot& s) noexcept
    {
        auto pct = [&] (P::Index i) { return clampf (sanitize (s[i]) * 0.01f, 0.0f, 1.0f); };
        FormantFurnaceParams p;
        p.enabled = s.flag (P::furnace_enabled);
        p.vowel = pct (P::furnace_vowel);
        p.spacing = pct (P::furnace_spacing);
        p.resonance = pct (P::furnace_resonance);
        // macro-mapping.md: Motion multiplies the Furnace modulation depths (Motion 0 removes them).
        const float motion = motionDepthMultiplier (s.percent (P::motion));
        p.envAmount = pct (P::furnace_env_amount) * motion;
        p.lfoAmount = pct (P::furnace_lfo_amount) * motion;
        p.mix = pct (P::furnace_mix);
        return p;
    }
};

class FormantFurnace
{
public:
    static constexpr int numPeaks = 4;
    static constexpr int controlStride = 16;     // samples between coefficient updates
    static constexpr float maxPeakQ = 18.0f;     // hard cap, far below self-oscillation (TPT BP is stable for any Q)
    /// Documented worst-case linear gain bound of the wet path (see spec).
    static constexpr float wetGainBound = 7.3f;

    void prepare (double sampleRate, int /*maxBlockSize*/)
    {
        sr_ = sampleRate > 1000.0 ? sampleRate : 48000.0;
        for (auto* s : { &vowelS_, &spacingS_, &resoS_, &shiftS_ }) s->prepare (sr_, 12.0f);
        mixS_.prepare (sr_, 15.0f);
        reset();
    }

    void reset()
    {
        for (auto& s : st_) s.reset();
        vowelS_.reset (p_.vowel);
        spacingS_.reset (p_.spacing);
        resoS_.reset (p_.resonance);
        shiftS_.reset (0.0f);
        mixS_.reset (p_.enabled ? p_.mix : 0.0f);
        counter_ = 0;
        idle_ = true;
        updateCoeffs();
    }

    void setParams (const FormantFurnaceParams& p)
    {
        p_ = p;
        vowelS_.setTarget (p.vowel);
        spacingS_.setTarget (p.spacing);
        resoS_.setTarget (p.resonance);
        mixS_.setTarget (p.enabled ? p.mix : 0.0f);
    }

    int latencySamples() const noexcept { return 0; }

    /// Mono mid bus, in place. Exact bypass (io untouched) once the mix has settled at 0.
    void process (float* io, int numSamples, const MidModulation& mod)
    {
        if (numSamples <= 0) return;
        const float shiftTarget = clampf (p_.envAmount * clampf (sanitize (mod.contour), 0.0f, 1.0f) * 1.5f
                                              + p_.lfoAmount * clampf (sanitize (mod.lfo), -1.0f, 1.0f),
                                          -2.0f, 2.0f);
        shiftS_.setTarget (shiftTarget);

        if (mixS_.target() == 0.0f && mixS_.isSettled())
        {
            if (! idle_) { for (auto& s : st_) s.reset(); idle_ = true; }
            return;
        }
        idle_ = false;

        for (int i = 0; i < numSamples; ++i)
        {
            vowelS_.next(); spacingS_.next(); resoS_.next(); shiftS_.next();
            float m = mixS_.next();
            if (mixS_.target() == 0.0f && m < 1.0e-4f) { mixS_.reset (0.0f); m = 0.0f; }     // -80 dB: snap to exact bypass
            if (counter_ == 0) updateCoeffs();
            if (++counter_ >= controlStride) counter_ = 0;

            const float x = io[i];
            const float xs = sanitize (x);
            float wet = dryTap * xs;
            for (int k = 0; k < numPeaks; ++k)
                wet += weight[k] * cf_[k].k * st_[k].process (xs, cf_[k]).bp;   // k * bp = unit-peak band-pass
            wet *= comp_;
            io[i] = x + m * (wet - x);
        }
        if (mixS_.target() == 0.0f && mixS_.isSettled())
        {
            for (auto& s : st_) s.reset();
            idle_ = true;
        }
    }

    /// Peak centre frequencies (Hz) for the current smoothed state; exposed for tests/UI.
    void currentPeakHz (float* out4) const noexcept
    {
        peakFrequencies (vowelS_.current(), spacingS_.current(), shiftS_.current(), out4);
    }

    /// Vowel table order is smooth in F1/F2 (u-o-a-e-i), 5 entries x 4 formants (Hz).
    static void peakFrequencies (float vowel01, float spacing01, float shiftOct, float* out4) noexcept
    {
        static constexpr float table[5][4] = {
            { 300.0f,  870.0f, 2240.0f, 3300.0f },   // u
            { 570.0f,  840.0f, 2410.0f, 3350.0f },   // o
            { 730.0f, 1090.0f, 2440.0f, 3400.0f },   // a
            { 530.0f, 1840.0f, 2480.0f, 3450.0f },   // e
            { 270.0f, 2290.0f, 3010.0f, 3500.0f } }; // i
        const float pos = clampf (vowel01, 0.0f, 1.0f) * 4.0f;
        const int i0 = pos >= 4.0f ? 3 : static_cast<int> (pos);
        const float fr = pos - static_cast<float> (i0);
        float f[4];
        for (int k = 0; k < 4; ++k)
            f[k] = std::exp (std::log (table[i0][k]) * (1.0f - fr) + std::log (table[i0 + 1][k]) * fr);
        const float centre = std::sqrt (f[0] * f[2]);
        const float s = 0.4f + 1.2f * clampf (spacing01, 0.0f, 1.0f);
        const float shift = std::exp2 (clampf (shiftOct, -2.0f, 2.0f));
        for (int k = 0; k < 4; ++k)
            out4[k] = centre * std::pow (f[k] / centre, s) * shift;
    }

private:
    static constexpr float dryTap = 0.5f;
    static constexpr float weight[numPeaks] = { 1.0f, 0.85f, 0.6f, 0.35f };
    static constexpr float qScale[numPeaks] = { 1.0f, 1.0f, 0.8f, 0.7f };

    void updateCoeffs() noexcept
    {
        float f[numPeaks];
        peakFrequencies (vowelS_.current(), spacingS_.current(), shiftS_.current(), f);
        const float r = clampf (resoS_.current(), 0.0f, 1.0f);
        const float q0 = 2.0f + (maxPeakQ - 2.0f) * r * std::sqrt (r);
        const float fMax = std::min (0.45f * static_cast<float> (sr_), 12000.0f);
        float p = dryTap * dryTap;
        for (int k = 0; k < numPeaks; ++k)
        {
            const float fk = clampf (f[k], 80.0f, fMax);
            const float q = std::max (q0 * qScale[k], 1.0f);
            cf_[k].set (fx::tanG (fk, sr_), 1.0f / q);
            p += weight[k] * weight[k] * 0.22f / q;      // pink-noise power through a constant-Q band-pass
        }
        comp_ = clampf (1.0f / std::sqrt (p), 0.7f, 2.2f);
    }

    double sr_ = 48000.0;
    FormantFurnaceParams p_;
    Smoother vowelS_, spacingS_, resoS_, shiftS_, mixS_;
    fx::SvfCoeffs cf_[numPeaks];
    fx::SvfState st_[numPeaks];
    float comp_ = 1.0f;
    int counter_ = 0;
    bool idle_ = true;
};
} // namespace snaral
