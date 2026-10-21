#pragma once

// Nonlinear-stage oversampler: Off / 2x / 4x using cascaded linear-phase polyphase halfband FIR stages.
// JUCE-free, header-only. All memory is allocated in prepare(); process() never allocates, locks or throws.
// Design, latency and measured numbers: docs/specs/expansion-oversampling.md. Owner: engine-output.
//
// Usage:   os.prepare (sr, maxBlock);  os.setMode (OversamplingMode::X2);
//          os.process (io, n, [] (float* buf, int count) { /* in-place nonlinear op at count = n * factor */ });
// Mode changes requested with setMode() take effect at the next commitMode()/process() call (a block boundary).
// They clear filter state (notes are not reset) and hold the last output sample while the new path warms up
// (2 * latency + 4 samples), then crossfade to it over fadeSamples (128 = 2.7 ms at 48 kHz) to avoid a click.

#include "DspUtil.h"

#include <cstring>
#include <vector>

namespace snaral
{
enum class OversamplingMode : int { Off = 0, X2 = 1, X4 = 2 };

inline constexpr int oversamplingFactor (OversamplingMode m) noexcept
{
    return m == OversamplingMode::X4 ? 4 : (m == OversamplingMode::X2 ? 2 : 1);
}

namespace detail
{
inline double besselI0 (double x) noexcept
{
    double sum = 1.0, term = 1.0;
    const double q = 0.25 * x * x;
    for (int k = 1; k < 60; ++k)
    {
        term *= q / (static_cast<double> (k) * k);
        sum += term;
        if (term < 1.0e-14 * sum) break;
    }
    return sum;
}

/// One 2x halfband stage. Taps: Kaiser-windowed sinc, length N = 4K+3, centre c = 2K+1 (odd),
/// h[c] = 0.5 and h[c +- even] = 0. up(): zero-stuff + filter (gain 2). down(): filter + decimate.
/// Group delay: up = c + pad, down = c high-rate samples (pad = optional extra high-rate delay, even).
class HalfbandStage
{
public:
    void design (int K, int padHighRate, double beta)
    {
        K_ = K;
        pad_ = padHighRate & ~1;
        d_ = pad_ / 2;
        const int N = 4 * K + 3, c = 2 * K + 1;
        std::vector<double> h (static_cast<std::size_t> (N), 0.0);
        const double i0b = besselI0 (beta);
        double oddSum = 0.0;
        for (int i = 0; i < N; ++i)
        {
            const int t = i - c;
            if (t == 0) { h[static_cast<std::size_t> (i)] = 0.5; continue; }
            if ((t & 1) == 0) continue;
            const double r = static_cast<double> (t) / static_cast<double> (c + 1);
            const double w = besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / i0b;
            const double v = std::sin (0.5 * pi * t) / (pi * t) * w;
            h[static_cast<std::size_t> (i)] = v;
            oddSum += v;
        }
        const double scale = 0.5 / oddSum; // non-centre taps sum to exactly 0.5 => unity DC gain
        he_.assign (static_cast<std::size_t> (2 * K + 2), 0.0f);
        g_.assign (static_cast<std::size_t> (2 * K + 2), 0.0f);
        for (int k = 0; k < 2 * K + 2; ++k)
        {
            const double v = h[static_cast<std::size_t> (2 * k)] * scale;
            he_[static_cast<std::size_t> (k)] = static_cast<float> (v);
            g_[static_cast<std::size_t> (k)] = static_cast<float> (2.0 * v);
        }
        histLen_ = 2 * K + 2 + d_;
    }

    void prepare (int maxLowBlock)
    {
        maxLow_ = maxLowBlock;
        const auto sz = static_cast<std::size_t> (histLen_ + maxLowBlock);
        upExt_.assign (sz, 0.0f); evExt_.assign (sz, 0.0f); odExt_.assign (sz, 0.0f);
    }

    void reset() noexcept
    {
        std::fill (upExt_.begin(), upExt_.end(), 0.0f);
        std::fill (evExt_.begin(), evExt_.end(), 0.0f);
        std::fill (odExt_.begin(), odExt_.end(), 0.0f);
    }

    int centre() const noexcept { return 2 * K_ + 1; }
    int totalDelayHighRate() const noexcept { return 2 * centre() + pad_; }
    const std::vector<float>& taps() const noexcept { return he_; } // even-index taps h[2k], k = 0..2K+1
    int K() const noexcept { return K_; }

    /// in: n samples (low rate) -> out: 2n samples.
    void up (const float* in, int n, float* out) noexcept
    {
        float* ext = upExt_.data();
        std::memcpy (ext + histLen_, in, static_cast<std::size_t> (n) * sizeof (float));
        const int K2 = 2 * K_ + 1;
        const float* g = g_.data();
        for (int m = 0; m < n; ++m)
        {
            const float* x = ext + histLen_ + m - d_;
            float acc = 0.0f;
            for (int k = 0; k <= K_; ++k)
                acc += g[k] * (x[-k] + x[-(K2 - k)]);
            out[2 * m] = acc;
            out[2 * m + 1] = x[-K_];
        }
        std::memmove (ext, ext + n, static_cast<std::size_t> (histLen_) * sizeof (float));
    }

    /// in: 2n samples (high rate) -> out: n samples.
    void down (const float* in, int n, float* out) noexcept
    {
        float* ev = evExt_.data() + histLen_;
        float* od = odExt_.data() + histLen_;
        for (int m = 0; m < n; ++m) { ev[m] = in[2 * m]; od[m] = in[2 * m + 1]; }
        const int K2 = 2 * K_ + 1;
        const float* h = he_.data();
        for (int m = 0; m < n; ++m)
        {
            const float* e = ev + m;
            float acc = 0.5f * od[m - K_ - 1];
            for (int k = 0; k <= K_; ++k)
                acc += h[k] * (e[-k] + e[-(K2 - k)]);
            out[m] = acc;
        }
        std::memmove (evExt_.data(), evExt_.data() + n, static_cast<std::size_t> (histLen_) * sizeof (float));
        std::memmove (odExt_.data(), odExt_.data() + n, static_cast<std::size_t> (histLen_) * sizeof (float));
    }

private:
    int K_ = 0, pad_ = 0, d_ = 0, histLen_ = 0, maxLow_ = 0;
    std::vector<float> he_, g_, upExt_, evExt_, odExt_;
};
} // namespace detail

/// Integer delay line for dry/wet alignment (bounded, preallocated). delay 0 is a pass-through.
class DelayAlign
{
public:
    void prepare (int maxDelay)
    {
        int size = 1;
        while (size < maxDelay + 2) size <<= 1;
        buf_.assign (static_cast<std::size_t> (size), 0.0f);
        mask_ = size - 1; maxDelay_ = maxDelay; pos_ = 0; delay_ = 0;
    }
    void reset() noexcept { std::fill (buf_.begin(), buf_.end(), 0.0f); pos_ = 0; }
    /// Changing the delay clears the line (do it together with an oversampling mode change).
    void setDelay (int d) noexcept { d = d < 0 ? 0 : (d > maxDelay_ ? maxDelay_ : d); if (d != delay_) { delay_ = d; reset(); } }
    int delay() const noexcept { return delay_; }
    float process (float x) noexcept
    {
        if (delay_ == 0) return x;
        buf_[static_cast<std::size_t> (pos_)] = x;
        const float y = buf_[static_cast<std::size_t> ((pos_ - delay_) & mask_)];
        pos_ = (pos_ + 1) & mask_;
        return y;
    }

private:
    std::vector<float> buf_;
    int mask_ = 0, maxDelay_ = 0, pos_ = 0, delay_ = 0;
};

class Oversampler
{
public:
    // Fixed filter design (see expansion-oversampling.md).
    static constexpr int stage1K = 11;      // 47 taps, runs at 2x
    static constexpr int stage2K = 4;       // 19 taps, runs at 4x
    static constexpr int stage2Pad = 2;     // makes the 4x total an exact integer number of base-rate samples
    static constexpr double kaiserBeta = 8.0;
    static constexpr int fadeSamples = 128;
    static constexpr int maxLatency = 32;

    /// Exact latency in base-rate samples for a mode (output vs input of an identity nonlinearity).
    static constexpr int latencyFor (OversamplingMode m) noexcept
    {
        constexpr int s1 = (2 * (2 * stage1K + 1)) / 2;                       // 23
        constexpr int s2 = (2 * (2 * stage2K + 1) + stage2Pad) / 4;           // 5
        return m == OversamplingMode::Off ? 0 : (m == OversamplingMode::X2 ? s1 : s1 + s2);
    }

    void prepare (double /*sampleRate*/, int maxBlockSize)
    {
        maxBlock_ = std::max (1, maxBlockSize);
        s1_.design (stage1K, 0, kaiserBeta);
        s2_.design (stage2K, stage2Pad, kaiserBeta);
        s1_.prepare (maxBlock_);
        s2_.prepare (2 * maxBlock_);
        buf2_.assign (static_cast<std::size_t> (2 * maxBlock_), 0.0f);
        buf4_.assign (static_cast<std::size_t> (4 * maxBlock_), 0.0f);
        reset();
    }

    void reset() noexcept
    {
        s1_.reset(); s2_.reset();
        std::fill (buf2_.begin(), buf2_.end(), 0.0f);
        std::fill (buf4_.begin(), buf4_.end(), 0.0f);
        fadeLeft_ = 0;
    }

    /// Request a mode; applied at the next commitMode()/process() (block boundary).
    void setMode (OversamplingMode m) noexcept { pending_ = m; }
    void commitMode() noexcept
    {
        if (pending_ == mode_) return;
        mode_ = pending_;
        reset();
        fadeLeft_ = fadeSamples + 2 * latencyFor (mode_) + 4;   // hold during filter warm-up, then ramp
        held_ = lastOut_;   // crossfade from the last output sample, so there is no step at the switch
    }
    OversamplingMode mode() const noexcept { return mode_; }
    int factor() const noexcept { return oversamplingFactor (mode_); }
    int latencySamples() const noexcept { return latencyFor (mode_); }

    /// In-place: io[0..n) -> upsample -> fn(float* buf, int count) at count = n * factor -> downsample.
    /// Off mode calls fn(io, n) directly (zero latency).
    template <class F>
    void process (float* io, int n, F&& fn)
    {
        commitMode();
        int done = 0;
        while (done < n)
        {
            const int chunk = std::min (n - done, maxBlock_);
            float* p = io + done;
            switch (mode_)
            {
                case OversamplingMode::Off:
                    fn (p, chunk);
                    break;
                case OversamplingMode::X2:
                    s1_.up (p, chunk, buf2_.data());
                    fn (buf2_.data(), 2 * chunk);
                    s1_.down (buf2_.data(), chunk, p);
                    break;
                case OversamplingMode::X4:
                    s1_.up (p, chunk, buf2_.data());
                    s2_.up (buf2_.data(), 2 * chunk, buf4_.data());
                    fn (buf4_.data(), 4 * chunk);
                    s2_.down (buf4_.data(), 2 * chunk, buf2_.data());
                    s1_.down (buf2_.data(), chunk, p);
                    break;
            }
            if (fadeLeft_ > 0)
            {
                for (int i = 0; i < chunk && fadeLeft_ > 0; ++i, --fadeLeft_)
                {
                    const float f = fadeLeft_ >= fadeSamples ? 0.0f : 1.0f - static_cast<float> (fadeLeft_) / static_cast<float> (fadeSamples);
                    p[i] = held_ + (p[i] - held_) * f;
                }
            }
            if (chunk > 0) lastOut_ = p[chunk - 1];
            done += chunk;
        }
    }

    const detail::HalfbandStage& stage1() const noexcept { return s1_; }
    const detail::HalfbandStage& stage2() const noexcept { return s2_; }

private:
    detail::HalfbandStage s1_, s2_;
    std::vector<float> buf2_, buf4_;
    OversamplingMode mode_ = OversamplingMode::Off, pending_ = OversamplingMode::Off;
    int maxBlock_ = 0, fadeLeft_ = 0;
    float held_ = 0.0f, lastOut_ = 0.0f;
};
} // namespace snaral
