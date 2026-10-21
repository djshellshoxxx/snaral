// SVF filter boundary tests (spec: motion-filter.md, Filter contract + acceptance 6).
#include "SynthTestUtil.h"

#include "Dsp/SvfFilter.h"

#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <vector>

using namespace snaral;

namespace
{
struct Lcg
{
    std::uint32_t s = 12345u;
    float next() { s = s * 1664525u + 1013904223u; return static_cast<float> (s >> 8) / 8388608.0f - 1.0f; } // -1..1
};

// Steady-state gain of a sine at hz through the filter (RMS ratio after settling).
double gainAt (SvfFilter& f, double sr, double hz)
{
    f.reset();
    const int settle = static_cast<int> (0.5 * sr);
    const int measure = static_cast<int> (std::max (0.25 * sr, 20.0 * sr / hz));
    double in = 0.0, out = 0.0;
    for (int n = 0; n < settle + measure; ++n)
    {
        const float x = static_cast<float> (std::sin (twoPi * hz * n / sr));
        const float y = f.process (x);
        if (n >= settle) { in += static_cast<double> (x) * x; out += static_cast<double> (y) * y; }
    }
    return std::sqrt (out / in);
}

void testResonanceMapping()
{
    CHECK_NEAR (SvfFilter::resonanceToQ (0.0f), 0.5, 1e-6);
    CHECK_NEAR (SvfFilter::resonanceToQ (80.0f), 10.0, 1e-5);
    CHECK_NEAR (SvfFilter::resonanceToQ (-10.0f), 0.5, 1e-6);
    CHECK_NEAR (SvfFilter::resonanceToQ (500.0f), 10.0, 1e-5);
    CHECK_NEAR (SvfFilter::resonanceToQ (std::nanf ("")), 0.5, 1e-6);
    float prev = 0.0f;
    for (float r = 0.0f; r <= 80.0f; r += 0.5f)
    {
        const float q = SvfFilter::resonanceToQ (r);
        CHECK (q > prev);
        CHECK (q >= 0.5f && q <= 10.0f);
        prev = q;
    }
    std::cout << "resonance 0..80 % -> Q " << SvfFilter::resonanceToQ (0) << " .. " << SvfFilter::resonanceToQ (80)
              << " (default 20 % -> " << SvfFilter::resonanceToQ (20) << ")\n";
}

void testFrequencyResponse()
{
    for (const double sr : { 44100.0, 48000.0, 96000.0 })
    {
        SvfFilter f;
        f.prepare (sr);
        for (const float resPct : { 0.0f, 20.0f, 50.0f, 80.0f })
        {
            const double q = SvfFilter::resonanceToQ (resPct);
            const double fc = 1000.0;

            f.setMode (SvfMode::LowPass);
            f.setResonancePercent (resPct);
            f.setCutoff (static_cast<float> (fc));
            CHECK_NEAR (gainAt (f, sr, 20.0), 1.0, 0.02);            // passband
            CHECK_NEAR (gainAt (f, sr, fc), q, 0.02 * q);            // |H(fc)| = Q
            CHECK (gainAt (f, sr, 10.0 * fc) < 0.02);                // >= 34 dB down a decade above

            f.setMode (SvfMode::BandPass);
            CHECK_NEAR (gainAt (f, sr, fc), 1.0, 0.02);              // unity-peak band-pass
            CHECK (gainAt (f, sr, fc / 20.0) < 0.2);
            CHECK (gainAt (f, sr, fc * 20.0) < 0.2);
        }
    }
    std::cout << "frequency response (LP passband, |H(fc)|=Q, rolloff, BP unity peak) OK\n";
}

void testEndpointsAndSweeps()
{
    const auto t0 = std::chrono::steady_clock::now();
    double worst = 0.0;
    for (const double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (const SvfMode mode : { SvfMode::LowPass, SvfMode::BandPass })
            for (const float cutoff : { 20.0f, 21.0f, 200.0f, 5000.0f, 19999.0f, 20000.0f })
                for (const float res : { 0.0f, 40.0f, 80.0f })
                {
                    SvfFilter f;
                    f.prepare (sr);
                    f.setMode (mode);
                    f.setResonancePercent (res);
                    f.setCutoff (cutoff);
                    Lcg r;
                    for (int n = 0; n < 6000; ++n)
                    {
                        const float y = f.process (r.next());
                        CHECK (std::isfinite (y));
                        worst = std::max (worst, static_cast<double> (std::fabs (y)));
                    }
                }
    CHECK (worst < 60.0);

    // Out of range cutoff requests are clamped; never above 20 kHz or 0.49 sr.
    for (const double sr : { 44100.0, 192000.0 })
    {
        SvfFilter f;
        f.prepare (sr);
        f.setCutoff (1.0e9f);
        CHECK (f.cutoffHz() <= 20000.0f && f.cutoffHz() <= 0.49 * sr);
        f.setCutoff (-5.0f);
        CHECK (f.cutoffHz() == 20.0f);
        f.setCutoff (std::nanf (""));
        CHECK (f.cutoffHz() >= 20.0f && f.cutoffHz() <= 20000.0f);
    }

    // Per-sample cutoff modulation, extreme and random, plus a full log sweep, at maximum resonance.
    double worstMod = 0.0;
    for (const SvfMode mode : { SvfMode::LowPass, SvfMode::BandPass })
        for (const double sr : { 44100.0, 192000.0 })
        {
            SvfFilter f;
            f.prepare (sr);
            f.setMode (mode);
            f.setResonancePercent (80.0f);
            Lcg r;
            for (int n = 0; n < 60000; ++n)
            {
                const float hz = (n & 1) ? 20.0f : 20000.0f; // alternate extremes every sample
                const float y = f.process (r.next(), hz);
                CHECK (std::isfinite (y));
                worstMod = std::max (worstMod, static_cast<double> (std::fabs (y)));
            }
            for (int n = 0; n < 60000; ++n)
            {
                const float hz = 20.0f * std::pow (1000.0f, 0.5f + 0.5f * r.next()); // random each sample
                const float y = f.process (r.next(), hz);
                CHECK (std::isfinite (y));
                worstMod = std::max (worstMod, static_cast<double> (std::fabs (y)));
            }
            for (int n = 0; n < static_cast<int> (2.0 * sr); ++n)
            {
                const float hz = 20.0f * std::pow (1000.0f, 0.5f - 0.5f * static_cast<float> (std::cos (twoPi * 3.0 * n / sr)));
                const float y = f.process (std::sin (0.05f * static_cast<float> (n)), hz);
                CHECK (std::isfinite (y));
                worstMod = std::max (worstMod, static_cast<double> (std::fabs (y)));
            }
        }
    CHECK (worstMod < 200.0);
    std::cout << "endpoints + sweeps finite; worst |y| static " << worst << ", per-sample modulation " << worstMod
              << " (took " << std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count() << " s)\n";
}

void testNoSelfOscillation()
{
    for (const double sr : { 44100.0, 192000.0 })
        for (const float cutoff : { 20.0f, 100.0f, 1000.0f, 20000.0f })
            for (const SvfMode mode : { SvfMode::LowPass, SvfMode::BandPass })
            {
                SvfFilter f;
                f.prepare (sr);
                f.setMode (mode);
                f.setResonancePercent (80.0f);
                f.setCutoff (cutoff);
                f.process (1.0f); // impulse
                // decay time constant ~ Q / (pi fc); wait 25 of them (capped at 6 s)
                const double tau = 10.0 / (pi * cutoff);
                const int n = static_cast<int> (std::clamp (25.0 * tau, 0.1, 6.0) * sr); // warping near Nyquist: wait >= 0.1 s
                float last = 0.0f;
                for (int i = 0; i < n; ++i)
                    last = f.process (0.0f);
                CHECK (std::fabs (last) < 1.0e-4f);
            }
    // DC behaviour
    SvfFilter f;
    f.prepare (48000.0);
    f.setCutoff (500.0f);
    f.setResonancePercent (80.0f);
    float y = 0.0f;
    for (int i = 0; i < 48000; ++i) y = f.process (1.0f);
    CHECK_NEAR (y, 1.0, 1e-3); // LP DC gain 1
    f.setMode (SvfMode::BandPass);
    f.reset();
    for (int i = 0; i < 48000; ++i) y = f.process (1.0f);
    CHECK_NEAR (y, 0.0, 1e-3); // BP DC gain 0
    std::cout << "no self-oscillation (impulse decays at Q max), DC gains OK\n";
}

void testResetDeterminismAndRobustness()
{
    SvfFilter f, g;
    f.prepare (48000.0);
    g.prepare (48000.0);
    auto run = [] (SvfFilter& s)
    {
        std::vector<float> out;
        s.setMode (SvfMode::LowPass);
        s.setResonancePercent (60.0f);
        Lcg r;
        for (int n = 0; n < 8000; ++n)
            out.push_back (s.process (r.next(), 100.0f * std::exp2 (static_cast<float> (n % 4000) / 400.0f)));
        return out;
    };
    f.reset();
    const auto a = run (f);
    f.reset();
    const auto b = run (f);
    const auto c = run (g);
    CHECK (std::memcmp (a.data(), b.data(), a.size() * sizeof (float)) == 0);
    CHECK (std::memcmp (a.data(), c.data(), a.size() * sizeof (float)) == 0);

    // reset clears state: silence in -> exactly silence out
    for (int i = 0; i < 100; ++i) f.process (1.0f);
    f.reset();
    for (int i = 0; i < 100; ++i) CHECK (f.process (0.0f) == 0.0f);

    // Non-finite input does not poison the state.
    for (const float bad : { std::nanf (""), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity() })
    {
        f.process (bad);
        for (int i = 0; i < 100; ++i) CHECK (std::isfinite (f.process (0.5f)));
    }
    // Huge input stays finite (state safety limit)
    for (int i = 0; i < 1000; ++i) CHECK (std::isfinite (f.process (1.0e30f)));
    for (int i = 0; i < 1000; ++i) CHECK (std::isfinite (f.process (0.0f)));

    // Parameters from snapshot
    ParamSnapshot s;
    s[P::filter_cutoff_hz] = 321.0f;
    s[P::filter_resonance] = 80.0f;
    s[P::filter_type] = 1.0f;
    const auto p = SvfParams::from (s);
    CHECK (p.cutoffHz == 321.0f && p.resonancePercent == 80.0f && p.mode == SvfMode::BandPass);
    f.setParams (p);
    CHECK (f.cutoffHz() == 321.0f);
    std::cout << "reset/determinism/robustness OK\n";
}

void testNoAllocation()
{
    SvfFilter f;
    f.prepare (48000.0);
    float sink = 0.0f;
    synthtest::NoAllocScope scope;
    for (int i = 0; i < 10000; ++i)
        sink += f.process (std::sin (0.01f * static_cast<float> (i)), 50.0f + static_cast<float> (i));
    CHECK (scope.count() == 0);
    CHECK (std::isfinite (sink));
}
} // namespace

int main()
{
    testResonanceMapping();
    testFrequencyResponse();
    testEndpointsAndSweeps();
    testNoSelfOscillation();
    testResetDeterminismAndRobustness();
    testNoAllocation();
    std::cout << "SvfFilterTests passed\n";
    return 0;
}
