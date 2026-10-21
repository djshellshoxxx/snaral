// Oscillator bank + sub oscillator boundary tests (specs: oscillator-bank.md, sub-mid-engine.md).
#include "SynthTestUtil.h"

#include "Dsp/Oscillators.h"

#include <chrono>
#include <cmath>
#include <complex>
#include <cstring>
#include <iostream>
#include <limits>
#include <vector>

using namespace snaral;

namespace
{

MidOscParams single (int fam, float pos = 0.5f, float width = 0.5f)
{
    MidOscParams p;
    p.sourceA = p.sourceB = fam;
    p.blend = 0.0f;
    p.position = pos;
    p.pulseWidth = width;
    return p;
}

std::vector<float> render (MidOscillator& o, float hz, int n)
{
    std::vector<float> v (static_cast<std::size_t> (n));
    for (auto& s : v)
        s = o.process (hz);
    return v;
}

double rmsOf (const std::vector<float>& v)
{
    double s = 0.0;
    for (const float x : v)
        s += static_cast<double> (x) * x;
    return std::sqrt (s / static_cast<double> (v.size()));
}

// --- aliasing measurement (recorded in oscillator-bank.md) ------------------------------------------------
// Coherent sampling: N = 32768, fundamental bin k (odd, so coprime with N): f0 = k * sr / N.
// Blackman-Harris (4-term) window; harmonic bins m*k +-4 are "signal", everything else up to Nyquist is "alias".
struct AliasResult { double totalDb; double worstBinDb; };

template <typename Gen>
AliasResult measureAlias (Gen&& gen, int k, int N)
{
    std::vector<std::complex<double>> z (static_cast<std::size_t> (N));
    for (int n = 0; n < N; ++n)
    {
        const double x = 2.0 * pi * n / N;
        const double w = 0.35875 - 0.48829 * std::cos (x) + 0.14128 * std::cos (2 * x) - 0.01168 * std::cos (3 * x);
        z[static_cast<std::size_t> (n)] = w * static_cast<double> (gen());
    }
    osc_detail::fft (z, false);
    std::vector<char> harm (static_cast<std::size_t> (N / 2 + 1), 0);
    for (int m = 1; static_cast<long> (m) * k <= N / 2; ++m)
        for (int d = -4; d <= 4; ++d)
        {
            const long b = static_cast<long> (m) * k + d;
            if (b >= 0 && b <= N / 2)
                harm[static_cast<std::size_t> (b)] = 1;
        }
    double hp = 0.0, np = 0.0, worst = 0.0;
    const double fund = std::abs (z[static_cast<std::size_t> (k)]);
    for (int b = 1; b <= N / 2; ++b)
    {
        const double p = std::norm (z[static_cast<std::size_t> (b)]);
        if (harm[static_cast<std::size_t> (b)])
            hp += p;
        else
        {
            np += p;
            worst = std::max (worst, std::sqrt (p));
        }
    }
    return { 10.0 * std::log10 (np / hp + 1e-30), 20.0 * std::log10 (worst / fund + 1e-30) };
}

void testFiniteAndPitchTracking()
{
    const double rates[] = { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 };
    const int notes[] = { 12, 24, 36, 48, 60, 84, 96 };
    for (const double sr : rates)
    {
        MidOscillator o;
        o.prepare (sr);
        for (int fam = 0; fam < 4; ++fam)
            for (const int note : notes)
            {
                o.setParams (single (fam, 0.5f, 0.35f));
                o.reset (0.0);
                const float hz = midiNoteToHz (static_cast<float> (note));
                const double T = sr / hz;
                int n = static_cast<int> (std::min (80000.0, std::max (8192.0, 6.0 * T)));
                const auto x = render (o, hz, n);
                for (const float v : x)
                {
                    CHECK (std::isfinite (v));
                    CHECK (std::fabs (v) <= 2.2f);
                }
                CHECK (rmsOf (x) > 0.05);

                // Pitch tracking: periodic at k*T (k chosen so k*T is within 0.02 sample of an integer) and
                // not periodic at T/2 (rules out an octave-up error).
                int bestK = 1;
                double bestRes = 1.0;
                for (int k = 1; k <= 60 && k * T <= n / 2; ++k)
                {
                    const double res = std::fabs (k * T - std::round (k * T));
                    if (res < bestRes) { bestRes = res; bestK = k; }
                    if (res < 0.02) break;
                }
                const int lag = static_cast<int> (std::lround (bestK * T));
                auto acf = [&] (int l)
                {
                    double num = 0.0, den = 0.0;
                    for (int i = 0; i + l < n; ++i)
                    {
                        num += static_cast<double> (x[static_cast<std::size_t> (i)]) * x[static_cast<std::size_t> (i + l)];
                        den += static_cast<double> (x[static_cast<std::size_t> (i)]) * x[static_cast<std::size_t> (i)];
                    }
                    return num / den;
                };
                if (lag > 0 && lag < n / 2)
                {
                    CHECK (acf (lag) > 0.95);
                    if (T > 8.0)
                        CHECK (acf (static_cast<int> (std::lround (T * 0.5))) < 0.95);
                }
            }
    }
    std::cout << "finite + pitch tracking OK (6 rates x 7 notes x 4 families)\n";
}

void testAliasing()
{
    // Threshold recorded in oscillator-bank.md: total alias energy and worst single alias bin must both be
    // at least 100 dB below the harmonic energy / fundamental respectively.
    constexpr double thresholdDb = -100.0;
    constexpr int N = 32768;
    double worstTotal = -999.0, worstBin = -999.0;
    for (const double sr : { 44100.0, 48000.0 })
    {
        MidOscillator o;
        o.prepare (sr);
        for (int fam = 0; fam < 4; ++fam)
            for (const float pos : { 0.0f, 0.37f, 1.0f })
                for (const float width : { 0.2f, 0.5f, 0.8f })
                {
                    if (fam != 1 && width != 0.5f) continue;
                    if ((fam == 0 || fam == 1) && pos != 0.0f) continue;
                    for (const double target : { 500.0, 1500.0, 3000.0, 5000.0, 7000.0, 9000.0, 12000.0 })
                    {
                        const int k = static_cast<int> (target * N / sr) | 1;
                        const float hz = static_cast<float> (static_cast<double> (k) * sr / N);
                        o.setParams (single (fam, pos, width));
                        o.reset (0.0);
                        const auto r = measureAlias ([&] { return o.process (hz); }, k, N);
                        worstTotal = std::max (worstTotal, r.totalDb);
                        worstBin = std::max (worstBin, r.worstBinDb);
                        CHECK (r.totalDb <= thresholdDb);
                        CHECK (r.worstBinDb <= thresholdDb);
                    }
                }
    }
    std::cout << "aliasing worst total " << worstTotal << " dB, worst bin " << worstBin << " dB (threshold " << thresholdDb << ")\n";

    // The method must be able to see aliasing: a naive (non band-limited) saw at the same pitches fails it.
    double ph = 0.0;
    const int k = static_cast<int> (5000.0 * N / 44100.0) | 1;
    const double inc = static_cast<double>(k) / N;
    const auto naive = measureAlias ([&] { const double v = 2.0 * ph - 1.0; ph += inc; ph -= std::floor (ph); return v; }, k, N);
    CHECK (naive.totalDb > -50.0);
    std::cout << "naive saw control: total " << naive.totalDb << " dB (method discriminates)\n";
}

void testLevelSelectionSafeAndContinuous()
{
    using WT = WaveTables;
    double prevW[WT::numLevels] = {};
    bool first = true;
    double maxJump = 0.0;
    for (float h = 400.0f; h >= 0.6f; h *= 0.9995f)
    {
        const auto ls = WT::selectLevels (h);
        double w[WT::numLevels] = {};
        w[ls.a] += 1.0 - ls.w;
        w[ls.b] += ls.w;
        if (h >= 1.0f)
        {
            // every contributing table must be alias-safe (its harmonic count fits below the limit) or the 256 cap
            if (ls.w < 1.0f) CHECK (static_cast<float> (WT::levelHarmonics[static_cast<std::size_t> (ls.a)]) <= h || ls.a == 0);
            if (ls.w > 0.0f) CHECK (static_cast<float> (WT::levelHarmonics[static_cast<std::size_t> (ls.b)]) <= h);
        }
        if (! first)
        {
            double d = 0.0;
            for (int i = 0; i < WT::numLevels; ++i) d += std::fabs (w[i] - prevW[i]);
            maxJump = std::max (maxJump, d);
        }
        for (int i = 0; i < WT::numLevels; ++i) prevW[i] = w[i];
        first = false;
    }
    CHECK (maxJump < 0.06);
    std::cout << "level selection safe; max weight jump per 0.05% pitch step " << maxJump << "\n";
}

void testBlend()
{
    for (const double sr : { 44100.0, 48000.0 })
    {
        MidOscillator o, ref;
        o.prepare (sr);
        ref.prepare (sr);

        // Endpoints select their sources (bit-identical to the single-source render).
        for (int a = 0; a < 4; ++a)
            for (int b = 0; b < 4; ++b)
            {
                if (a == b) continue;
                MidOscParams p;
                p.sourceA = a; p.sourceB = b; p.position = 0.4f; p.pulseWidth = 0.3f;
                p.blend = 0.0f; o.setParams (p); o.reset (0.0);
                ref.setParams (single (a, 0.4f, 0.3f)); ref.reset (0.0);
                CHECK (render (o, 110.0f, 2000) == render (ref, 110.0f, 2000));
                p.blend = 1.0f; o.setParams (p); o.reset (0.0);
                ref.setParams (single (b, 0.4f, 0.3f)); ref.reset (0.0);
                CHECK (render (o, 110.0f, 2000) == render (ref, 110.0f, 2000));
            }

        // Level-step requirement (sub-mid-engine.md): steady-state RMS at blend 0.25/0.5/0.75 stays within 1 dB
        // of BOTH endpoint renders, for every ordered pair of distinct families, several positions/widths/notes.
        double worst = 0.0;
        for (int a = 0; a < 4; ++a)
            for (int b = 0; b < 4; ++b)
            {
                if (a == b) continue;
                for (const float pos : { 0.0f, 0.5f, 1.0f })
                    for (const float width : { 0.2f, 0.5f, 0.8f })
                        for (const int note : { 24, 48, 84 })
                        {
                            if (sr != 44100.0 && (pos != 0.5f || width != 0.5f)) continue; // full grid at 44.1 kHz only
                            const float hz = midiNoteToHz (static_cast<float> (note));
                            const int period = static_cast<int> (std::round (sr / hz));
                            const int n = period * std::max (8, 12000 / period);
                            auto level = [&] (float blend)
                            {
                                MidOscParams p;
                                p.sourceA = a; p.sourceB = b; p.position = pos; p.pulseWidth = width; p.blend = blend;
                                o.setParams (p);
                                o.reset (0.0);
                                return rmsOf (render (o, hz, n));
                            };
                            const double r0 = level (0.0f), r1 = level (1.0f);
                            for (const float blend : { 0.25f, 0.5f, 0.75f })
                            {
                                const double rm = level (blend);
                                const double d0 = std::fabs (20.0 * std::log10 (rm / r0));
                                const double d1 = std::fabs (20.0 * std::log10 (rm / r1));
                                worst = std::max ({ worst, d0, d1 });
                                CHECK (d0 <= 1.0 && d1 <= 1.0);
                            }
                        }
            }
        std::cout << "blend level step at " << sr << " Hz: worst deviation from an endpoint " << worst << " dB (limit 1 dB)\n";
    }
}

void testDcAndBounds()
{
    const double sr = 48000.0;
    MidOscillator o;
    o.prepare (sr);
    const double limit = std::pow (10.0, -90.0 / 20.0);
    double worstDc = 0.0, worstPeak = 0.0;
    for (int fam = 0; fam < 4; ++fam)
        for (const float pos : { 0.0f, 0.5f, 1.0f })
            for (const float width : { 0.2f, 0.5f, 0.8f })
                for (const int period : { 3000, 1000, 400, 120, 40, 12 }) // 16 Hz .. 4 kHz at 48 kHz
                {
                    o.setParams (single (fam, pos, width));
                    o.reset (0.0);
                    const float hz = static_cast<float> (sr / period);
                    const int n = period * std::max (4, 24000 / period);
                    double sum = 0.0;
                    for (int i = 0; i < n; ++i)
                    {
                        const float v = o.process (hz);
                        sum += v;
                        worstPeak = std::max (worstPeak, static_cast<double> (std::fabs (v)));
                    }
                    const double dc = std::fabs (sum / n);
                    worstDc = std::max (worstDc, dc);
                    CHECK (dc < limit);
                }
    CHECK (worstPeak <= 2.1); // Formant Pair peak cap 2.0 + Gibbs; others <= ~1.1
    std::cout << "DC worst " << 20.0 * std::log10 (worstDc) << " dBFS (limit -90), peak " << worstPeak << "\n";
}

void testDeterminism()
{
    auto run = [] (MidOscillator& o)
    {
        std::vector<float> out;
        MidOscParams p;
        p.sourceA = 0; p.sourceB = 3; p.blend = 0.0f; p.position = 0.2f; p.pulseWidth = 0.4f;
        o.setParams (p);
        o.reset (0.0);
        double hz = 55.0;
        for (int i = 0; i < 20000; ++i)
        {
            if (i == 3000) { p.blend = 0.6f; p.sourceB = 2; o.setParams (p); }
            if (i == 9000) { p.sourceA = 1; p.pulseWidth = 0.7f; o.setParams (p); }
            hz *= 1.00005;                     // glide
            const float pos = 0.5f + 0.4f * std::sin (0.001f * static_cast<float> (i));
            out.push_back (o.process (static_cast<float> (hz), pos));
        }
        return out;
    };
    MidOscillator a, b;
    a.prepare (48000.0);
    b.prepare (48000.0);
    const auto r1 = run (a);
    const auto r2 = run (a); // same instance, after reset
    const auto r3 = run (b); // fresh instance
    CHECK (r1.size() == r2.size() && std::memcmp (r1.data(), r2.data(), r1.size() * sizeof (float)) == 0);
    CHECK (std::memcmp (r1.data(), r3.data(), r1.size() * sizeof (float)) == 0);

    SubOscillator s1, s2;
    s1.prepare (48000.0);
    s2.prepare (48000.0);
    auto runSub = [] (SubOscillator& s)
    {
        std::vector<float> out;
        s.setWaveform (SubWave::Sine);
        s.reset (0.0);
        for (int i = 0; i < 10000; ++i)
        {
            if (i == 4000) s.setWaveform (SubWave::Triangle);
            out.push_back (s.process (41.2f + 0.001f * static_cast<float> (i)));
        }
        return out;
    };
    const auto q1 = runSub (s1);
    const auto q2 = runSub (s1);
    const auto q3 = runSub (s2);
    CHECK (std::memcmp (q1.data(), q2.data(), q1.size() * sizeof (float)) == 0);
    CHECK (std::memcmp (q1.data(), q3.data(), q1.size() * sizeof (float)) == 0);

    // Table content is deterministic: the hash is stable within the process and non-trivial.
    CHECK (WaveTables::instance().contentHash() != 0);
    std::cout << "determinism OK; table content hash (this build) " << std::hex << WaveTables::instance().contentHash() << std::dec
              << ", " << WaveTables::instance().totalFloats() << " floats\n";
}

void testRobustInputs()
{
    MidOscillator o;
    o.prepare (44100.0);
    o.setParams (single (3));
    o.reset (0.0);
    for (const float hz : { 0.0f, -10.0f, 1.0e9f, std::nanf (""), std::numeric_limits<float>::infinity(), 22050.0f, 30000.0f })
        for (const float pos : { -3.0f, 0.0f, 1.0f, 7.0f, std::nanf ("") })
            for (int i = 0; i < 64; ++i)
                CHECK (std::isfinite (o.process (hz, pos)));

    // Position out of range behaves exactly like the clamped position.
    MidOscillator p, q;
    p.prepare (48000.0); q.prepare (48000.0);
    p.setParams (single (2)); q.setParams (single (2));
    p.reset (0.0); q.reset (0.0);
    for (int i = 0; i < 4000; ++i)
        CHECK (p.process (200.0f, 5.0f) == q.process (200.0f, 1.0f));

    // Phase advances by exactly sum(hz / sr) with per-sample pitch (glide / bend).
    MidOscillator g;
    g.prepare (48000.0);
    g.reset (0.0);
    double expect = 0.0;
    for (int i = 0; i < 10000; ++i)
    {
        const float hz = 40.0f * std::exp2 (static_cast<float> (i) / 5000.0f);
        g.process (hz);
        expect += static_cast<double> (hz) / 48000.0;
    }
    CHECK_NEAR (g.phase(), expect - std::floor (expect), 1.0e-9);
    std::cout << "robust inputs + per-sample pitch phase accumulation OK\n";
}

void testParamsFromSnapshot()
{
    ParamSnapshot s;
    s[P::mid_blend] = 50.0f;
    s[P::growl] = 100.0f;
    s[P::mid_pulse_width] = 80.0f;
    s[P::mid_table_position] = 25.0f;
    s[P::mid_source_a] = 2.0f;
    s[P::mid_source_b] = 3.0f;
    s[P::mid_tune_cents] = 50.0f;
    const auto p = MidOscParams::from (s);
    CHECK (p.sourceA == 2 && p.sourceB == 3);
    CHECK_NEAR (p.blend, 0.7, 1e-6);      // 0.5 + 0.20 Growl bias
    CHECK_NEAR (p.pulseWidth, 0.8, 1e-6);
    CHECK_NEAR (p.position, 0.25, 1e-6);
    CHECK_NEAR (p.tuneRatio, std::exp2 (50.0 / 1200.0), 1e-6);
    s[P::mid_blend] = 100.0f;
    CHECK (MidOscParams::from (s).blend == 1.0f);

    s[P::sub_waveform] = 1.0f;
    s[P::sub_tune_cents] = -50.0f;
    const auto sp = SubOscParams::from (s);
    CHECK (sp.wave == SubWave::Triangle);
    CHECK_NEAR (sp.tuneRatio, std::exp2 (-50.0 / 1200.0), 1e-6);
}

void testSub()
{
    for (const double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        SubOscillator o;
        o.prepare (sr);

        // Sine: phase accurate against std::sin, deterministic start at the rising zero crossing.
        o.setWaveform (SubWave::Sine);
        o.reset (0.0);
        const float hz = 43.65f; // F1
        double maxErr = 0.0;
        for (int n = 0; n < 20000; ++n)
        {
            const double expect = std::sin (twoPi * static_cast<double> (hz) * n / sr);
            maxErr = std::max (maxErr, std::fabs (o.process (hz) - expect));
        }
        CHECK (maxErr < 2.0e-6);

        // reset(phase) is deterministic: quarter-cycle start gives +1 first.
        o.reset (0.25);
        CHECK_NEAR (o.process (hz), 1.0, 1e-6);

        // Triangle: starts at 0 rising, peak 1, zero DC, close to the ideal triangle at bass pitches.
        o.setWaveform (SubWave::Triangle);
        o.reset (0.0);
        // wait out the 5 ms weight crossfade, then restart for a clean measurement
        for (int n = 0; n < static_cast<int> (0.1 * sr); ++n) o.process (hz);
        o.reset (0.0);
        const int period = static_cast<int> (std::round (sr / 50.0));
        const float hz50 = static_cast<float> (sr / period);
        double peak = 0.0, sum = 0.0, maxDev = 0.0;
        for (int n = 0; n < period * 10; ++n)
        {
            const double ph = std::fmod (static_cast<double> (n) / period, 1.0);
            double q = ph + 0.25; q -= std::floor (q);
            const double ideal = 1.0 - 4.0 * std::fabs (q - 0.5);
            const float v = o.process (hz50);
            CHECK (std::isfinite (v));
            peak = std::max (peak, static_cast<double> (std::fabs (v)));
            sum += v;
            maxDev = std::max (maxDev, std::fabs (v - ideal));
        }
        CHECK (peak <= 1.0 + 1e-3);
        CHECK (peak >= 0.99);
        CHECK (std::fabs (sum / (period * 10)) < 3.0e-5);
        CHECK (maxDev < 0.01);

        // Pitch: count rising zero crossings of the sine over exactly 2 s -> hz * 2 cycles.
        o.setWaveform (SubWave::Sine);
        o.reset (0.0);
        int crossings = 0;
        float prev = 0.0f;
        const int n2 = static_cast<int> (2.0 * sr);
        for (int n = 0; n < n2; ++n)
        {
            const float v = o.process (55.0f);
            if (prev < 0.0f && v >= 0.0f) ++crossings;
            prev = v;
        }
        CHECK (crossings == 110 || crossings == 109);

        // Waveform switch is crossfaded (no jump larger than a few percent per sample), all finite.
        o.reset (0.0);
        float last = 0.0f; double maxStep = 0.0;
        for (int n = 0; n < static_cast<int> (0.05 * sr); ++n)
        {
            if (n == 1000) o.setWaveform (SubWave::Triangle);
            const float v = o.process (60.0f);
            maxStep = std::max (maxStep, static_cast<double> (std::fabs (v - last)));
            last = v;
        }
        CHECK (maxStep < 0.1);

        // Non-finite / nonsense pitch stays finite.
        for (const float h : { 0.0f, -5.0f, std::nanf (""), 1.0e9f })
            CHECK (std::isfinite (o.process (h)));
    }

    // High notes: triangle stays band-limited (aliasing check, same method as the mid families).
    SubOscillator o;
    constexpr int N = 32768;
    for (const double sr : { 44100.0, 48000.0 })
    {
        o.prepare (sr);
        o.setWaveform (SubWave::Triangle);
        for (const double target : { 2000.0, 5000.0, 9000.0 })
        {
            const int k = static_cast<int> (target * N / sr) | 1;
            const float hz = static_cast<float> (static_cast<double> (k) * sr / N);
            o.reset (0.0);
            for (int i = 0; i < 20000; ++i) o.process (hz); // settle crossfade weights
            o.reset (0.0);
            const auto r = measureAlias ([&] { return o.process (hz); }, k, N);
            CHECK (r.totalDb <= -100.0);
        }
    }
    std::cout << "sub oscillator OK (sine accuracy, triangle, reset, pitch, crossfade, aliasing)\n";
}

void testNoAllocationInRender()
{
    MidOscillator o;
    o.prepare (48000.0); // tables built here, outside the render path
    SubOscillator s;
    s.prepare (48000.0);
    o.setParams (single (3));
    float sink = 0.0f;
    {
        synthtest::NoAllocScope scope;
        for (int fam = 0; fam < 4; ++fam)
        {
            MidOscParams p;
            p.sourceA = fam; p.sourceB = (fam + 1) % 4; p.blend = 0.5f;
            o.setParams (p);
            for (int i = 0; i < 4000; ++i)
                sink += o.process (80.0f + 0.01f * static_cast<float> (i), 0.3f) + s.process (50.0f);
        }
        CHECK (scope.count() == 0);
    }
    CHECK (std::isfinite (sink));
    {
        synthtest::NoAllocScope probe; // the counter itself must work
        void* p = ::operator new (16);
        asm volatile ("" : : "r" (p) : "memory"); // keep the allocation observable
        CHECK (probe.count() >= 1);
        ::operator delete (p);
    }
    std::cout << "no heap allocation during render OK\n";
}
} // namespace

int main()
{
    const auto t0 = std::chrono::steady_clock::now();
    auto lap = [&] (const char* n) { std::cout << "  [" << n << " " << std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count() << " s]\n"; };
    testFiniteAndPitchTracking();
    lap ("testFiniteAndPitchTracking");
    testAliasing();
    lap ("testAliasing");
    testLevelSelectionSafeAndContinuous();
    lap ("testLevelSelectionSafeAndContinuous");
    testBlend();
    lap ("testBlend");
    testDcAndBounds();
    lap ("testDcAndBounds");
    testDeterminism();
    lap ("testDeterminism");
    testRobustInputs();
    lap ("testRobustInputs");
    testParamsFromSnapshot();
    lap ("testParamsFromSnapshot");
    testSub();
    lap ("testSub");
    testNoAllocationInRender();
    lap ("testNoAllocationInRender");
    std::cout << "OscillatorsTests passed\n";
    return 0;
}
