#pragma once

// Harmonic Fracture: bus-level MONO mid effect (3-band waveshaper). Spec: docs/specs/expansion-effect-harmonic-fracture.md
// JUCE-free, header-only, no allocation / locks / I/O in process(); all buffers are sized in prepare(). Owner: engine-fx.

#include "FxSvf.h"
#include "../../Parameters/ParameterTable.h"
#include "../../Engine/EngineTypes.h"

#include <vector>

namespace snaral
{
struct HarmonicFractureParams
{
    bool enabled = false;
    float crossoverHz = 500.0f;   // 120..3000 (low-mid / mid split; mid / upper-mid split at 3x)
    float driveLow = 0.25f;       // 0..1
    float driveMid = 0.35f;
    float driveHigh = 0.45f;
    float symmetry = 0.5f;        // 0..1, 0.5 = symmetric
    float tilt = 0.5f;            // 0..1, 0.5 = neutral (band blend)
    float compDb = 0.0f;          // -12..+6
    float mix = 0.5f;             // 0..1

    static HarmonicFractureParams from (const ParamSnapshot& s) noexcept
    {
        auto pct = [&] (P::Index i) { return clampf (sanitize (s[i]) * 0.01f, 0.0f, 1.0f); };
        HarmonicFractureParams p;
        p.enabled = s.flag (P::fracture_enabled);
        p.crossoverHz = clampf (sanitize (s[P::fracture_crossover_hz]), 120.0f, 3000.0f);
        p.driveLow = pct (P::fracture_drive_low);
        p.driveMid = pct (P::fracture_drive_mid);
        p.driveHigh = pct (P::fracture_drive_high);
        p.symmetry = pct (P::fracture_symmetry);
        p.tilt = pct (P::fracture_band_mix);
        p.compDb = clampf (sanitize (s[P::fracture_comp_db]), -12.0f, 6.0f);
        p.mix = pct (P::fracture_mix);
        return p;
    }
};

/// Per-band, per-sample (base rate) shaper controls handed to an external (e.g. oversampled) shaper callback.
struct FractureShapeControl
{
    const float* gain;     // pre-gain (>= 1)
    const float* amount;   // 0..1 crossfade between linear band and shaped band
    const float* bias;     // static pre-shaper offset (fold symmetry); shared by all bands
};

class HarmonicFracture
{
public:
    static constexpr int numBands = 3;
    static constexpr int controlStride = 16;
    static constexpr float minProcessHz = 120.0f;       // fracture min frequency: sub-120 Hz distortion is removed
    static constexpr int maxShaperLatency = 512;

    void prepare (double sampleRate, int maxBlockSize)
    {
        sr_ = sampleRate > 1000.0 ? sampleRate : 48000.0;
        maxBlock_ = maxBlockSize > 0 ? maxBlockSize : 1;
        const auto n = static_cast<std::size_t> (maxBlock_);
        for (int b = 0; b < numBands; ++b)
        {
            bandIn_[b].assign (n, 0.0f);
            shaped_[b].assign (n, 0.0f);
            gain_[b].assign (n, 1.0f);
            amount_[b].assign (n, 0.0f);
        }
        bias_.assign (n, 0.0f);
        for (auto& d : delay_) d.assign (delaySize, 0.0f);
        fcS_.prepare (sr_, 40.0f);
        for (auto* s : { &dLowS_, &dMidS_, &dHighS_, &biasS_ }) s->prepare (sr_, 10.0f);
        for (auto* s : { &wLowS_, &wHighS_, &mixS_, &compS_ }) s->prepare (sr_, 10.0f);
        engageS_.prepare (sr_, 15.0f);
        hpCoeff_.set (fx::tanG (minProcessHz, sr_), 1.4142135f);
        dcR_ = 1.0f - static_cast<float> (twoPi) * 15.0f / static_cast<float> (sr_);
        reset();
    }

    void reset()
    {
        resetStates();
        for (auto& d : delay_) std::fill (d.begin(), d.end(), 0.0f);
        wr_ = 0;
        fcS_.reset (p_.crossoverHz);
        dLowS_.reset (p_.driveLow); dMidS_.reset (p_.driveMid); dHighS_.reset (p_.driveHigh);
        biasS_.reset (biasFor (p_));
        wLowS_.reset (tiltLow (p_)); wHighS_.reset (tiltHigh (p_));
        mixS_.reset (p_.mix);
        compS_.reset (compFor (p_));
        engageS_.reset (engageTarget (p_));
        counter_ = 0;
        updateCoeffs();
    }

    void setParams (const HarmonicFractureParams& p)
    {
        p_ = p;
        fcS_.setTarget (p.crossoverHz);
        dLowS_.setTarget (p.driveLow); dMidS_.setTarget (p.driveMid); dHighS_.setTarget (p.driveHigh);
        biasS_.setTarget (biasFor (p));
        wLowS_.setTarget (tiltLow (p)); wHighS_.setTarget (tiltHigh (p));
        mixS_.setTarget (p.mix);
        compS_.setTarget (compFor (p));
        engageS_.setTarget (engageTarget (p));
    }

    /// Latency of an externally supplied shaper (oversampler group delay), in samples. The linear band
    /// path, the dry path and the shaper input are delayed by the same amount so wet/dry stay aligned.
    /// With the built-in shaper this is 0. Must be called outside process().
    void setShaperLatency (int samples)
    {
        samples = std::max (0, std::min (samples, maxShaperLatency));
        if (samples != shaperLatency_)
        {
            shaperLatency_ = samples;
            for (auto& d : delay_) std::fill (d.begin(), d.end(), 0.0f);
        }
    }
    int latencySamples() const noexcept { return shaperLatency_; }

    // ---- nonlinear stage (separable; pure, bounded, no state) -------------------------------------------

    /// Bounded shaper for one band. band 0 = low-mid (tanh saturation), 1 = mid (sine fold), 2 = upper-mid
    /// (triangle fold). `gain` >= 1 is the pre-gain, `amount` 0..1 blends linear (0) -> shaped (1), `bias` is the
    /// static pre-shaper offset; the response at x = 0 is exactly 0 for every bias (no static DC).
    /// |result| <= |x| + amount * (2 / sqrt(gain) + |x|).
    static float shapeSample (int band, float x, float gain, float amount, float bias) noexcept
    {
        const float u = gain * x + bias;
        float y, y0;
        switch (band)
        {
            case 0:  y = std::tanh (u); y0 = std::tanh (bias); break;
            case 1:  y = std::sin (1.5707963f * u); y0 = std::sin (1.5707963f * bias); break;
            default: y = tri (u); y0 = tri (bias); break;
        }
        const float shaped = (y - y0) / std::sqrt (gain);
        return x + amount * (shaped - x);
    }

    /// Convenience: the shaper at the current (smoothed) control values, for a quick look at the curve.
    float shape (float x, int band) const noexcept
    {
        const float d = band == 0 ? dLowS_.current() : (band == 1 ? dMidS_.current() : dHighS_.current());
        return shapeSample (band, x, gainFor (band, d), amountFor (d), biasS_.current());
    }

    // ---- processing -------------------------------------------------------------------------------------

    void process (float* io, int numSamples, const MidModulation& mod)
    {
        processWith (io, numSamples, mod, [] (int band, const float* in, float* out, int n, const FractureShapeControl& c)
        {
            for (int i = 0; i < n; ++i)
                out[i] = shapeSample (band, in[i], c.gain[i], c.amount[i], c.bias[i]);
        });
    }

    /// Same as process(), but the nonlinear stage is `shaper(band, in, out, n, ctrl)`: it must write out[0..n)
    /// from the band signal in[0..n) using the base-rate controls in ctrl (an oversampling wrapper upsamples
    /// `in`, evaluates shapeSample() at the higher rate with the held/interpolated controls, and decimates).
    /// If the callback has latency, call setShaperLatency() with it. Must not allocate.
    template <class Shaper>
    void processWith (float* io, int numSamples, const MidModulation& /*mod*/, Shaper&& shaper)
    {
        if (maxBlock_ <= 0) return;
        while (numSamples > 0)
        {
            const int n = std::min (numSamples, maxBlock_);
            processChunk (io, n, shaper);
            io += n;
            numSamples -= n;
        }
    }

private:
    static constexpr int delaySize = 1024;     // power of two > maxShaperLatency
    static constexpr float maxGain[numBands] = { 5.0f, 9.0f, 13.0f };

    static float tri (float u) noexcept
    {
        const float t = u * 0.25f + 0.25f;
        return 4.0f * std::fabs (t - std::floor (t + 0.5f)) - 1.0f;
    }
    static float gainFor (int band, float d) noexcept { return 1.0f + (maxGain[band] - 1.0f) * clampf (d, 0.0f, 1.0f); }
    static float amountFor (float d) noexcept { return clampf (6.0f * d, 0.0f, 1.0f); }
    static float biasFor (const HarmonicFractureParams& p) noexcept { return (p.symmetry - 0.5f) * 1.2f; }
    static float tiltLow (const HarmonicFractureParams& p) noexcept { return dbToGain (-6.0f * (p.tilt - 0.5f) * 2.0f); }
    static float tiltHigh (const HarmonicFractureParams& p) noexcept { return dbToGain (6.0f * (p.tilt - 0.5f) * 2.0f); }
    static float compFor (const HarmonicFractureParams& p) noexcept
    {
        const float mean = (p.driveLow + p.driveMid + p.driveHigh) * (1.0f / 3.0f);
        return dbToGain (p.compDb) / std::sqrt (1.0f + 1.2f * mean);   // auto level compensation x user trim
    }
    static float engageTarget (const HarmonicFractureParams& p) noexcept { return (p.enabled && p.mix > 0.0f) ? 1.0f : 0.0f; }

    void resetStates() noexcept
    {
        for (auto* s : { &a_, &b1_, &b2_, &c_, &d1_, &d2_, &e_, &f1_, &f2_, &hp_, &hp2_ }) s->reset();
        dcX_ = dcY_ = 0.0f;
    }

    void updateCoeffs() noexcept
    {
        const float fs = static_cast<float> (sr_);
        const float f2max = 0.42f * fs;
        const float f1 = clampf (fcS_.current(), minProcessHz, std::min (3000.0f, f2max / 1.5f));
        const float f2 = std::min (3.0f * f1, f2max);
        cx1_.set (fx::tanG (f1, sr_), 1.4142135f);
        cx2_.set (fx::tanG (f2, sr_), 1.4142135f);
    }

    float delayed (int line, float v) noexcept
    {
        if (shaperLatency_ == 0) return v;
        auto& d = delay_[line];
        d[static_cast<std::size_t> (wr_ & (delaySize - 1))] = v;
        return d[static_cast<std::size_t> ((wr_ - shaperLatency_) & (delaySize - 1))];
    }

    template <class Shaper>
    void processChunk (float* io, int n, Shaper& shaper)
    {
        const bool bypassed = engageS_.target() == 0.0f && engageS_.isSettled();
        if (bypassed)
        {
            if (! idle_) { resetStates(); idle_ = true; }
            if (shaperLatency_ > 0)       // keep the dry path aligned with the oversampled chain
                for (int i = 0; i < n; ++i) { io[i] = delayed (0, io[i]); ++wr_; }
            return;
        }
        idle_ = false;

        // Phase 1: control smoothing + 3-band split (flat-summing Linkwitz-Riley, see spec).
        for (int i = 0; i < n; ++i)
        {
            fcS_.next();
            const float dl = dLowS_.next(), dm = dMidS_.next(), dh = dHighS_.next();
            const float bias = biasS_.next();
            if (counter_ == 0) updateCoeffs();
            if (++counter_ >= controlStride) counter_ = 0;

            const float x = sanitize (io[i]);
            const auto sa = a_.process (x, cx1_);
            const float low = b1_.process (sa.lp, cx1_).lp;
            const float high1 = b2_.process (sa.hp, cx1_).hp;
            const auto sc = c_.process (high1, cx2_);
            const float mid = d1_.process (sc.lp, cx2_).lp;
            const float high = d2_.process (sc.hp, cx2_).hp;
            const auto se = e_.process (low, cx2_);                     // allpass match so the three bands sum flat
            const float lowAp = f1_.process (se.lp, cx2_).lp + f2_.process (se.hp, cx2_).hp;

            bandIn_[0][static_cast<std::size_t> (i)] = lowAp;
            bandIn_[1][static_cast<std::size_t> (i)] = mid;
            bandIn_[2][static_cast<std::size_t> (i)] = high;
            const float dv[numBands] = { dl, dm, dh };
            for (int b = 0; b < numBands; ++b)
            {
                gain_[b][static_cast<std::size_t> (i)] = gainFor (b, dv[b]);
                amount_[b][static_cast<std::size_t> (i)] = amountFor (dv[b]);
            }
            bias_[static_cast<std::size_t> (i)] = bias;
        }

        // Phase 2: nonlinear stage (built-in or externally oversampled).
        for (int b = 0; b < numBands; ++b)
        {
            const FractureShapeControl ctrl { gain_[b].data(), amount_[b].data(), bias_.data() };
            shaper (b, bandIn_[b].data(), shaped_[b].data(), n, ctrl);
        }

        // Phase 3: delta recombination, DC / sub-120 Hz removal, wet mix, compensation.
        for (int i = 0; i < n; ++i)
        {
            const auto k = static_cast<std::size_t> (i);
            const float wl = wLowS_.next(), wh = wHighS_.next(), m = mixS_.next(), comp = compS_.next();
            float e = engageS_.next();
            if (engageS_.target() == 0.0f && e < 1.0e-4f) { engageS_.reset (0.0f); e = 0.0f; }   // -80 dB: snap to exact bypass

            const float xd = delayed (0, io[i]);
            const float b0 = delayed (1, bandIn_[0][k]);
            const float b1 = delayed (2, bandIn_[1][k]);
            const float b2 = delayed (3, bandIn_[2][k]);
            ++wr_;

            const float lin = b0 + b1 + b2;
            const float dLow = wl * sanitize (shaped_[0][k] - b0);
            const float dRest = sanitize (shaped_[1][k] - b1) + wh * sanitize (shaped_[2][k] - b2);

            const float lowClean = hp2_.process (hp_.process (dLow, hpCoeff_).hp, hpCoeff_).hp;   // 4th-order HP: no distortion below 120 Hz
            const float restDc = dRest - dcX_ + dcR_ * dcY_;               // DC blocker (~15 Hz)
            dcX_ = dRest; dcY_ = flushDenormal (restDc);

            const float active = (lin + m * (lowClean + restDc)) * (1.0f + m * (comp - 1.0f));
            io[i] = xd + e * (active - xd);
        }
        if (engageS_.target() == 0.0f && engageS_.isSettled()) { resetStates(); idle_ = true; }
    }

    double sr_ = 48000.0;
    int maxBlock_ = 0;
    int shaperLatency_ = 0;
    int wr_ = 0;
    int counter_ = 0;
    bool idle_ = true;
    HarmonicFractureParams p_;
    Smoother fcS_, dLowS_, dMidS_, dHighS_, biasS_, wLowS_, wHighS_, mixS_, compS_, engageS_;
    fx::SvfCoeffs cx1_, cx2_, hpCoeff_;
    fx::SvfState a_, b1_, b2_, c_, d1_, d2_, e_, f1_, f2_, hp_, hp2_;
    float dcX_ = 0.0f, dcY_ = 0.0f, dcR_ = 0.998f;
    std::vector<float> bandIn_[numBands], shaped_[numBands], gain_[numBands], amount_[numBands], bias_;
    std::vector<float> delay_[4];
};
} // namespace snaral
