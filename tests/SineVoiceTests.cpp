#ifdef NDEBUG
#undef NDEBUG
#endif

#include "../Source/Engine/SineVoice.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>

int main()
{
    snaral::SineVoice voice;
    voice.prepare (48000.0);
    voice.noteOn (69, 1.0f);

    float peak = 0.0f;
    for (int i = 0; i < 480; ++i)
        peak = std::max (peak, std::abs (voice.renderSample()));
    assert (peak > 0.1f);
    assert (voice.isActive());

    voice.noteOff (0.01);
    float last = peak;
    for (int i = 0; i < 480; ++i)
    {
        const auto sample = voice.renderSample();
        assert (std::isfinite (sample));
        last = sample;
    }
    assert (! voice.isActive());
    assert (std::abs (last) < 1.0e-5f);

    voice.noteOn (60, 0.5f);
    assert (std::isfinite (voice.renderSample()));
    assert (voice.isActive());
    // Verify both endpoints, exact center, and pitch retention across notes.
    voice.setPitchWheel (8192);
    voice.noteOn (69, 1.0f);
    assert (std::abs (voice.frequencyHz() - 440.0) < 0.001);
    voice.setPitchWheel (0);
    assert (std::abs (12.0 * std::log2 (voice.frequencyHz() / 440.0) + 2.0) < 0.0001);
    voice.setPitchWheel (16383);
    assert (std::abs (12.0 * std::log2 (voice.frequencyHz() / 440.0) - 2.0) < 0.0001);
    voice.setPitchWheel (8192);
    assert (std::abs (voice.frequencyHz() - 440.0) < 0.001);
    voice.setPitchWheel (4096);
    voice.noteOn (60, 1.0f);
    const auto expectedHz = 440.0 * std::exp2 ((60.0 - 69.0 - 1.0) / 12.0);
    assert (std::abs (voice.frequencyHz() - expectedHz) < 0.001);
    voice.setPitchWheel (-100);
    assert (std::abs (12.0 * std::log2 (voice.frequencyHz() / 440.0) - (60.0 - 69.0 - 2.0)) < 0.0001);
    voice.setPitchWheel (20000);
    assert (std::abs (12.0 * std::log2 (voice.frequencyHz() / 440.0) - (60.0 - 69.0 + 2.0)) < 0.0001);
    std::cout << "SineVoiceTests passed\\n";
}
