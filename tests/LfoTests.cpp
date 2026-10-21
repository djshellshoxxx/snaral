// LFO boundary tests (spec: motion-filter.md, LFO contract + acceptance 3, 4, 5).
#include "SynthTestUtil.h"

#include "Dsp/Lfo.h"

#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <vector>

using namespace snaral;

namespace
{
HostTiming timing (double bpm, bool valid = true)
{
    HostTiming t;
    t.bpm = bpm;
    t.bpmValid = valid;
    return t;
}

// Duration (seconds) of `cycles` consecutive sine cycles measured between rising zero crossings
// with linear interpolation of the crossing position (sub-sample accuracy).
double measureCycleSeconds (Lfo& lfo, double sr, double expectedSeconds, int cycles = 2)
{
    lfo.reset();
    std::vector<double> crossings;
    float prev = lfo.process();
    const long maxSamples = static_cast<long> ((cycles + 2) * expectedSeconds * sr) + 10;
    for (long n = 1; n < maxSamples && static_cast<int> (crossings.size()) < cycles + 1; ++n)
    {
        const float v = lfo.process();
        if (prev < 0.0f && v >= 0.0f)
            crossings.push_back (static_cast<double> (n - 1) + static_cast<double> (-prev) / (static_cast<double> (v) - prev));
        prev = v;
    }
    CHECK (static_cast<int> (crossings.size()) == cycles + 1);
    return (crossings.back() - crossings.front()) / cycles / sr;
}

void testWaveformShapes()
{
    CHECK_NEAR (lfoWaveform (LfoWave::Sine, 0.0), 0.0, 1e-9);
    CHECK_NEAR (lfoWaveform (LfoWave::Sine, 0.25), 1.0, 1e-7);
    CHECK_NEAR (lfoWaveform (LfoWave::Sine, 0.75), -1.0, 1e-7);
    CHECK_NEAR (lfoWaveform (LfoWave::Triangle, 0.0), 0.0, 1e-7);
    CHECK_NEAR (lfoWaveform (LfoWave::Triangle, 0.25), 1.0, 1e-7);
    CHECK_NEAR (lfoWaveform (LfoWave::Triangle, 0.5), 0.0, 1e-7);
    CHECK_NEAR (lfoWaveform (LfoWave::Triangle, 0.75), -1.0, 1e-7);
    CHECK_NEAR (lfoWaveform (LfoWave::Saw, 0.0), -1.0, 1e-9);
    CHECK_NEAR (lfoWaveform (LfoWave::Saw, 0.5), 0.0, 1e-9);
    CHECK_NEAR (lfoWaveform (LfoWave::Saw, 0.999999), 1.0, 1e-5);
    CHECK (lfoWaveform (LfoWave::Pulse, 0.0) == 1.0f && lfoWaveform (LfoWave::Pulse, 0.49) == 1.0f);
    CHECK (lfoWaveform (LfoWave::Pulse, 0.5) == -1.0f && lfoWaveform (LfoWave::Pulse, 0.99) == -1.0f);
    for (int w = 0; w < 4; ++w)
        for (double ph = 0.0; ph < 1.0; ph += 0.001)
        {
            const float v = lfoWaveform (static_cast<LfoWave> (w), ph);
            CHECK (v >= -1.0001f && v <= 1.0001f);
        }
    std::cout << "waveform shapes OK\n";
}

void testDivisionTable()
{
    const double expectBeats[18] = { 4, 6, 8.0 / 3, 2, 3, 4.0 / 3, 1, 1.5, 2.0 / 3, 0.5, 0.75, 1.0 / 3, 0.25, 0.375, 1.0 / 6, 0.125, 0.1875, 1.0 / 12 };
    for (int d = 0; d < 18; ++d)
        CHECK_NEAR (lfoDivisionBeats (d), expectBeats[d], 1e-12);
    // ordering matches the lfo_sync_division choices exactly
    const auto& def = paramDef (P::lfo_sync_division);
    CHECK (def.numChoices == 18);
    const char* names[18] = { "1/1", "1/1d", "1/1t", "1/2", "1/2d", "1/2t", "1/4", "1/4d", "1/4t", "1/8", "1/8d", "1/8t", "1/16", "1/16d", "1/16t", "1/32", "1/32d", "1/32t" };
    for (int i = 0; i < 18; ++i)
        CHECK (def.choices[static_cast<std::size_t> (i)] == names[i]);
    CHECK (lfoDivisionBeats (-4) == lfoDivisionBeats (0) && lfoDivisionBeats (99) == lfoDivisionBeats (17));
    std::cout << "division table + choice ordering OK\n";
}

void testSyncedCycleDuration()
{
    double worstErrMs = 0.0;
    for (const double sr : { 44100.0, 48000.0 })
    {
        Lfo lfo;
        lfo.prepare (sr);
        for (const double bpm : { 60.0, 120.0, 180.0 })
            for (int d = 0; d < 18; ++d)
            {
                LfoParams p;
                p.sync = true;
                p.division = d;
                p.wave = LfoWave::Sine;
                p.depth01 = 1.0f;
                lfo.setParams (p);
                lfo.setHostTiming (timing (bpm));
                CHECK (! lfo.usingFallbackRate());
                const double expected = lfoDivisionBeats (d) * 60.0 / bpm;
                const double measured = measureCycleSeconds (lfo, sr, expected, 2);
                const double tol = std::max (0.001, 1.0 / sr);
                worstErrMs = std::max (worstErrMs, std::fabs (measured - expected) * 1000.0);
                CHECK_NEAR (measured, expected, tol);
            }
    }
    std::cout << "sync cycle duration: 18 divisions x 60/120/180 BPM x 2 sample rates, worst error " << worstErrMs << " ms (limit 1 ms)\n";
}

void testFreeCycleDuration()
{
    double worstPct = 0.0;
    for (const float hz : { 0.02f, 0.1f, 0.5f, 1.0f, 2.0f, 7.3f, 20.0f })
    {
        Lfo lfo;
        lfo.prepare (48000.0);
        LfoParams p;
        p.sync = false;
        p.rateHz = hz;
        p.wave = LfoWave::Sine;
        lfo.setParams (p);
        const double measured = measureCycleSeconds (lfo, 48000.0, 1.0 / hz, 2);
        const double errPct = std::fabs (measured * hz - 1.0) * 100.0;
        worstPct = std::max (worstPct, errPct);
        CHECK (errPct < 0.1);
    }
    // sync mode ignores the free rate when tempo is valid, free mode ignores tempo
    Lfo lfo;
    lfo.prepare (48000.0);
    LfoParams p;
    p.sync = false; p.rateHz = 3.0f;
    lfo.setParams (p);
    lfo.setHostTiming (timing (120.0));
    CHECK_NEAR (lfo.currentRateHz(), 3.0, 1e-9);
    CHECK (! lfo.usingFallbackRate());
    std::cout << "free cycle duration worst error " << worstPct << " % (limit 0.1 %)\n";
}

void testPhaseOffsetAndRetrigger()
{
    for (const int w : { 0, 1, 2, 3 })
        for (const float deg : { 0.0f, 90.0f, 180.0f, 270.0f, 360.0f })
        {
            Lfo lfo;
            lfo.prepare (48000.0);
            LfoParams p;
            p.wave = static_cast<LfoWave> (w);
            p.phaseDegrees = deg;
            lfo.setParams (p);
            lfo.reset();
            double ph = static_cast<double> (deg) / 360.0;
            ph -= std::floor (ph);
            CHECK_NEAR (lfo.rawValue(), lfoWaveform (p.wave, ph), 1e-6);
        }

    Lfo lfo;
    lfo.prepare (48000.0);
    LfoParams p;
    p.rateHz = 5.0f;
    p.phaseDegrees = 90.0f;
    p.retrigger = true;
    lfo.setParams (p);
    lfo.reset();
    for (int i = 0; i < 12345; ++i) lfo.process();
    CHECK (lfo.phase() > 0.0);
    lfo.noteOn();
    CHECK (lfo.phase() == 0.0); // retrigger on resets phase

    p.retrigger = false;
    lfo.setParams (p);
    for (int i = 0; i < 7777; ++i) lfo.process();
    const double before = lfo.phase();
    lfo.noteOn();
    CHECK (lfo.phase() == before); // retrigger off preserves phase
    std::cout << "phase offset 0-360 + retrigger on/off OK\n";
}

void testFallback()
{
    Lfo lfo;
    lfo.prepare (48000.0);
    LfoParams p;
    p.sync = true;
    p.division = 6;
    p.rateHz = 3.5f;
    lfo.setParams (p);

    CHECK (std::string (Lfo::fallbackMessage) == "Host tempo unavailable - using free rate");
    lfo.setHostTiming (timing (120.0, false));
    CHECK (lfo.usingFallbackRate());
    CHECK_NEAR (lfo.currentRateHz(), 3.5, 1e-9);

    lfo.setHostTiming (timing (120.0, true));
    CHECK (! lfo.usingFallbackRate());
    CHECK_NEAR (lfo.currentRateHz(), 2.0, 1e-9); // 1/4 at 120 BPM = 2 Hz

    for (const double bad : { 0.0, -120.0, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity() })
    {
        lfo.setHostTiming (timing (bad, true));
        CHECK (lfo.usingFallbackRate());
        CHECK_NEAR (lfo.currentRateHz(), 3.5, 1e-9);
    }

    // The fallback rate follows the free-rate control (last valid), and NaN does not replace it.
    lfo.setHostTiming (timing (120.0, false));
    p.rateHz = 8.0f;
    lfo.setParams (p);
    CHECK_NEAR (lfo.currentRateHz(), 8.0, 1e-9);
    p.rateHz = std::nanf ("");
    lfo.setParams (p);
    CHECK_NEAR (lfo.currentRateHz(), 8.0, 1e-9);

    // Free mode never reports fallback; synced rate is capped (no audio-rate modulation).
    p.sync = false; p.rateHz = 1.0f;
    lfo.setParams (p);
    lfo.setHostTiming (timing (120.0, false));
    CHECK (! lfo.usingFallbackRate());
    p.sync = true; p.division = 17;
    lfo.setParams (p);
    lfo.setHostTiming (timing (999.0, true));
    CHECK (lfo.currentRateHz() <= lfoMaxSyncedRateHz);
    std::cout << "tempo fallback + status flag OK\n";
}

void testSlewAndContinuity()
{
    const double sr = 48000.0;
    Lfo lfo;
    lfo.prepare (sr);
    const double maxStep = 2.0 / (0.002 * sr) + 1e-6;

    // Smooth waveforms at the top of the range are untouched by the slew limiter.
    for (const LfoWave w : { LfoWave::Sine, LfoWave::Triangle })
    {
        LfoParams p;
        p.wave = w; p.rateHz = 20.0f;
        lfo.setParams (p);
        lfo.reset();
        for (int i = 0; i < 5000; ++i)
        {
            const float v = lfo.process();
            CHECK (v == lfo.rawValue());
        }
    }
    // Discontinuous waveforms (and retrigger / phase / waveform jumps) never step more than maxStep per sample.
    for (const LfoWave w : { LfoWave::Saw, LfoWave::Pulse })
    {
        LfoParams p;
        p.wave = w; p.rateHz = 20.0f; p.phaseDegrees = 0.0f;
        lfo.setParams (p);
        lfo.reset();
        float prev = lfo.process();
        for (int i = 0; i < 48000; ++i)
        {
            if (i == 5000) lfo.noteOn();
            if (i == 9000) { p.phaseDegrees = 200.0f; lfo.setParams (p); }
            if (i == 12000) { p.wave = (w == LfoWave::Saw ? LfoWave::Pulse : LfoWave::Saw); lfo.setParams (p); }
            const float v = lfo.process();
            CHECK (std::fabs (v - prev) <= maxStep);
            CHECK (v >= -1.0f && v <= 1.0f);
            prev = v;
        }
    }
    // The slewed output still reaches full scale on a pulse.
    LfoParams p;
    p.wave = LfoWave::Pulse; p.rateHz = 2.0f;
    lfo.setParams (p);
    lfo.reset();
    float mx = -2.0f, mn = 2.0f;
    for (int i = 0; i < 48000; ++i) { const float v = lfo.process(); mx = std::max (mx, v); mn = std::min (mn, v); }
    CHECK (mx == 1.0f && mn == -1.0f);

    // Rate change keeps the phase continuous (increment only).
    p.wave = LfoWave::Sine; p.rateHz = 1.0f;
    lfo.setParams (p);
    lfo.reset();
    for (int i = 0; i < 1000; ++i) lfo.process();
    const double ph = lfo.phase();
    p.rateHz = 15.0f;
    lfo.setParams (p);
    CHECK (lfo.phase() == ph);
    std::cout << "slew limiting + phase continuity OK\n";
}

void testDeterminismAndParams()
{
    auto run = [] (Lfo& l)
    {
        std::vector<float> out;
        LfoParams p;
        p.sync = true; p.division = 4; p.wave = LfoWave::Triangle; p.phaseDegrees = 33.0f;
        l.setParams (p);
        l.setHostTiming (timing (137.0));
        l.reset();
        for (int i = 0; i < 20000; ++i)
        {
            if (i == 5000) l.noteOn();
            if (i == 8000) { p.wave = LfoWave::Pulse; l.setParams (p); }
            out.push_back (l.process());
        }
        return out;
    };
    Lfo a, b;
    a.prepare (44100.0);
    b.prepare (44100.0);
    const auto r1 = run (a), r2 = run (a), r3 = run (b);
    CHECK (std::memcmp (r1.data(), r2.data(), r1.size() * sizeof (float)) == 0);
    CHECK (std::memcmp (r1.data(), r3.data(), r1.size() * sizeof (float)) == 0);

    ParamSnapshot s;
    s[P::lfo_mode] = 1.0f;
    s[P::lfo_rate_hz] = 4.0f;
    s[P::lfo_sync_division] = 13.0f;
    s[P::lfo_waveform] = 3.0f;
    s[P::lfo_phase] = 270.0f;
    s[P::lfo_depth] = 80.0f;
    s[P::lfo_retrigger] = 0.0f;
    const auto p = LfoParams::from (s);
    CHECK (p.sync && p.rateHz == 4.0f && p.division == 13 && p.wave == LfoWave::Pulse);
    CHECK (p.phaseDegrees == 270.0f && ! p.retrigger);
    CHECK_NEAR (p.depth01, 0.8, 1e-6);

    // Range clamps
    Lfo c;
    c.prepare (48000.0);
    LfoParams q;
    q.rateHz = 1000.0f;
    c.setParams (q);
    CHECK_NEAR (c.currentRateHz(), 20.0, 1e-9);
    q.rateHz = 0.0f;
    c.setParams (q);
    CHECK_NEAR (c.currentRateHz(), 0.02, 1e-9);
    q.rateHz = 2.0f; q.phaseDegrees = 1.0e9f; q.depth01 = std::nanf ("");
    c.setParams (q);
    for (int i = 0; i < 1000; ++i) CHECK (std::isfinite (c.process()));
    std::cout << "determinism + params OK\n";
}

void testNoAllocation()
{
    Lfo lfo;
    lfo.prepare (48000.0);
    LfoParams p;
    p.sync = true; p.wave = LfoWave::Saw;
    lfo.setParams (p);
    float sink = 0.0f;
    synthtest::NoAllocScope scope;
    lfo.setHostTiming (timing (140.0));
    for (int i = 0; i < 20000; ++i)
    {
        if (i % 3000 == 0) { lfo.noteOn(); lfo.setParams (p); }
        sink += lfo.process();
    }
    CHECK (scope.count() == 0);
    CHECK (std::isfinite (sink));
}
} // namespace

int main()
{
    testWaveformShapes();
    testDivisionTable();
    testSyncedCycleDuration();
    testFreeCycleDuration();
    testPhaseOffsetAndRetrigger();
    testFallback();
    testSlewAndContinuity();
    testDeterminismAndParams();
    testNoAllocation();
    std::cout << "LfoTests passed\n";
    return 0;
}
