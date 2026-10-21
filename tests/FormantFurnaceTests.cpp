#ifdef NDEBUG
#undef NDEBUG
#endif
#include "Dsp/Fx/FormantFurnace.h"

#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace snaral;

namespace
{
struct Rng
{
    std::uint32_t s = 0x12345678u;
    float next() noexcept   // [-1, 1)
    {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return static_cast<float> (s) * (2.0f / 4294967296.0f) - 1.0f;
    }
    float uni() noexcept { return 0.5f * (next() + 1.0f); }
};

constexpr double fs = 48000.0;

FormantFurnaceParams makeParams (bool on, float vowel, float spacing, float reso, float env, float lfo, float mix)
{
    FormantFurnaceParams p;
    p.enabled = on; p.vowel = vowel; p.spacing = spacing; p.resonance = reso;
    p.envAmount = env; p.lfoAmount = lfo; p.mix = mix;
    return p;
}

double rms (const std::vector<float>& v, std::size_t a, std::size_t b)
{
    double s = 0.0;
    for (std::size_t i = a; i < b; ++i) s += static_cast<double> (v[i]) * v[i];
    return std::sqrt (s / static_cast<double> (b - a));
}

void testParamsFromSnapshot()
{
    ParamSnapshot s;
    auto p = FormantFurnaceParams::from (s);
    assert (! p.enabled);
    assert (std::fabs (p.vowel - 0.25f) < 1e-6f && std::fabs (p.mix - 0.5f) < 1e-6f);
    s[P::furnace_vowel] = 1000.0f;                       // out of domain must be clamped
    s[P::furnace_mix] = std::nanf ("");
    p = FormantFurnaceParams::from (s);
    assert (p.vowel == 1.0f && p.mix == 0.0f);
}

void testBypassIdentity()
{
    Rng r;
    std::vector<float> in (4096), out;
    for (auto& x : in) x = r.next();

    for (int mode = 0; mode < 3; ++mode)
    {
        FormantFurnace f;
        f.prepare (fs, 512);
        // 0: default params (disabled), 1: enabled but mix = 0, 2: enabled then disabled and settled
        f.setParams (mode == 0 ? FormantFurnaceParams {} : makeParams (true, 0.4f, 0.6f, 0.9f, 0.5f, 0.5f, mode == 1 ? 0.0f : 1.0f));
        MidModulation mod { 0.7f, 0.3f };
        out = in;
        if (mode == 2)
        {
            for (int b = 0; b < 40; ++b) f.process (out.data(), 512, mod);   // active, ~0.4 s
            f.setParams (makeParams (false, 0.4f, 0.6f, 0.9f, 0.5f, 0.5f, 1.0f));
            for (int b = 0; b < 40; ++b) f.process (out.data(), 512, mod);   // fade out + settle
            out = in;
        }
        else
            out = in;
        for (std::size_t o = 0; o + 512 <= out.size(); o += 512)
            f.process (out.data() + o, 512, mod);
        assert (std::memcmp (out.data(), in.data(), in.size() * sizeof (float)) == 0);
    }
}

void testFiniteAndBounded()
{
    Rng r;
    FormantFurnace f;
    f.prepare (fs, 1024);
    std::vector<float> buf (1024);
    float peak = 0.0f;
    for (int blk = 0; blk < 20000; ++blk)
    {
        const int n = (blk % 5 == 0) ? 1 : 1 + static_cast<int> (r.uni() * 255.0f);
        auto ext = [&] (float v) { return r.uni() < 0.3f ? (r.uni() < 0.5f ? 0.0f : 1.0f) : v; };   // hit the endpoints often
        f.setParams (makeParams (r.uni() < 0.9f, ext (r.uni()), ext (r.uni()), ext (r.uni()), ext (r.uni()), ext (r.uni()), ext (r.uni())));
        MidModulation mod { ext (r.uni()), r.next() };
        for (int i = 0; i < n; ++i) buf[static_cast<std::size_t> (i)] = (blk % 7 == 0) ? (r.next() > 0 ? 1.0f : -1.0f) : r.next();
        f.process (buf.data(), n, mod);
        for (int i = 0; i < n; ++i)
        {
            assert (std::isfinite (buf[static_cast<std::size_t> (i)]));
            peak = std::max (peak, std::fabs (buf[static_cast<std::size_t> (i)]));
        }
    }
    std::printf ("  furnace worst peak for |in|<=1: %.3f (bound %.2f)\n", static_cast<double> (peak), static_cast<double> (FormantFurnace::wetGainBound));
    assert (peak <= FormantFurnace::wetGainBound);
}

void testNoSelfOscillation()
{
    for (int v = 0; v <= 4; ++v)
        for (float spacing : { 0.0f, 0.5f, 1.0f })
            for (float shiftAmt : { 0.0f, 1.0f })
            {
                FormantFurnace f;
                f.prepare (fs, 512);
                f.setParams (makeParams (true, static_cast<float> (v) * 0.25f, spacing, 1.0f, shiftAmt, 0.0f, 1.0f));
                f.reset();
                std::vector<float> y (static_cast<std::size_t> (fs), 0.0f);
                y[0] = 1.0f;
                MidModulation mod { 1.0f, 0.0f };
                for (std::size_t o = 0; o < y.size(); o += 500) f.process (y.data() + o, static_cast<int> (std::min<std::size_t> (500, y.size() - o)), mod);
                float pk = 0.0f;
                for (auto s : y) pk = std::max (pk, std::fabs (s));
                const double tail = rms (y, y.size() - 12000, y.size());
                assert (std::isfinite (pk) && pk > 0.0f);
                assert (tail < 1e-3 * pk);
            }
}

void testNoStuckEnergyAndDc()
{
    Rng r;
    FormantFurnace f;
    f.prepare (fs, 512);
    f.setParams (makeParams (true, 0.8f, 0.3f, 1.0f, 0.5f, 0.5f, 1.0f));
    std::vector<float> a (512);
    for (int b = 0; b < 100; ++b)
    {
        for (auto& x : a) x = r.next();
        f.process (a.data(), 512, { 0.5f, 0.2f });
    }
    for (int b = 0; b < 200; ++b)   // 2 s of silence
    {
        std::fill (a.begin(), a.end(), 0.0f);
        f.process (a.data(), 512, { 0.5f, 0.2f });
    }
    for (auto x : a) assert (std::fabs (x) < 1e-6f);

    // DC: a DC-free sine yields a DC-free output (linear band-pass bank, no offset introduced)
    FormantFurnace g;
    g.prepare (fs, 480);
    g.setParams (makeParams (true, 0.5f, 0.5f, 0.7f, 0.0f, 0.0f, 1.0f));
    std::vector<float> s (480);
    double sum = 0.0; long cnt = 0;
    for (int b = 0; b < 400; ++b)
    {
        for (int i = 0; i < 480; ++i) s[static_cast<std::size_t> (i)] = 0.8f * std::sin (static_cast<float> (twoPi) * static_cast<float> (i) / 200.0f * 0.0f + static_cast<float> (twoPi) * 240.0f * static_cast<float> (i + b * 480) / static_cast<float> (fs));
        g.process (s.data(), 480, {});
        if (b >= 300) for (auto x : s) { sum += x; ++cnt; }
    }
    assert (std::fabs (sum / static_cast<double> (cnt)) < 3.0e-5);
}

void testDeterminism()
{
    Rng r;
    std::vector<float> in (8192);
    for (auto& x : in) x = r.next();
    auto run = [&] (FormantFurnace& f) {
        auto b = in;
        f.setParams (makeParams (true, 0.6f, 0.2f, 0.8f, 0.4f, 0.6f, 0.7f));
        for (std::size_t o = 0; o < b.size(); o += 256) f.process (b.data() + o, 256, { 0.3f, -0.4f });
        return b;
    };
    FormantFurnace a, b;
    a.prepare (fs, 256); b.prepare (fs, 256);
    const auto ra = run (a), rb = run (b);
    assert (std::memcmp (ra.data(), rb.data(), ra.size() * sizeof (float)) == 0);
    a.setParams (FormantFurnaceParams {});   // reset() snaps smoothers to the stored params: restore the fresh state
    a.reset();
    const auto rc = run (a);
    assert (std::memcmp (ra.data(), rc.data(), ra.size() * sizeof (float)) == 0);
}

void testMonoFoldDown()
{
    // Linear + mono-in/mono-out: process(L) + process(R) == process(L + R), so a mono fold-down of a processed
    // stereo source equals the processed fold-down. Also no complete cancellation of broadband material.
    Rng r;
    std::vector<float> L (16384), R (16384), S (16384);
    for (std::size_t i = 0; i < L.size(); ++i) { L[i] = 0.4f * r.next(); R[i] = 0.4f * r.next(); S[i] = L[i] + R[i]; }
    for (float vowel : { 0.0f, 0.3f, 0.7f, 1.0f })
        for (float reso : { 0.0f, 0.5f, 1.0f })
        {
            FormantFurnace fl, fr, fsum;
            for (auto* f : { &fl, &fr, &fsum }) { f->prepare (fs, 512); f->setParams (makeParams (true, vowel, 0.5f, reso, 0.0f, 0.0f, 0.8f)); f->reset(); }
            auto l = L, rr = R, s = S;
            for (std::size_t o = 0; o < l.size(); o += 512)
            {
                fl.process (l.data() + o, 512, {}); fr.process (rr.data() + o, 512, {}); fsum.process (s.data() + o, 512, {});
            }
            float maxDiff = 0.0f;
            for (std::size_t i = 0; i < l.size(); ++i) maxDiff = std::max (maxDiff, std::fabs (l[i] + rr[i] - s[i]));
            assert (maxDiff < 1e-4f);
            const double ratio = rms (s, 4096, s.size()) / rms (S, 4096, S.size());
            assert (ratio > 0.3 && ratio < 8.0);
        }
}

void testSmoothSweepNoZipper()
{
    // Abruptly stepping the vowel control must not produce a step in the output waveform larger than ~ the
    // steady-state sample-to-sample slew of either end state.
    auto maxStep = [] (float vowelA, float vowelB, bool sweep)
    {
        FormantFurnace f;
        f.prepare (fs, 256);
        f.setParams (makeParams (true, vowelA, 0.5f, 0.7f, 0.0f, 0.0f, 1.0f));
        f.reset();
        std::vector<float> y (48128);
        for (std::size_t i = 0; i < y.size(); ++i) y[i] = 0.5f * std::sin (static_cast<float> (twoPi * 400.0 * static_cast<double> (i) / fs));
        float prev = 0.0f, mx = 0.0f;
        for (std::size_t o = 0; o < y.size(); o += 256)
        {
            if (sweep && o == 24000) f.setParams (makeParams (true, vowelB, 0.5f, 0.7f, 0.0f, 0.0f, 1.0f));
            f.process (y.data() + o, 256, {});
            for (std::size_t i = o; i < o + 256 && i < y.size(); ++i)
            {
                if (o > 12000) mx = std::max (mx, std::fabs (y[i] - prev));
                prev = y[i];
            }
        }
        return mx;
    };
    const float steadyA = maxStep (0.0f, 0.0f, false), steadyB = maxStep (1.0f, 1.0f, false);
    const float swept = maxStep (0.0f, 1.0f, true);
    assert (swept <= 1.6f * std::max (steadyA, steadyB));
}

void testPeakTable()
{
    float prev[4] = {};
    for (int i = 0; i <= 100; ++i)
        for (float sp : { 0.0f, 0.5f, 1.0f })
        {
            float f[4];
            FormantFurnace::peakFrequencies (static_cast<float> (i) / 100.0f, sp, 0.0f, f);
            for (int k = 0; k < 4; ++k) assert (std::isfinite (f[k]) && f[k] > 100.0f && f[k] < 12000.0f);
            if (sp == 0.5f)
            {
                if (i > 0) for (int k = 0; k < 4; ++k) assert (std::fabs (std::log2 (f[k] / prev[k])) < 0.1f);   // continuous
                for (int k = 0; k < 4; ++k) prev[k] = f[k];
            }
        }
}

void benchmark()
{
    FormantFurnace f;
    f.prepare (fs, 512);
    f.setParams (makeParams (true, 0.5f, 0.5f, 0.8f, 0.5f, 0.5f, 1.0f));
    Rng r;
    std::vector<float> b (512);
    const int blocks = 48000 * 20 / 512;
    const auto t0 = std::chrono::steady_clock::now();
    float sink = 0.0f;
    for (int i = 0; i < blocks; ++i)
    {
        for (auto& x : b) x = r.next() * 0.5f;
        f.process (b.data(), 512, { 0.5f, std::sin (static_cast<float> (i) * 0.01f) });
        sink += b[0];
    }
    const double sec = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    std::printf ("  furnace bench: %.1f s audio in %.3f s (%.0fx realtime, %.3f%% of one core @48k; sink %.2f)\n",
                 blocks * 512.0 / fs, sec, blocks * 512.0 / fs / sec, 100.0 * sec / (blocks * 512.0 / fs), static_cast<double> (sink));
}
} // namespace

int main()
{
    testParamsFromSnapshot();
    testBypassIdentity();
    testFiniteAndBounded();
    testNoSelfOscillation();
    testNoStuckEnergyAndDc();
    testDeterminism();
    testMonoFoldDown();
    testSmoothSweepNoZipper();
    testPeakTable();
    benchmark();
    std::puts ("FormantFurnaceTests passed");
    return 0;
}
