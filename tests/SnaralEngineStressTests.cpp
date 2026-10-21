// Randomised stress of the complete engine: 10 000 blocks of random parameters (endpoints, midpoints, random values) and
// random MIDI (notes 0..127, velocity 0..127, pedal, bend, CC120/123, transport stop, panic, host timing garbage) at block
// sizes 1..2048 and sample rates 44.1 / 48 / 88.2 / 96 / 192 kHz. Output must stay finite and bounded; the voice allocator
// must never leak a stuck voice. JUCE-free.
#include "EngineOutputTestUtil.h"
#include "EngineRig.h"

#include <cstdio>

using namespace snaral;
using namespace rig;

struct Rnd
{
    std::uint64_t s;
    explicit Rnd (std::uint64_t seed) : s (seed * 2862933555777941757ull + 3037000493ull) {}
    std::uint32_t next() { s = s * 6364136223846793005ull + 1442695040888963407ull; return static_cast<std::uint32_t> (s >> 33); }
    float unit() { return static_cast<float> (next() & 0xFFFFFF) / 16777216.0f; }
    int below (int n) { return static_cast<int> (next() % static_cast<std::uint32_t> (n)); }
};

static int pickBlockSize (Rnd& r)
{
    static const int sizes[] = { 1, 2, 7, 32, 33, 64, 100, 256, 480, 1024, 2048 };
    static const int weights[] = { 10, 5, 10, 14, 6, 14, 10, 14, 7, 6, 4 };
    int total = 0;
    for (const int w : weights) total += w;
    int x = r.below (total);
    for (std::size_t i = 0; i < 11; ++i)
    {
        if (x < weights[i]) return sizes[i];
        x -= weights[i];
    }
    return 256;
}

static void randomiseParams (Rnd& r, ParamSnapshot& p)
{
    const int changes = 1 + r.below (10);
    for (int k = 0; k < changes; ++k)
    {
        const int i = r.below (parameterCount);
        const auto& d = parameterTable[static_cast<std::size_t> (i)];
        float v;
        switch (r.below (6))
        {
            case 0: v = d.minimum; break;
            case 1: v = d.maximum; break;
            case 2: v = 0.5f * (d.minimum + d.maximum); break;
            case 3: v = d.defaultValue; break;
            default: v = d.minimum + r.unit() * (d.maximum - d.minimum); break;
        }
        if (d.type == ParamType::Choice) v = static_cast<float> (r.below (d.numChoices));
        if (d.type == ParamType::Bool) v = static_cast<float> (r.below (2));
        p.v[static_cast<std::size_t> (i)] = d.clampToDomain (v);
    }
}

int main()
{
    const double rates[] = { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 };
    const int blocksPerRate = 2000;
    double worstPeak = 0.0;
    long totalSamples = 0, totalEvents = 0;
    Rnd r (20260101);
    for (int rateIdx = 0; rateIdx < 5; ++rateIdx)
    {
        Rig rg (rates[rateIdx], 2048);
        rg.p[P::voice_mode] = 1.0f;
        std::vector<float> l (2048), rr (2048);
        for (int b = 0; b < blocksPerRate; ++b)
        {
            randomiseParams (r, rg.p);
            const int n = pickBlockSize (r);
            // host timing: valid, invalid, NaN, absurd
            HostTiming t;
            switch (r.below (5))
            {
                case 0: t.bpm = 120.0; t.bpmValid = true; break;
                case 1: t.bpm = 20.0 + 979.0 * r.unit(); t.bpmValid = true; break;
                case 2: t.bpm = std::nan (""); t.bpmValid = true; break;
                case 3: t.bpm = 1.0e9; t.bpmValid = true; break;
                default: t.bpmValid = false; break;
            }
            t.playing = r.below (2) == 0;
            t.ppqValid = r.below (4) != 0;
            t.ppqPosition = r.below (8) == 0 ? -5.0 : r.unit() * 1000.0;
            rg.eng.setHostTiming (t);
            rg.eng.setParameters (rg.p);

            // events
            std::vector<Event> ev;
            const int ne = r.below (7);
            for (int k = 0; k < ne; ++k)
            {
                EngineMidiEvent e;
                switch (r.below (12))
                {
                    case 0: case 1: case 2: e = noteOnEv (r.below (128), r.below (128)); break;
                    case 3: case 4: e = noteOffEv (r.below (128)); break;
                    case 5: e = ccEv (64, r.below (2) * 127); break;
                    case 6: e = ccEv (r.below(3) == 0 ? 120 : 123, 0); break;
                    case 7: e = bendEv (r.below (16384) - 8192); break;
                    case 8: e.type = EngineMidiEvent::Type::TransportStop; break;
                    case 9: e = ccEv (r.below (128), r.below (128)); break;
                    case 10: e.type = EngineMidiEvent::Type::AllNotesOff; break;
                    default: e = noteOnEv (24 + r.below (60), 1 + r.below (127)); break;
                }
                ev.push_back ({ r.below (n), e });
            }
            std::stable_sort (ev.begin(), ev.end(), [] (const Event& a, const Event& c) { return a.at < c.at; });
            totalEvents += static_cast<long> (ev.size());

            // render with splitting at event offsets (mirrors the processor)
            int off = 0;
            for (const auto& e : ev)
            {
                if (e.at > off) { rg.eng.render (l.data() + off, rr.data() + off, e.at - off); off = e.at; }
                rg.eng.handleMidi (e.e);
            }
            if (off < n) rg.eng.render (l.data() + off, rr.data() + off, n - off);
            if (r.below (200) == 0) rg.eng.panic();
            if (r.below (50) == 0) rg.eng.render (l.data(), nullptr, 0); // zero-length block is a no-op
            if (r.below (60) == 0) rg.eng.render (l.data(), l.data(), std::min (n, 128)); // mono alias

            for (int i = 0; i < n; ++i)
            {
                if (! std::isfinite (l[i]) || ! std::isfinite (rr[i]))
                {
                    std::fprintf (stderr, "non-finite output: rate %.0f block %d sample %d\n", rates[rateIdx], b, i);
                    return 1;
                }
                worstPeak = std::max ({ worstPeak, std::fabs (static_cast<double> (l[i])), std::fabs (static_cast<double> (rr[i])) });
            }
            totalSamples += n;
            CHECK (rg.eng.activeVoiceCount() >= 0 && rg.eng.activeVoiceCount() <= 8);
        }
        // everything released must end in silence: no stuck voice / unbounded tail
        rg.p[P::bypass] = 0.0f;
        rg.p[P::sub_amp_release_ms] = 100.0f;
        rg.p[P::mid_amp_release_ms] = 100.0f;
        rg.p[P::sub_amp_sustain] = 100.0f;
        rg.p[P::voice_mode] = 1.0f;
        rg.eng.setParameters (rg.p);
        rg.eng.handleMidi (ccEv (64, 0));
        rg.eng.handleMidi (ccEv (123, 0));
        { EngineMidiEvent ts; ts.type = EngineMidiEvent::Type::TransportStop; rg.eng.handleMidi (ts); }
        // release length is latched at note-off (up to 5 s), so allow the longest release plus margin
        const int settleBlocks = static_cast<int> (5.5 * rates[rateIdx] / 2048.0) + 1;
        for (int i = 0; i < settleBlocks && rg.eng.activeVoiceCount() > 0; ++i) rg.eng.render (l.data(), rr.data(), 2048);
        if (rg.eng.activeVoiceCount() != 0)
        {
            std::fprintf (stderr, "stuck voices after release at %.0f Hz: %d\n", rates[rateIdx], rg.eng.activeVoiceCount());
            return 1;
        }
    }
    std::printf ("stress ok: %ld samples, %ld events, 5 sample rates, worst |x| = %.3f\n", totalSamples, totalEvents, worstPeak);
    CHECK (worstPeak < 100.0);
    std::printf ("SnaralEngineStressTests: all passed\n");
    return 0;
}
