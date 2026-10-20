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
    std::cout << "SineVoiceTests passed\n";
}
