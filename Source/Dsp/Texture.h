#pragma once

// Optional mid texture layers: seeded Noise and a bounded Metal resonator bank.
// JUCE-free, header-only, real-time safe. Owner: engine-output. Spec: docs/specs/processing-output.md.
//
// API choice (documented): one TextureLayer instance PER VOICE. The voice calls noteOn(note) on each new
// note, then addTo(buf, n) to add the texture into the voice's source signal BEFORE the voice filter
// (chain: source blend + noise + metal -> SVF -> amp ADSR). A single bus-level instance also works
// (noise is note-agnostic; metal tracks the most recent noteOn).
// Texture is "disabled when Growl=0" through the growlGain input: TextureParams::from() fills it with
// growlMidMultiplier(growl) (= x^2). If the engine already multiplies the whole mid bus by that value
// after the voice, pass growlGain = 1 (set via setGrowlGain) to avoid applying it twice, but keep a
// hard 0 at Growl=0.

#include "DspUtil.h"
#include "SettlingSmoother.h"
#include "Parameters/MacroMappings.h"
#include "Parameters/ParameterTable.h"

#include <cstdint>

namespace snaral
{
struct TextureParams
{
    float noiseLevel = 0.0f;     // 0..1
    float noiseTone = 0.5f;      // 0..1 (0 dark, 1 white)
    float metalLevel = 0.0f;     // 0..1
    float metalDecayMs = 300.0f; // 10..2000, captured at noteOn
    float metalTune = 0.5f;      // 0..1, 0.5 = centre; +-12 semitones, captured at noteOn
    float growlGain = 1.0f;      // 0..1 gain input; 0 silences both layers

    static TextureParams from (const ParamSnapshot& s) noexcept
    {
        TextureParams p;
        p.noiseLevel = s.percent (P::noise_level);
        p.noiseTone = s.percent (P::noise_tone);
        p.metalLevel = s.percent (P::metal_level);
        p.metalDecayMs = s[P::metal_decay_ms];
        p.metalTune = s.percent (P::metal_tune);
        p.growlGain = growlMidMultiplier (s.percent (P::growl));
        return p;
    }
};

/// Deterministic xorshift32 white noise in [-1, 1) with a one-pole tone filter.
class NoiseGen
{
public:
    static constexpr float maxAmplitude = 0.5f;   // level 1 -> +-0.5 peak (before tone filtering)

    void prepare (double sampleRate, std::uint32_t seed) noexcept
    {
        sr_ = sampleRate;
        seed_ = seed == 0 ? 0x9E3779B9u : seed;
        gain_.prepare (sampleRate, 5.0f);
        reset();
    }
    void reset() noexcept { state_ = seed_; lp_ = 0.0f; gain_.reset (0.0f); }
    void setParams (float level, float tone, float growlGain) noexcept
    {
        level = clampf (sanitize (level), 0.0f, 1.0f);
        tone = clampf (sanitize (tone), 0.0f, 1.0f);
        gain_.setTarget (maxAmplitude * level * level * clampf (sanitize (growlGain), 0.0f, 1.0f));
        const float fc = std::min (500.0f * std::pow (36.0f, tone), 0.45f * static_cast<float> (sr_));
        a_ = tone >= 0.999f ? 1.0f : 1.0f - std::exp (-static_cast<float> (twoPi) * fc / static_cast<float> (sr_));
    }
    /// Adds noise to out. Exactly nothing is added (and the PRNG is not advanced) while the gain is 0.
    void addTo (float* out, int n) noexcept
    {
        if (gain_.isSettled() && gain_.target() == 0.0f) return;
        for (int i = 0; i < n; ++i)
        {
            state_ ^= state_ << 13; state_ ^= state_ >> 17; state_ ^= state_ << 5;
            const float w = static_cast<float> (static_cast<std::int32_t> (state_)) * (1.0f / 2147483648.0f);
            lp_ += a_ * (w - lp_);
            lp_ = flushDenormal (lp_);
            out[i] += gain_.next() * lp_;
        }
    }

private:
    double sr_ = 48000.0;
    std::uint32_t seed_ = 1, state_ = 1;
    float a_ = 1.0f, lp_ = 0.0f;
    SettlingSmoother gain_;
};

/// Up to 6 decaying sine partials at fixed inharmonic (circular-membrane mode) ratios. Each partial is a
/// damped rotation (|pole| = r < 1 for every frequency), there is no feedback between partials, and the
/// summed amplitude is normalised to <= 1, so |output| <= maxAmplitude * level always.
class MetalBank
{
public:
    static constexpr int maxPartials = 6;
    static constexpr float maxAmplitude = 0.5f;
    static constexpr float ratio[maxPartials] = { 1.0f, 1.593f, 2.136f, 2.296f, 2.653f, 2.918f };

    void prepare (double sampleRate) noexcept
    {
        sr_ = sampleRate;
        gain_.prepare (sampleRate, 5.0f);
        reset();
    }
    void reset() noexcept
    {
        for (int k = 0; k < maxPartials; ++k) { s_[k] = c_[k] = 0.0f; }
        active_ = false; env_ = 0.0f; gain_.reset (0.0f);
    }
    void setLevel (float level, float growlGain) noexcept
    {
        level = clampf (sanitize (level), 0.0f, 1.0f);
        gain_.setTarget (maxAmplitude * level * level * clampf (sanitize (growlGain), 0.0f, 1.0f));
    }

    /// Excite the bank. Base pitch = note + 24 semitones + tune offset (+-12 st); partials above 0.45 fs are muted.
    void noteOn (float midiNote, float tune01, float decayMs, float velocity = 1.0f) noexcept
    {
        midiNote = clampf (sanitize (midiNote), 0.0f, 127.0f);
        tune01 = clampf (sanitize (tune01), 0.0f, 1.0f);
        decayMs = clampf (sanitize (decayMs), 10.0f, 2000.0f);
        velocity = clampf (sanitize (velocity), 0.0f, 1.0f);
        const float f0 = midiNoteToHz (midiNote + 24.0f + (tune01 - 0.5f) * 24.0f);
        const float fMax = 0.45f * static_cast<float> (sr_);
        static constexpr float weight[maxPartials] = { 0.30f, 0.24f, 0.19f, 0.14f, 0.08f, 0.05f }; // sums to 1.0
        float slowest = 0.0f;
        for (int k = 0; k < maxPartials; ++k)
        {
            const float f = f0 * ratio[k];
            const bool ok = f < fMax;
            const double w = twoPi * static_cast<double> (ok ? f : 0.0f) / sr_;
            cw_[k] = static_cast<float> (std::cos (w)); sw_[k] = static_cast<float> (std::sin (w));
            const double dMs = static_cast<double> (decayMs) / (1.0 + 0.4 * k);   // higher partials die faster
            r_[k] = static_cast<float> (std::exp (-6.907755 / (dMs * 0.001 * sr_)));  // -60 dB at dMs
            s_[k] = 0.0f;
            c_[k] = ok ? weight[k] * velocity : 0.0f;
            slowest = std::max (slowest, ok ? r_[k] : 0.0f);
        }
        rSlow_ = slowest;
        env_ = 1.0f;
        active_ = slowest > 0.0f;
    }

    void addTo (float* out, int n) noexcept
    {
        if (! active_) return;
        for (int i = 0; i < n; ++i)
        {
            float sum = 0.0f;
            for (int k = 0; k < maxPartials; ++k)
            {
                const float s = r_[k] * (s_[k] * cw_[k] + c_[k] * sw_[k]);
                const float c = r_[k] * (c_[k] * cw_[k] - s_[k] * sw_[k]);
                s_[k] = flushDenormal (s); c_[k] = flushDenormal (c);
                sum += s_[k];
            }
            out[i] += gain_.next() * sum;
        }
        env_ *= std::pow (rSlow_, static_cast<float> (n));
        if (env_ < 1.0e-5f) { for (int k = 0; k < maxPartials; ++k) s_[k] = c_[k] = 0.0f; active_ = false; }
    }
    bool isActive() const noexcept { return active_; }

private:
    double sr_ = 48000.0;
    float s_[maxPartials] {}, c_[maxPartials] {}, cw_[maxPartials] {}, sw_[maxPartials] {}, r_[maxPartials] {};
    float rSlow_ = 0.0f, env_ = 0.0f;
    bool active_ = false;
    SettlingSmoother gain_;
};

class TextureLayer
{
public:
    void prepare (double sampleRate, std::uint32_t seed) noexcept
    {
        noise_.prepare (sampleRate, seed);
        metal_.prepare (sampleRate);
        setParams (TextureParams {});
    }
    void reset() noexcept { noise_.reset(); metal_.reset(); }
    void setParams (const TextureParams& p) noexcept
    {
        params_ = p;
        noise_.setParams (p.noiseLevel, p.noiseTone, p.growlGain);
        metal_.setLevel (p.metalLevel, p.growlGain);
    }
    void setGrowlGain (float g) noexcept { params_.growlGain = g; setParams (params_); }
    void noteOn (float midiNote, float velocity = 1.0f) noexcept
    {
        metal_.noteOn (midiNote, params_.metalTune, params_.metalDecayMs, velocity);
    }
    /// Adds noise + metal to out (out is NOT cleared). Silent (adds exact zeros) when both levels are 0.
    void addTo (float* out, int n) noexcept { noise_.addTo (out, n); metal_.addTo (out, n); }

private:
    NoiseGen noise_;
    MetalBank metal_;
    TextureParams params_;
};
} // namespace snaral
