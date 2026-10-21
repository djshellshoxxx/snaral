#ifdef NDEBUG
#undef NDEBUG
#endif
#include "Dsp/Adsr.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

using snaral::Adsr;

static Adsr make (double sr, float a, float d, float s, float r)
{
    Adsr e;
    e.prepare (sr);
    e.setParams ({ a, d, s, r });
    return e;
}

int main()
{
    const double sr = 48000.0;
    const int Na = 480, Nd = 960, Nr = 1440;

    // --- exact trajectory: attack, decay, sustain (stricter than "within one sample")
    {
        Adsr e = make (sr, 10.0f, 20.0f, 0.5f, 30.0f);
        assert (! e.isActive() && e.next() == 0.0f);
        e.noteOn();
        for (int i = 0; i < Na; ++i)
            assert (std::fabs (e.next() - static_cast<float> (i + 1) / Na) < 1e-6f);
        assert (e.level() == 1.0f);                                   // peak at index Na-1
        for (int k = 1; k <= Nd; ++k)
            assert (std::fabs (e.next() - (1.0f - 0.5f * static_cast<float> (k) / Nd)) < 1e-6f);
        assert (e.stage() == Adsr::Stage::Sustain && e.level() == 0.5f);
        for (int i = 0; i < 1000; ++i) assert (e.next() == 0.5f);

        // release from sustain: linear to exactly 0 at Nr
        e.noteOff();
        float prev = 0.5f;
        for (int k = 1; k <= Nr; ++k)
        {
            const float v = e.next();
            assert (std::fabs (v - 0.5f * (1.0f - static_cast<float> (k) / Nr)) < 1e-6f);
            assert (v <= prev);
            prev = v;
        }
        assert (e.level() == 0.0f && e.stage() == Adsr::Stage::Idle && ! e.isActive());
        assert (e.next() == 0.0f);
    }

    // --- timing independent of sample rate: 44.1k / 96k hit the rounded sample counts
    for (const double r : { 44100.0, 96000.0 })
    {
        Adsr e = make (r, 10.0f, 20.0f, 0.5f, 30.0f);
        const int na = static_cast<int> (std::lround (0.010 * r));
        e.noteOn();
        for (int i = 0; i < na - 1; ++i) assert (e.next() < 1.0f);
        assert (e.next() == 1.0f);
        e.noteOff();
        const int nr = static_cast<int> (std::lround (0.030 * r));
        for (int i = 0; i < nr - 1; ++i) { e.next(); assert (e.isActive()); }
        assert (e.next() == 0.0f && ! e.isActive());
    }

    // --- noteOff mid-attack and mid-decay releases from the current level, no jump
    {
        Adsr e = make (sr, 10.0f, 20.0f, 0.5f, 30.0f);
        e.noteOn();
        float v = 0;
        for (int i = 0; i < 240; ++i) v = e.next();
        assert (std::fabs (v - 0.5f) < 1e-6f);
        e.noteOff();
        const float first = e.next();
        assert (first < v && v - first < 1.0f / Nr + 1e-6f);
        // decay case
        Adsr d = make (sr, 0.0f, 20.0f, 0.2f, 30.0f);
        d.noteOn();
        for (int i = 0; i < 300; ++i) v = d.next();
        d.noteOff();
        const float f2 = d.next();
        assert (f2 < v && v - f2 < 1.0f / Nr + 1e-6f);
    }

    // --- retrigger/voice reuse: starts from the CURRENT level, continuous, monotone rise, never jumps down
    {
        Adsr e = make (sr, 10.0f, 20.0f, 0.5f, 30.0f);
        e.noteOn();
        for (int i = 0; i < 2000; ++i) e.next();
        e.noteOff();
        float prev = 0;
        for (int i = 0; i < 700; ++i) prev = e.next();
        assert (prev > 0.0f && e.isReleasing());
        e.noteOn();
        for (int i = 0; i < Na; ++i)
        {
            const float v = e.next();
            assert (v > prev && v - prev <= (1.0f - 0.0f) / Na + 1e-6f + 0.0f);
            prev = v;
        }
        assert (prev == 1.0f);
    }

    // --- attack 0 ms: first sample is 1; velocity attack scale hook
    {
        Adsr e = make (sr, 0.0f, 20.0f, 0.5f, 30.0f);
        e.noteOn();
        assert (e.next() == 1.0f);
        Adsr h = make (sr, 10.0f, 20.0f, 1.0f, 30.0f);
        h.noteOn (0.5f);
        for (int i = 0; i < 239; ++i) assert (h.next() < 1.0f);
        assert (h.next() == 1.0f);                                    // 240 samples
        Adsr z = make (sr, 10.0f, 20.0f, 1.0f, 30.0f);
        z.noteOn (0.0f);
        assert (z.next() == 1.0f);
        Adsr n = make (sr, 10.0f, 20.0f, 1.0f, 30.0f);
        n.noteOn (1.0f);                                              // default scale == no effect
        for (int i = 0; i < Na - 1; ++i) assert (n.next() < 1.0f);
        assert (n.next() == 1.0f);
    }

    // --- kill / forceRelease / ignored noteOff
    {
        Adsr e = make (sr, 0.0f, 20.0f, 1.0f, 30.0f);
        e.noteOff();
        assert (! e.isActive());
        e.noteOn(); e.next();
        e.forceRelease (240);
        for (int i = 0; i < 239; ++i) assert (e.next() > 0.0f);
        assert (e.next() == 0.0f && ! e.isActive());
        e.noteOn(); e.next(); e.kill();
        assert (e.level() == 0.0f && ! e.isActive());
        // noteOff in release does not restart the release
        e.noteOn(); for (int i = 0; i < 100; ++i) e.next();
        e.noteOff(); const float a = e.next(); e.noteOff(); const float b = e.next();
        assert (b < a);
    }

    // --- sustain change while held slews, no step larger than 1/(5 ms)
    {
        Adsr e = make (sr, 0.0f, 1.0f, 1.0f, 30.0f);
        e.noteOn();
        for (int i = 0; i < 100; ++i) e.next();
        assert (e.stage() == Adsr::Stage::Sustain);
        e.setParams ({ 0.0f, 1.0f, 0.2f, 30.0f });
        float prev = e.level();
        int steps = 0;
        while (e.level() != 0.2f && steps < 10000)
        {
            const float v = e.next();
            assert (std::fabs (v - prev) <= 1.0f / 240.0f + 1e-6f);
            prev = v;
            ++steps;
        }
        assert (e.level() == 0.2f && steps >= 150 && steps <= 200);
    }

    // --- release reaches zero within release + one block, for blocks of any size and every stage
    for (const int block : { 1, 64, 256, 1024 })
        for (const int stageSamples : { 5, 300, 700, 3000 })
        {
            Adsr e = make (sr, 10.0f, 20.0f, 0.6f, 30.0f);
            std::vector<float> buf (static_cast<std::size_t> (block));
            e.noteOn();
            int done = 0;
            while (done < stageSamples) { e.process (buf.data(), block); done += block; }
            e.noteOff();
            int elapsed = 0;
            while (e.isActive()) { e.process (buf.data(), block); elapsed += block; assert (elapsed < 10 * Nr); }
            assert (elapsed <= Nr + block);
            for (float f : buf) assert (f >= 0.0f);
        }

    // --- fuzz: random events, random params; output always finite and in [0,1]; always returns to idle
    {
        Adsr e;
        e.prepare (sr);
        std::uint32_t s = 12345u;
        auto rnd = [&s] { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; };
        for (int i = 0; i < 400000; ++i)
        {
            const auto r = rnd() % 4000;
            if (r == 0) e.noteOn (static_cast<float> (rnd() % 101) / 100.0f);
            else if (r == 1) e.noteOff();
            else if (r == 2) e.kill();
            else if (r == 3) e.forceRelease (static_cast<int> (rnd() % 500));
            else if (r == 4)
                e.setParams ({ static_cast<float> (rnd() % 501), 1.0f + static_cast<float> (rnd() % 5000),
                               static_cast<float> (rnd() % 101) / 100.0f, 1.0f + static_cast<float> (rnd() % 5000) });
            const float v = e.next();
            assert (std::isfinite (v) && v >= 0.0f && v <= 1.0f);
        }
        e.noteOff();
        for (int i = 0; i < 5000 * 48 + 10; ++i) e.next();
        assert (e.level() == 0.0f);
    }

    // --- NaN / out of range params are sanitised
    {
        Adsr e;
        e.prepare (sr);
        e.setParams ({ NAN, INFINITY, -3.0f, 1.0e9f });
        e.noteOn();
        for (int i = 0; i < 100; ++i) { const float v = e.next(); assert (std::isfinite (v) && v >= 0.0f && v <= 1.0f); }
    }

    std::cout << "AdsrTests passed\n";
    return 0;
}
