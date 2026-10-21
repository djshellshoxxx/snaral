#pragma once

// Standalone-only reference tone: 440 Hz sine, default -12 dBFS peak (docs/standards: AUDIO_APP_BASELINE section 5,
// docs/specs/cdl-theme-options.md), explicit toggle only, click-free 20 ms fades. JUCE-free, header-only.
//
// The tone is NEVER enabled automatically. In Plugin mode setEnabled(true) is refused unless the object was
// constructed with allowInPlugin = true (reserved for a future approved feature).
//
// Threading: setEnabled/stop/setLevelDb from the UI thread (atomics); prepare/process on the audio thread.

#include <algorithm>
#include <atomic>
#include <cmath>

namespace snaral
{
class ReferenceTone
{
public:
    enum class Mode { Standalone, Plugin };

    static constexpr float defaultLevelDb = -12.0f;
    static constexpr double frequencyHz = 440.0;
    static constexpr double fadeSeconds = 0.02;

    explicit ReferenceTone (Mode mode, bool allowInPlugin = false) noexcept
        : allowed_ (mode == Mode::Standalone || allowInPlugin) {}

    /// Returns false (and stays off) when refused.
    bool setEnabled (bool on) noexcept
    {
        if (on && ! allowed_)
            return false;
        enabled_.store (on, std::memory_order_release);
        return true;
    }
    bool toggle() noexcept { setEnabled (! enabled()); return enabled(); }
    void stop() noexcept { enabled_.store (false, std::memory_order_release); } // Stop/Panic
    bool enabled() const noexcept { return enabled_.load (std::memory_order_acquire); }
    bool allowed() const noexcept { return allowed_; }

    /// Peak level in dBFS, clamped to -60..0.
    void setLevelDb (float db) noexcept { levelDb_.store (std::isfinite (db) ? std::clamp (db, -60.0f, 0.0f) : defaultLevelDb); }
    float levelDb() const noexcept { return levelDb_.load(); }

    /// Audio thread. True while any tone is audible (fading in, steady or fading out).
    bool active() const noexcept { return gain_ > 0.0f || enabled(); }

    void prepare (double sampleRate) noexcept
    {
        sr_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        phaseInc_ = 2.0 * 3.14159265358979323846 * frequencyHz / sr_;
        fadeStep_ = static_cast<float> (1.0 / (fadeSeconds * sr_));
        phase_ = 0.0;
        gain_ = 0.0f;
    }

    /// Adds the tone to both channels (right may alias left: then it is added once). No allocation.
    void process (float* left, float* right, int numSamples) noexcept
    {
        const bool on = enabled();
        if (! on && gain_ <= 0.0f)
            return;
        const float amp = std::pow (10.0f, levelDb_.load() / 20.0f);
        for (int i = 0; i < numSamples; ++i)
        {
            gain_ = on ? std::min (1.0f, gain_ + fadeStep_) : std::max (0.0f, gain_ - fadeStep_);
            // raised-cosine shaped fade from the linear ramp
            const float g = 0.5f - 0.5f * std::cos (gain_ * 3.14159265f);
            const float s = amp * g * static_cast<float> (std::sin (phase_));
            phase_ += phaseInc_;
            if (phase_ > 6.283185307179586) phase_ -= 6.283185307179586;
            left[i] += s;
            if (right != left)
                right[i] += s;
        }
    }

private:
    bool allowed_;
    std::atomic<bool> enabled_ { false };
    std::atomic<float> levelDb_ { defaultLevelDb };
    double sr_ = 48000.0, phaseInc_ = 2.0 * 3.14159265358979323846 * 440.0 / 48000.0, phase_ = 0.0;
    float fadeStep_ = 1.0f / 960.0f, gain_ = 0.0f;
};
} // namespace snaral
