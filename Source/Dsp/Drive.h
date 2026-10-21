#pragma once

// Mid-bus drive: two bounded characters, tone, mix, static level compensation, DC blocker.
// JUCE-free, header-only, real-time safe (all buffers sized in prepare()). Owner: engine-output.
// Spec: docs/specs/processing-output.md. Mid only: the caller never routes the sub through this block.
//
// Signal path (base rate unless oversampled):
//   in -> sanitize -> pre-gain -> shaper [oversampled when an Oversampler is passed] -> tone LP -> DC blocker
//      -> level compensation -> dry/wet mix (dry delayed by the oversampler latency) -> out

#include "DspUtil.h"
#include "Oversampler.h"
#include "Parameters/MacroMappings.h"
#include "Parameters/ParameterTable.h"

#include <vector>

namespace snaral
{
enum class DriveType : int { WarmSaturation = 0, WaveFold = 1 };

struct DriveParams
{
    static constexpr float amountRangeDb = 24.0f;   // drive_amount 0..100 % -> 0..24 dB pre-gain
    static constexpr float maxTotalDb = 36.0f;      // amount + Bite offset (0..12 dB) is bounded here

    DriveType type = DriveType::WarmSaturation;
    float amountPercent = 20.0f;
    float tonePercent = 50.0f;
    float mixPercent = 100.0f;
    float biteOffsetDb = 0.0f;                      // biteDriveOffsetDb (macro), 0..12 dB

    static DriveParams from (const ParamSnapshot& s) noexcept
    {
        DriveParams p;
        p.type = s.choice (P::drive_type) == 1 ? DriveType::WaveFold : DriveType::WarmSaturation;
        p.amountPercent = s[P::drive_amount];
        p.tonePercent = s[P::drive_tone];
        p.mixPercent = s[P::drive_mix];
        p.biteOffsetDb = biteDriveOffsetDb (s.percent (P::bite));
        return p;
    }

    float totalDriveDb() const noexcept
    {
        const float a = clampf (sanitize (amountPercent), 0.0f, 100.0f) * 0.01f * amountRangeDb;
        return clampf (a + clampf (sanitize (biteOffsetDb), 0.0f, 12.0f), 0.0f, maxTotalDb);
    }
};

namespace driveshape
{
/// Smooth tanh-style saturator (rational approximation, exactly +-1 at |x| >= 3, odd => no DC).
inline float warm (float x) noexcept
{
    x = clampf (x, -3.0f, 3.0f);
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}
/// Triangle wave folder: identity for |x| <= 1, reflects beyond. Always within [-1, 1].
inline float fold (float x) noexcept
{
    x = clampf (x, -1000.0f, 1000.0f);
    const float p = 0.25f * x + 0.25f;
    return 4.0f * std::fabs (p - std::floor (p + 0.5f)) - 1.0f;
}
inline float shape (DriveType t, float x) noexcept { return t == DriveType::WaveFold ? fold (x) : warm (x); }
} // namespace driveshape

class Drive
{
public:
    /// Level compensation is referenced to a steady-state sine at this peak level (-9 dBFS).
    static constexpr float compensationRefPeak = 0.35f;
    static constexpr double compensationRefHz = 110.0;
    static constexpr float inputClamp = 64.0f;

    void prepare (double sampleRate, int maxBlockSize)
    {
        sr_ = sampleRate;
        maxBlock_ = std::max (1, maxBlockSize);
        dry_.assign (static_cast<std::size_t> (maxBlock_), 0.0f);
        dryDelay_.prepare (Oversampler::maxLatency);
        blockTau_ = static_cast<float> (std::exp (-1.0 / (0.010 * sampleRate)));  // 10 ms per-sample coefficient
        dcR_ = static_cast<float> (1.0 - twoPi * 8.0 / sampleRate);
        setParams (DriveParams {});
        reset();
    }

    void setParams (const DriveParams& p) noexcept
    {
        type_ = p.type;
        const float db = p.totalDriveDb();
        const float g = dbToGain (db);
        const float tone = clampf (sanitize (p.tonePercent), 0.0f, 100.0f) * 0.01f;
        const float fc = std::min (500.0f * std::pow (40.0f, tone), 0.45f * static_cast<float> (sr_)); // 500 Hz .. 20 kHz
        toneA_ = tone >= 0.999f ? 1.0f : 1.0f - std::exp (-static_cast<float> (twoPi) * fc / static_cast<float> (sr_));
        if (g != lastGain_ || type_ != lastType_ || toneA_ != lastToneA_)
        {
            lastGain_ = g; lastType_ = type_; lastToneA_ = toneA_;
            compTarget_ = computeCompensation (type_, g, toneA_, sr_);
        }
        gain_.tgt = g;
        comp_.tgt = compTarget_;
        mix_.tgt = clampf (sanitize (p.mixPercent), 0.0f, 100.0f) * 0.01f;
    }

    /// Snap smoothed values to targets and clear filter/delay state.
    void reset() noexcept
    {
        gain_.cur = gain_.tgt; comp_.cur = comp_.tgt; mix_.cur = mix_.tgt;
        lp_ = dcX_ = dcY_ = 0.0f;
        dryDelay_.reset();
    }

    /// Steady-state compensation: gain so that the shaped reference tone (110 Hz sine and saw at compensationRefPeak,
    /// run through the shaper and the tone one-pole) has the RMS of the unshaped one. Called from setParams()
    /// only when drive amount/type/tone change (about 1000 shaper evaluations, no allocation).
    static float computeCompensation (DriveType t, float preGain, float toneA, double sampleRate) noexcept
    {
        const int period = std::max (16, static_cast<int> (sampleRate / compensationRefHz));
        double accOut = 0.0, accIn = 0.0;
        for (int shape = 0; shape < 2; ++shape)          // sine and saw references, power-averaged
        {
            double lp = 0.0;
            for (int i = 0; i < 2 * period; ++i)
            {
                const double ph = static_cast<double> (i % period) / period;
                const double x = shape == 0 ? std::sin (twoPi * ph) : 2.0 * ph - 1.0;
                const float in = compensationRefPeak * static_cast<float> (x);
                const float y = driveshape::shape (t, preGain * in);
                lp += static_cast<double> (toneA) * (y - lp);
                if (i >= period) { accOut += lp * lp; accIn += static_cast<double> (in) * in; }
            }
        }
        return clampf (static_cast<float> (std::sqrt (accIn / std::max (accOut, 1.0e-12))), 0.125f, 8.0f);
    }

    float compensationGain() const noexcept { return compTarget_; }

    /// In place, any block size. Pass the shared Oversampler (or nullptr) to run the shaper at 2x/4x.
    /// The output is delayed by os->latencySamples() when oversampling is active (dry path is aligned).
    void process (float* io, int n, Oversampler* os = nullptr) noexcept
    {
        int lat = 0;
        if (os != nullptr) { os->commitMode(); lat = os->latencySamples(); }
        if (dryDelay_.delay() != lat) dryDelay_.setDelay (lat);
        for (int off = 0; off < n; off += maxBlock_)
            processChunk (io + off, std::min (maxBlock_, n - off), os);
    }

private:
    struct Ramp
    {
        float cur = 0.0f, tgt = 0.0f;
        float advance (int n, float perSampleCoef) noexcept
        {
            if (cur == tgt) return cur;
            float end = tgt + (cur - tgt) * std::pow (perSampleCoef, static_cast<float> (n));
            if (std::fabs (end - tgt) < 1.0e-6f * (1.0f + std::fabs (tgt))) end = tgt;
            return end;
        }
    };

    void processChunk (float* io, int n, Oversampler* os) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            const float x = clampf (sanitize (io[i]), -inputClamp, inputClamp);
            io[i] = x;
            dry_[static_cast<std::size_t> (i)] = dryDelay_.process (x);
        }
        const float g0 = gain_.cur, g1 = gain_.advance (n, blockTau_);
        const float c0 = comp_.cur, c1 = comp_.advance (n, blockTau_);
        const float m0 = mix_.cur, m1 = mix_.advance (n, blockTau_);
        const DriveType t = type_;

        auto shaper = [g0, g1, t] (float* b, int cnt) noexcept
        {
            const float step = (g1 - g0) / static_cast<float> (cnt);
            float g = g0;
            if (t == DriveType::WaveFold)
                for (int i = 0; i < cnt; ++i, g += step) b[i] = driveshape::fold (b[i] * g);
            else
                for (int i = 0; i < cnt; ++i, g += step) b[i] = driveshape::warm (b[i] * g);
        };
        if (os != nullptr) os->process (io, n, shaper); else shaper (io, n);

        const float inv = 1.0f / static_cast<float> (n);
        for (int i = 0; i < n; ++i)
        {
            const float f = static_cast<float> (i) * inv;
            lp_ += toneA_ * (io[i] - lp_);
            lp_ = flushDenormal (lp_);
            const float d = lp_ - dcX_ + dcR_ * dcY_;
            dcX_ = lp_; dcY_ = flushDenormal (d);
            const float wet = dcY_ * (c0 + (c1 - c0) * f);
            const float m = m0 + (m1 - m0) * f;
            io[i] = sanitize (dry_[static_cast<std::size_t> (i)] * (1.0f - m) + wet * m);
        }
        gain_.cur = g1; comp_.cur = c1; mix_.cur = m1;
    }

    double sr_ = 48000.0;
    int maxBlock_ = 0;
    DriveType type_ = DriveType::WarmSaturation, lastType_ = DriveType::WaveFold;
    Ramp gain_, comp_, mix_;
    float lastGain_ = -1.0f, lastToneA_ = -1.0f, compTarget_ = 1.0f, blockTau_ = 0.99f, toneA_ = 1.0f, dcR_ = 0.999f;
    float lp_ = 0.0f, dcX_ = 0.0f, dcY_ = 0.0f;
    std::vector<float> dry_;
    DelayAlign dryDelay_;
};
} // namespace snaral
