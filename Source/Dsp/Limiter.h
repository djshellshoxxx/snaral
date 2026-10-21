#pragma once

// Lookahead peak-safety limiter + output trim. JUCE-free, header-only, real-time safe. Owner: engine-output.
// Spec: docs/specs/processing-output.md. This is a safety stage, not a loudness maximiser.
//
// Algorithm (stereo-linked):
//  1. Detector: per sample, max(|x|, cubic 4-point mid-sample estimates of both adjacent segments) over
//     both channels ("true-ish" inter-sample peak). The detector looks at index j = n - 2.
//  2. Required gain g = min(1, ceiling / peak), then instant-attack / exponential-release r <= g.
//  3. Sliding minimum of r over L + 1 samples, then a boxcar mean over L samples (L = 1 ms lookahead).
//     Each averaged term is <= g[n - L], so the applied gain never exceeds the gain required for the sample
//     that is output at that time (the guarantee behind "output <= ceiling").
//  4. Audio is delayed by D = L + 2 samples (latencySamples()); applied gain = 1 + (G - 1) * engage.
//  Enable/bypass moves `engage` over 5 ms; the delay and the gain tracker keep running while bypassed, so the
//  latency never changes and re-enabling is click-free.

#include "DspUtil.h"
#include "SettlingSmoother.h"
#include "Parameters/ParameterTable.h"

#include <cstdint>
#include <vector>

namespace snaral
{
struct LimiterParams
{
    bool enabled = true;
    float ceilingDb = -1.0f;

    static LimiterParams from (const ParamSnapshot& s) noexcept
    {
        return { s.flag (P::limiter_enabled), s[P::limiter_ceiling_db] };
    }
};

class Limiter
{
public:
    static constexpr float lookaheadMs = 1.0f;
    static constexpr float releaseMs = 80.0f;
    static constexpr float engageMs = 5.0f;
    static constexpr float minCeilingDb = -12.0f, maxCeilingDb = 0.0f;
    static constexpr float safetyClampMarginDb = 0.05f;   // last-resort clamp above the ceiling (counted)

    void prepare (double sampleRate, int /*maxBlockSize*/)
    {
        sr_ = sampleRate;
        L_ = std::max (2, static_cast<int> (std::ceil (lookaheadMs * 0.001 * sampleRate)));
        D_ = L_ + 2;
        int size = 1;
        while (size < D_ + 8) size <<= 1;
        audioL_.assign (static_cast<std::size_t> (size), 0.0f);
        audioR_.assign (static_cast<std::size_t> (size), 0.0f);
        audioMask_ = size - 1;
        int q = 1;
        while (q < L_ + 4) q <<= 1;
        dqVal_.assign (static_cast<std::size_t> (q), 1.0f);
        dqIdx_.assign (static_cast<std::size_t> (q), 0);
        dqMask_ = q - 1;
        boxcar_.assign (static_cast<std::size_t> (L_), 1.0f);
        relCoef_ = static_cast<float> (1.0 - std::exp (-1.0 / (0.001 * releaseMs * sampleRate)));
        engage_.prepare (sampleRate, engageMs);
        setParams (LimiterParams {});
        reset();
    }

    void setParams (const LimiterParams& p) noexcept
    {
        enabled_ = p.enabled;
        ceilingDb_ = clampf (sanitize (p.ceilingDb), minCeilingDb, maxCeilingDb);
        ceilLin_ = dbToGain (ceilingDb_);
        clampLin_ = dbToGain (ceilingDb_ + safetyClampMarginDb);
        engage_.setTarget (enabled_ ? 1.0f : 0.0f);
    }

    void reset() noexcept
    {
        std::fill (audioL_.begin(), audioL_.end(), 0.0f);
        std::fill (audioR_.begin(), audioR_.end(), 0.0f);
        std::fill (boxcar_.begin(), boxcar_.end(), 1.0f);
        boxSum_ = static_cast<double> (L_);
        boxPos_ = 0; sinceResum_ = 0;
        dqHead_ = dqTail_ = 0; t_ = 0;
        rel_ = 1.0f; relDeficit_ = 0.0f; pos_ = 0;
        engage_.reset (enabled_ ? 1.0f : 0.0f);
        clampHits_ = 0; lastGain_ = 1.0f;
    }

    int latencySamples() const noexcept { return D_; }
    float currentGain() const noexcept { return lastGain_; }             // applied limiter gain (1 = none)
    float currentReductionDb() const noexcept { return gainToDb (lastGain_); }
    std::uint64_t safetyClampHits() const noexcept { return clampHits_; }
    bool isEngaged() const noexcept { return engage_.target() >= 0.5f; }

    /// Stereo in place. right may be nullptr or alias left (mono): then only left is processed.
    void process (float* left, float* right, int n) noexcept
    {
        const bool stereo = right != nullptr && right != left;
        for (int i = 0; i < n; ++i)
        {
            const float xl = clampf (sanitize (left[i]), -1.0e6f, 1.0e6f);
            const float xr = stereo ? clampf (sanitize (right[i]), -1.0e6f, 1.0e6f) : xl;
            audioL_[idx (0)] = xl;
            audioR_[idx (0)] = xr;

            // detector at j = n - 2
            float peak = std::max (std::fabs (at (audioL_, 2)), std::fabs (at (audioR_, 2)));
            peak = std::max (peak, midPeak (audioL_));
            if (stereo) peak = std::max (peak, midPeak (audioR_));
            const float g = peak > ceilLin_ ? ceilLin_ / peak : 1.0f;

            // release works on the gain DEFICIT (1 - r) so it decays to exactly 0 (no float stall near 1)
            relDeficit_ *= 1.0f - relCoef_;
            if (relDeficit_ < 1.0e-6f) relDeficit_ = 0.0f;
            relDeficit_ = std::max (relDeficit_, 1.0f - g);
            rel_ = std::min (g, 1.0f - relDeficit_);

            const float m = slidingMin (rel_);
            // boxcar mean over L
            boxSum_ += static_cast<double> (m) - static_cast<double> (boxcar_[static_cast<std::size_t> (boxPos_)]);
            boxcar_[static_cast<std::size_t> (boxPos_)] = m;
            if (++boxPos_ >= L_) boxPos_ = 0;
            if (++sinceResum_ >= L_)
            {
                sinceResum_ = 0;
                double s = 0.0;
                for (const float v : boxcar_) s += static_cast<double> (v);
                boxSum_ = s;
            }
            float G = static_cast<float> (boxSum_ / static_cast<double> (L_));
            if (G > 1.0f - 1.0e-7f) G = 1.0f;
            if (G < 0.0f) G = 0.0f;
            lastGain_ = G;

            const float e = engage_.next();
            const float applied = 1.0f + (G - 1.0f) * e;
            float yl = at (audioL_, D_) * applied;
            float yr = at (audioR_, D_) * applied;
            if (e >= 1.0f)
            {
                if (std::fabs (yl) > clampLin_ || std::fabs (yr) > clampLin_) ++clampHits_;
                yl = clampf (yl, -clampLin_, clampLin_);
                yr = clampf (yr, -clampLin_, clampLin_);
            }
            left[i] = flushDenormal (yl);
            if (stereo) right[i] = flushDenormal (yr);
            pos_ = (pos_ + 1) & audioMask_;
        }
    }

private:
    std::size_t idx (int back) const noexcept { return static_cast<std::size_t> ((pos_ - back) & audioMask_); }
    float at (const std::vector<float>& b, int back) const noexcept { return b[idx (back)]; }

    // Catmull-Rom mid-sample estimates for the segments (j-1, j) and (j, j+1), j = n-2.
    float midPeak (const std::vector<float>& b) const noexcept
    {
        const float x0 = at (b, 4), x1 = at (b, 3), x2 = at (b, 2), x3 = at (b, 1), x4 = at (b, 0);
        const float a = (-x0 + 9.0f * x1 + 9.0f * x2 - x3) * (1.0f / 16.0f);
        const float c = (-x1 + 9.0f * x2 + 9.0f * x3 - x4) * (1.0f / 16.0f);
        return std::max (std::fabs (a), std::fabs (c));
    }

    float slidingMin (float v) noexcept
    {
        const auto m = static_cast<std::size_t> (dqMask_);
        while (dqTail_ != dqHead_ && dqVal_[(dqTail_ - 1) & m] >= v) --dqTail_;
        dqVal_[dqTail_ & m] = v; dqIdx_[dqTail_ & m] = t_;
        ++dqTail_;
        while (dqIdx_[dqHead_ & m] <= t_ - (L_ + 1)) ++dqHead_;
        ++t_;
        return dqVal_[dqHead_ & m];
    }

    double sr_ = 48000.0;
    int L_ = 48, D_ = 50, audioMask_ = 0, dqMask_ = 0, pos_ = 0, boxPos_ = 0, sinceResum_ = 0;
    std::size_t dqHead_ = 0, dqTail_ = 0;
    std::int64_t t_ = 0;
    std::vector<float> audioL_, audioR_, dqVal_, boxcar_;
    std::vector<std::int64_t> dqIdx_;
    double boxSum_ = 0.0;
    float relCoef_ = 0.001f, rel_ = 1.0f, relDeficit_ = 0.0f, ceilingDb_ = -1.0f, ceilLin_ = 0.89f, clampLin_ = 0.9f, lastGain_ = 1.0f;
    bool enabled_ = true;
    std::uint64_t clampHits_ = 0;
    SettlingSmoother engage_;
};

/// Output trim (-24..+12 dB) with 20 ms smoothing; settles to the exact target gain (no stuck state).
class OutputTrim
{
public:
    static constexpr float minDb = -24.0f, maxDb = 12.0f;

    void prepare (double sampleRate, float smoothMs = 20.0f) noexcept
    {
        gain_.prepare (sampleRate, smoothMs);
        setTrimDb (0.0f);
        gain_.reset (1.0f);
    }
    void setTrimDb (float db) noexcept { gain_.setTarget (dbToGain (clampf (sanitize (db), minDb, maxDb))); }
    void reset() noexcept { gain_.reset (gain_.target()); }
    bool isSettled() const noexcept { return gain_.isSettled(); }
    float currentGain() const noexcept { return gain_.current(); }

    /// right may be nullptr or alias left.
    void process (float* left, float* right, int n) noexcept
    {
        const bool stereo = right != nullptr && right != left;
        for (int i = 0; i < n; ++i)
        {
            const float g = gain_.next();
            left[i] *= g;
            if (stereo) right[i] *= g;
        }
    }

private:
    SettlingSmoother gain_;
};
} // namespace snaral
