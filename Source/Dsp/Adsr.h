#pragma once

// Per-layer amplitude ADSR. JUCE-free, header-only, real-time safe (no allocation, no locks).
// Owner: engine-voice.
//
// Trajectory (all stages linear in amplitude, counted in whole samples; N = max(1, round(ms * sr / 1000))):
//   Attack : out[i] = s0 + (1 - s0) * (i + 1) / Na         i = 0..Na-1, s0 = level at noteOn (0 when idle)
//            The peak (1.0) is output at sample index Na-1. Retrigger/reuse starts from the CURRENT level (no click).
//            Attack of 0 ms is Na = 1: the first sample is already 1.0 (an intentional hard onset).
//   Decay  : out[k] = 1 + (S - 1) * k / Nd                  k = 1..Nd, S = sustain at the moment decay began.
//            Decay time is always the full decay_ms regardless of sustain depth. out[Nd] == S exactly.
//   Sustain: holds S. If sustain is changed while held the level slews to the new value at 1/(5 ms) full-scale
//            per second (no zipper step); a note-off during the slew releases from the current level.
//   Release: out[k] = r0 * (1 - k / Nr)                     k = 1..Nr, r0 = level at noteOff (any stage).
//            Reaches exactly 0 at k = Nr and the envelope becomes Idle. Release duration is constant (release_ms)
//            independent of r0.
// noteOff in Attack/Decay releases from the current level (no jump). noteOff while Idle or Release is ignored.
// forceRelease(n) = release with an explicit length (used for the 5 ms de-click on Kill); kill() = hard zero.
// velocity-to-attack hook: noteOn(attackScale) multiplies attack_ms (see NoteMath.h velocityToAttackScale).
// Parameter changes apply to stages that have not started yet (attack length is latched at noteOn,
// release length at noteOff).

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace snaral
{
class Adsr
{
public:
    enum class Stage : std::uint8_t { Idle, Attack, Decay, Sustain, Release };

    struct Params
    {
        float attackMs = 2.0f;    // 0..500
        float decayMs = 300.0f;   // 1..5000
        float sustain = 1.0f;     // 0..1 (linear amplitude)
        float releaseMs = 120.0f; // 1..5000
    };

    void prepare (double sampleRate) noexcept
    {
        sr_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        slewStep_ = static_cast<float> (1.0 / (0.005 * sr_));
        kill();
    }

    void reset() noexcept { kill(); }

    void setParams (const Params& p) noexcept
    {
        p_.attackMs = sane (p.attackMs, 0.0f, 500.0f, 0.0f);
        p_.decayMs = sane (p.decayMs, 1.0f, 5000.0f, 1.0f);
        p_.sustain = sane (p.sustain, 0.0f, 1.0f, 1.0f);
        p_.releaseMs = sane (p.releaseMs, 1.0f, 5000.0f, 1.0f);
    }

    void noteOn (float attackScale = 1.0f) noexcept
    {
        const float sc = sane (attackScale, 0.0f, 1.0f, 1.0f);
        start_ = level_;
        total_ = msToSamples (p_.attackMs * sc);
        count_ = 0;
        stage_ = Stage::Attack;
    }

    void noteOff() noexcept
    {
        if (stage_ == Stage::Idle || stage_ == Stage::Release) return;
        beginRelease (msToSamples (p_.releaseMs));
    }

    /// Release with an explicit length in samples (>=1), whatever the stage. Used for the de-click kill.
    void forceRelease (int samples) noexcept
    {
        if (stage_ == Stage::Idle) return;
        beginRelease (std::max (1, samples));
    }

    /// Immediate silence and Idle (voice reuse after a hard kill).
    void kill() noexcept
    {
        level_ = 0.0f;
        stage_ = Stage::Idle;
        count_ = total_ = 0;
    }

    float next() noexcept
    {
        switch (stage_)
        {
            case Stage::Idle: return 0.0f;
            case Stage::Attack:
                ++count_;
                if (count_ >= total_)
                {
                    level_ = 1.0f;
                    stage_ = Stage::Decay;
                    start_ = 1.0f;
                    target_ = p_.sustain;
                    total_ = msToSamples (p_.decayMs);
                    count_ = 0;
                }
                else
                    level_ = start_ + (1.0f - start_) * (static_cast<float> (count_) / static_cast<float> (total_));
                return level_;
            case Stage::Decay:
                ++count_;
                if (count_ >= total_)
                {
                    level_ = target_;
                    stage_ = Stage::Sustain;
                }
                else
                    level_ = start_ + (target_ - start_) * (static_cast<float> (count_) / static_cast<float> (total_));
                return level_;
            case Stage::Sustain:
                if (level_ != p_.sustain)
                {
                    const float d = p_.sustain - level_;
                    level_ = std::fabs (d) <= slewStep_ ? p_.sustain : level_ + (d > 0 ? slewStep_ : -slewStep_);
                }
                return level_;
            case Stage::Release:
                ++count_;
                if (count_ >= total_)
                {
                    level_ = 0.0f;
                    stage_ = Stage::Idle;
                }
                else
                    level_ = start_ * (1.0f - static_cast<float> (count_) / static_cast<float> (total_));
                return level_;
        }
        return 0.0f;
    }

    void process (float* out, int n) noexcept
    {
        for (int i = 0; i < n; ++i) out[i] = next();
    }

    float level() const noexcept { return level_; }
    Stage stage() const noexcept { return stage_; }
    bool isActive() const noexcept { return stage_ != Stage::Idle; }
    bool isReleasing() const noexcept { return stage_ == Stage::Release; }

    int msToSamples (float ms) const noexcept
    {
        return std::max (1, static_cast<int> (std::lround (static_cast<double> (ms) * 0.001 * sr_)));
    }

private:
    static float sane (float x, float lo, float hi, float fallback) noexcept
    {
        return std::isfinite (x) ? std::min (hi, std::max (lo, x)) : fallback;
    }

    void beginRelease (int samples) noexcept
    {
        start_ = level_;
        total_ = samples;
        count_ = 0;
        stage_ = Stage::Release;
        if (level_ <= 0.0f) { level_ = 0.0f; stage_ = Stage::Idle; }
    }

    Params p_ {};
    double sr_ = 48000.0;
    float level_ = 0.0f, start_ = 0.0f, target_ = 1.0f, slewStep_ = 0.004f;
    int count_ = 0, total_ = 0;
    Stage stage_ = Stage::Idle;
};
} // namespace snaral
