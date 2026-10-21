#pragma once

// Mid-only stereo widener. Mono mid in -> stereo out. JUCE-free, header-only, real-time safe.
// Owner: engine-output. Spec: docs/specs/processing-output.md.
//
// Design (mid/side recombination with a decorrelated side):
//   side = Decorrelate(HP(mid)),  L = c * (mid + w * side),  R = c * (mid - w * side)
// Decorrelate = 4 first-order allpass sections (|H| = 1, different phase turns) + a short delay
// (about 0.4 ms). HP (one pole, ~200 Hz) keeps the low end mono. w = widthPercent / 100 (0..1.2).
// c = 1 / sqrt(1 + 0.5 w^2) is a loudness compensation (wide settings do not get louder).
// Mono fold-down: (L + R) / 2 = c * mid exactly, so no polarity inversion can cancel the mid; the worst
// case (w = 1.2) retains c^2 = 0.58, i.e. -2.4 dB, far above the -20 dB requirement.
// w = 0 gives L == R == mid bit-exactly. The sub is NOT routed through here: the caller sums it
// identically into L and R after this block. Space changes are smoothed over 20 ms (mono <-> wide crossfade).

#include "DspUtil.h"
#include "SettlingSmoother.h"
#include "Parameters/MacroMappings.h"
#include "Parameters/ParameterTable.h"

#include <vector>

namespace snaral
{
struct WidthParams
{
    float widthPercent = 0.0f; // 0..120

    static WidthParams from (const ParamSnapshot& s) noexcept
    {
        return { spaceWidthPercent (s.percent (P::space)) };
    }
};

class Width
{
public:
    static constexpr float maxWidthPercent = 120.0f;

    void prepare (double sampleRate, int /*maxBlockSize*/)
    {
        sr_ = sampleRate;
        delaySamples_ = std::max (1, static_cast<int> (std::lround (0.0004 * sampleRate)));
        int size = 1;
        while (size < delaySamples_ + 2) size <<= 1;
        delayBuf_.assign (static_cast<std::size_t> (size), 0.0f);
        mask_ = size - 1;
        hpA_ = static_cast<float> (std::exp (-twoPi * 200.0 / sampleRate));
        width_.prepare (sampleRate, 20.0f);
        setParams (WidthParams {});
        reset();
    }

    void setParams (const WidthParams& p) noexcept
    {
        width_.setTarget (clampf (sanitize (p.widthPercent), 0.0f, maxWidthPercent) * 0.01f);
    }

    void reset() noexcept
    {
        width_.reset (width_.target());
        hpX_ = hpY_ = 0.0f;
        for (auto& z : apZ_) z = 0.0f;
        std::fill (delayBuf_.begin(), delayBuf_.end(), 0.0f);
        pos_ = 0;
    }

    /// mid -> (left, right). left/right may alias mid (in-place on the left channel); n samples.
    void process (const float* mid, float* left, float* right, int n) noexcept
    {
        static constexpr float coef[4] = { 0.62f, -0.47f, 0.83f, -0.71f };
        for (int i = 0; i < n; ++i)
        {
            const float m = sanitize (mid[i]);
            // one-pole high-pass
            hpY_ = hpA_ * (hpY_ + m - hpX_);
            hpX_ = m;
            hpY_ = flushDenormal (hpY_);
            float v = hpY_;
            for (int k = 0; k < 4; ++k)               // y = a x + z; z = x - a y   (allpass, a in (-1,1))
            {
                const float y = coef[k] * v + apZ_[k];
                apZ_[k] = flushDenormal (v - coef[k] * y);
                v = y;
            }
            delayBuf_[static_cast<std::size_t> (pos_)] = v;
            const float side = delayBuf_[static_cast<std::size_t> ((pos_ - delaySamples_) & mask_)];
            pos_ = (pos_ + 1) & mask_;

            const float w = width_.next();
            const float c = 1.0f / std::sqrt (1.0f + 0.5f * w * w);
            const float ws = w * side;
            left[i] = c * (m + ws);
            right[i] = c * (m - ws);
        }
    }

    float currentWidth() const noexcept { return width_.current(); }

private:
    double sr_ = 48000.0;
    int delaySamples_ = 1, mask_ = 0, pos_ = 0;
    float hpA_ = 0.97f, hpX_ = 0.0f, hpY_ = 0.0f;
    float apZ_[4] {};
    std::vector<float> delayBuf_;
    SettlingSmoother width_;
};
} // namespace snaral
