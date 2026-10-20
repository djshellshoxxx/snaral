#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace snaral
{
class SineVoice
{
public:
    void prepare (double sampleRate) noexcept
    {
        sampleRate_ = std::isfinite (sampleRate) && sampleRate > 1.0 ? sampleRate : 44100.0;
        phase_ = 0.0;
        bendSemitones_ = 0.0;
        envelope_ = 0.0f;
        active_ = false;
        releaseSamplesRemaining_ = 0;
        releaseSamplesTotal_ = 0;
    }

    void noteOn (int midiNote, float velocity) noexcept
    {
        const auto note = std::clamp (midiNote, 0, 127);
        const auto safeVelocity = std::isfinite (velocity) ? std::clamp (velocity, 0.0f, 1.0f) : 0.0f;
        baseFrequency_ = 440.0 * std::exp2 ((static_cast<double> (note) - 69.0) / 12.0);
        updateFrequency();
        amplitude_ = 0.2f * safeVelocity;
        phase_ = 0.0;
        envelope_ = 1.0f;
        active_ = safeVelocity > 0.0f;
        releaseSamplesRemaining_ = 0;
        releaseSamplesTotal_ = 0;
    }

    void noteOff (double releaseSeconds) noexcept
    {
        if (! active_)
            return;

        const auto safeSeconds = std::isfinite (releaseSeconds) ? std::max (0.0, releaseSeconds) : 0.0;
        releaseSamplesTotal_ = static_cast<std::uint64_t> (safeSeconds * sampleRate_);
        releaseSamplesRemaining_ = releaseSamplesTotal_;
        if (releaseSamplesTotal_ == 0)
        {
            envelope_ = 0.0f;
            active_ = false;
        }
    }

    // MIDI pitch wheel: 0 is minimum, 8192 center, 16383 maximum.
    // Mapping uses asymmetric denominators so center is exactly unbent.
    void setPitchWheel (int value) noexcept
    {
        const auto wheel = std::clamp (value, 0, 16383);
        bendSemitones_ = wheel < 8192
                             ? 2.0 * static_cast<double> (wheel - 8192) / 8192.0
                             : 2.0 * static_cast<double> (wheel - 8192) / 8191.0;
        updateFrequency();
    }

    double frequencyHz() const noexcept { return frequency_; }

    float renderSample() noexcept
    {
        if (! active_)
            return 0.0f;

        constexpr auto twoPi = 6.283185307179586476925286766559;
        if (releaseSamplesRemaining_ > 0)
        {
            --releaseSamplesRemaining_;
            envelope_ = releaseSamplesTotal_ == 0
                            ? 0.0f
                            : static_cast<float> (releaseSamplesRemaining_) / static_cast<float> (releaseSamplesTotal_);
            if (releaseSamplesRemaining_ == 0)
                active_ = false;
        }

        const auto sample = amplitude_ * envelope_ * static_cast<float> (std::sin (phase_));
        phase_ += twoPi * frequency_ / sampleRate_;
        if (phase_ >= twoPi)
            phase_ -= twoPi;
        return sample;
    }

    bool isActive() const noexcept { return active_; }

private:
    void updateFrequency() noexcept
    {
        frequency_ = baseFrequency_ * std::exp2 (bendSemitones_ / 12.0);
    }

    double sampleRate_ = 44100.0;
    double baseFrequency_ = 440.0;
    double frequency_ = 440.0;
    double bendSemitones_ = 0.0;
    double phase_ = 0.0;
    float amplitude_ = 0.0f;
    float envelope_ = 0.0f;
    std::uint64_t releaseSamplesRemaining_ = 0;
    std::uint64_t releaseSamplesTotal_ = 0;
    bool active_ = false;
};
} // namespace snaral
