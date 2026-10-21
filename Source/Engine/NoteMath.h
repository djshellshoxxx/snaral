#pragma once

// Pitch / velocity math for the voice layer. JUCE-free, header-only, real-time safe. Owner: engine-voice.
//
// Pitch chain (all values are fractional MIDI note numbers = semitones):
//   p0 = note + transpose_semitones                       (transpose before everything else)
//   p1 = glide(p0)                                        (PitchGlide, linear in semitones = exponential in Hz)
//   p2 = p1 + bendSemitones(bend14, pitch_bend_range)     (bend AFTER transpose/glide, smoothed by the engine)
//   hz = A4 * 2^((p2 + layerCents/100 - 69) / 12)         (per-layer fine tune LAST)
// Glide applies to p0 only; bend and layer cents are never glided.

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace snaral
{
inline constexpr float bendSmoothMs = 3.0f;  // recommended Smoother time for the bend value (engine)

inline double clampd (double x, double lo, double hi) noexcept { return x < lo ? lo : (x > hi ? hi : x); }

/// Fractional MIDI note (semitones) -> Hz at reference A4. Double precision (exact to far better than 0.1 cent).
inline double semitonesToHz (double midiNote, double a4Hz = 440.0) noexcept
{
    return a4Hz * std::exp2 ((midiNote - 69.0) / 12.0);
}

/// Pitch of a MIDI note after global transpose, in semitones.
inline double transposedPitch (int note, double transposeSemitones) noexcept
{
    return static_cast<double> (note) + transposeSemitones;
}

/// Final frequency of a layer: global tune + transpose already in 'pitchSemitones' arg chain, layer cents last.
inline double layerHz (double pitchSemitones, double a4Hz, double layerCents) noexcept
{
    return semitonesToHz (pitchSemitones + layerCents * 0.01, a4Hz);
}

/// Convenience: note -> Hz with master tune, transpose, layer cents (no glide, no bend).
inline double noteToHz (int note, double a4Hz = 440.0, double transposeSemitones = 0.0, double layerCents = 0.0) noexcept
{
    return layerHz (transposedPitch (note, transposeSemitones), a4Hz, layerCents);
}

/// 14-bit bend (-8192..8191) -> semitones. -8192 -> -range, 0 -> 0, 8191 -> +range (asymmetric normalisation so both
/// endpoints are exactly +-range).
inline double bendSemitones (int bend14, double rangeSemitones) noexcept
{
    const int v = std::clamp (bend14, -8192, 8191);
    const double n = v < 0 ? static_cast<double> (v) / 8192.0 : static_cast<double> (v) / 8191.0;
    return n * clampd (rangeSemitones, 1.0, 12.0);
}

/// Velocity 1..127 (0 treated as 1) -> 0..1.
inline float normalizedVelocity (int velocity) noexcept
{
    return static_cast<float> (std::clamp (velocity, 1, 127) - 1) / 126.0f;
}

/// Gain 1 - a*(1-v). amountPercent 0 (default) -> exactly 1 for every velocity. Monotonic non-decreasing in velocity.
inline float velocityToLevelGain (int velocity, float amountPercent) noexcept
{
    const float a = std::clamp (amountPercent * 0.01f, 0.0f, 1.0f);
    return 1.0f - a * (1.0f - normalizedVelocity (velocity));
}

/// Attack-time multiplier 1 - a*v (harder = faster). amountPercent 0 -> exactly 1. Monotonic non-increasing in velocity.
/// Feed to Adsr::noteOn(attackScale).
inline float velocityToAttackScale (int velocity, float amountPercent) noexcept
{
    const float a = std::clamp (amountPercent * 0.01f, 0.0f, 1.0f);
    return 1.0f - a * normalizedVelocity (velocity);
}

/// Glide in the semitone domain. Linear in semitones over exactly glide_ms (whole samples) = exponential frequency
/// trajectory. glide_ms <= 0 or glide disallowed -> snap. Re-targeting mid-glide restarts from the current pitch.
class PitchGlide
{
public:
    void prepare (double sampleRate) noexcept { sr_ = sampleRate > 0.0 ? sampleRate : 48000.0; }
    void reset (double semitones) noexcept { cur_ = target_ = semitones; remaining_ = 0; step_ = 0.0; }

    void setTarget (double semitones, float glideMs, bool allowGlide) noexcept
    {
        target_ = semitones;
        if (! allowGlide || ! (glideMs > 0.0f))
        {
            cur_ = target_;
            remaining_ = 0;
            return;
        }
        remaining_ = std::max (1, static_cast<int> (std::lround (static_cast<double> (glideMs) * 0.001 * sr_)));
        step_ = (target_ - cur_) / remaining_;
    }

    double next() noexcept
    {
        if (remaining_ > 0)
        {
            cur_ += step_;
            if (--remaining_ == 0) cur_ = target_;
        }
        return cur_;
    }

    double current() const noexcept { return cur_; }
    double target() const noexcept { return target_; }
    bool isGliding() const noexcept { return remaining_ > 0; }

private:
    double sr_ = 48000.0, cur_ = 0.0, target_ = 0.0, step_ = 0.0;
    int remaining_ = 0;
};
} // namespace snaral
