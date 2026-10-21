// CPU measurement: 8 voices, 48 kHz, 256-sample blocks. Build/run: see docs/specs/realtime-performance.md.
#include "Engine/SnaralEngine.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <vector>
using namespace snaral;
static double run (int osMode, bool fx)
{
    SnaralEngine eng; eng.prepare (48000.0, 256);
    ParamSnapshot p; p[P::voice_mode] = 1.0f; p[P::oversampling_mode] = static_cast<float> (osMode);
    if (! fx) { p[P::fracture_mix] = 0.0f; }
    eng.setParameters (p); eng.reset(); eng.setParameters (p);
    for (int n = 0; n < 8; ++n) { EngineMidiEvent e; e.type = EngineMidiEvent::Type::NoteOn; e.data1 = static_cast<std::uint8_t> (36 + n * 3); e.data2 = 100; eng.handleMidi (e); }
    std::vector<float> l (256), r (256);
    for (int i = 0; i < 500; ++i) eng.render (l.data(), r.data(), 256);
    std::vector<double> t;
    for (int k = 0; k < 20; ++k)
    {
        const auto a = std::chrono::steady_clock::now();
        for (int i = 0; i < 1000; ++i) eng.render (l.data(), r.data(), 256);
        t.push_back (std::chrono::duration<double> (std::chrono::steady_clock::now() - a).count());
    }
    std::sort (t.begin(), t.end());
    const double budget = 1000.0 * 256 / 48000.0;
    std::printf ("os mode %d: voices %d, median %.2f%% of real time, worst-of-20 %.2f%%\n", osMode, eng.activeVoiceCount(), 100.0 * t[10] / budget, 100.0 * t.back() / budget);
    return t[10];
}
int main() { for (int m = 0; m < 3; ++m) run (m, true); }
