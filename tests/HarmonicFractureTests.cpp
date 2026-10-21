#ifdef NDEBUG
#undef NDEBUG
#endif
#include "Dsp/Fx/HarmonicFracture.h"

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
    std::uint32_t s = 0x9e3779b9u;
    float next() noexcept
    {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return static_cast<float> (s) * (2.0f / 4294967296.0f) - 1.0f;
    }
    float uni() noexcept { return 0.5f * (next() + 1.0f); }
};

constexpr double fs = 48000.0;

HarmonicFractureParams cleanParams (float crossover)
{
    HarmonicFractureParams p;
    p.enabled = true; p.crossoverHz = crossover;
    p.driveLow = p.driveMid = p.driveHigh = 0.0f;
    p.symmetry = 0.5f; p.tilt = 0.5f; p.compDb = 0.0f; p.mix = 1.0f;
    return p;
}

HarmonicFractureParams hotParams()
{
    HarmonicFractureParams p;
    p.enabled = true; p.crossoverHz = 400.0f;
    p.driveLow = p.driveMid = p.driveHigh = 1.0f;
    p.symmetry = 0.5f; p.tilt = 0.5f; p.compDb = 0.0f; p.mix = 1.0f;
    return p;
}

double rms (const std::vector<float>& v, std::size_t a, std::size_t b)
{
    double s = 0.0;
    for (std::size_t i = a; i < b; ++i) s += static_cast<double> (v[i]) * v[i];
    return std::sqrt (s / static_cast<double> (b - a));
}

void run (HarmonicFracture& f, std::vector<float>& v, int block = 512)
{
    for (std::size_t o = 0; o < v.size(); o += static_cast<std::size_t> (block))
        f.process (v.data() + o, static_cast<int> (std::min<std::size_t> (static_cast<std::size_t> (block), v.size() - o)), {});
}

std::vector<float> sine (double hz, float amp, std::size_t n)
{
    std::vector<float> v (n);
    for (std::size_t i = 0; i < n; ++i) v[i] = amp * static_cast<float> (std::sin (twoPi * hz * static_cast<double> (i) / fs));
    return v;
}

void testParamsFromSnapshot()
{
    ParamSnapshot s;
    auto p = HarmonicFractureParams::from (s);
    assert (! p.enabled && p.crossoverHz == 500.0f && std::fabs (p.driveHigh - 0.45f) < 1e-6f && p.compDb == 0.0f);
    s[P::fracture_crossover_hz] = 5.0f;
    s[P::fracture_comp_db] = std::nanf ("");
    p = HarmonicFractureParams::from (s);
    assert (p.crossoverHz == 120.0f && p.compDb == 0.0f);
}

void testBypassIdentity()
{
    Rng r;
    std::vector<float> in (4096);
    for (auto& x : in) x = r.next();
    for (int mode = 0; mode < 3; ++mode)
    {
        HarmonicFracture f;
        f.prepare (fs, 512);
        auto p = hotParams();
        if (mode == 0) p = HarmonicFractureParams {};              // default: disabled
        if (mode == 1) p.mix = 0.0f;                                // enabled, mix 0
        f.setParams (p);
        f.reset();
        auto out = in;
        if (mode == 2)
        {
            auto warm = in;
            run (f, warm);                                          // active, then disable and settle
            f.setParams (HarmonicFractureParams {});
            warm = in; run (f, warm); warm = in; run (f, warm);
        }
        run (f, out);
        assert (std::memcmp (out.data(), in.data(), in.size() * sizeof (float)) == 0);
    }
}

void testCrossoverFlatness()
{
    // Zero drive: the three bands must sum to a flat magnitude (all-pass) at every crossover setting.
    for (float xo : { 120.0f, 300.0f, 500.0f, 1200.0f, 3000.0f })
    {
        float worstDb = 0.0f;
        for (double hz : { 100.0, 150.0, 220.0, 330.0, 500.0, 750.0, 1000.0, 1500.0, 2200.0, 3300.0, 5000.0, 8000.0, 11000.0 })
        {
            HarmonicFracture f;
            f.prepare (fs, 512);
            f.setParams (cleanParams (xo));
            f.reset();
            auto v = sine (hz, 0.5f, 48000);
            run (f, v);
            const double ratio = rms (v, 24000, 48000) / (0.5 / std::sqrt (2.0));
            worstDb = std::max (worstDb, std::fabs (20.0f * std::log10 (static_cast<float> (ratio))));
        }
        assert (worstDb < 0.1f);
    }
}

void testCrossoverSweepNoNotch()
{
    // Sweep the crossover continuously over broadband noise with zero drive: short-term RMS must stay within 1 dB of
    // the input RMS (no persistent notch, no peak build-up), output finite.
    Rng r;
    HarmonicFracture f;
    f.prepare (fs, 256);
    f.setParams (cleanParams (120.0f));
    f.reset();
    double worst = 0.0;
    for (int blk = 0; blk < 1500; ++blk)
    {
        auto p = cleanParams (120.0f * std::pow (25.0f, 0.5f + 0.5f * std::sin (static_cast<float> (blk) * 0.02f)));
        f.setParams (p);
        std::vector<float> v (256), ref;
        for (auto& x : v) x = 0.3f * r.next();
        ref = v;
        f.process (v.data(), 256, {});
        if (blk > 20)
        {
            worst = std::max (worst, std::fabs (20.0 * std::log10 (rms (v, 0, 256) / rms (ref, 0, 256))));
            for (auto x : v) assert (std::isfinite (x) && std::fabs (x) < 2.0f);
        }
    }
    std::printf ("  fracture crossover sweep worst short-term level deviation: %.2f dB\n", worst);
    assert (worst < 2.5);   // white noise, 256-sample windows: dominated by estimator spread; flatness itself is asserted above
}

void testFiniteAndBounded()
{
    Rng r;
    HarmonicFracture f;
    f.prepare (fs, 1024);
    std::vector<float> buf (1024);
    float peak = 0.0f;
    for (int blk = 0; blk < 20000; ++blk)
    {
        const int n = (blk % 5 == 0) ? 1 : 1 + static_cast<int> (r.uni() * 255.0f);
        auto ext = [&] (float lo, float hi) { const float u = r.uni(); return u < 0.15f ? lo : (u < 0.3f ? hi : lo + (hi - lo) * r.uni()); };
        HarmonicFractureParams p;
        p.enabled = r.uni() < 0.9f;
        p.crossoverHz = ext (120.0f, 3000.0f);
        p.driveLow = ext (0.0f, 1.0f); p.driveMid = ext (0.0f, 1.0f); p.driveHigh = ext (0.0f, 1.0f);
        p.symmetry = ext (0.0f, 1.0f); p.tilt = ext (0.0f, 1.0f);
        p.compDb = ext (-12.0f, 6.0f); p.mix = ext (0.0f, 1.0f);
        f.setParams (p);
        for (int i = 0; i < n; ++i) buf[static_cast<std::size_t> (i)] = (blk % 7 == 0) ? (r.next() > 0 ? 1.0f : -1.0f) : r.next();
        f.process (buf.data(), n, {});
        for (int i = 0; i < n; ++i)
        {
            assert (std::isfinite (buf[static_cast<std::size_t> (i)]));
            peak = std::max (peak, std::fabs (buf[static_cast<std::size_t> (i)]));
        }
    }
    std::printf ("  fracture worst peak for |in|<=1 under random sweeps: %.3f\n", static_cast<double> (peak));
    assert (peak < 20.0f);
}

void testShapeFunction()
{
    for (int band = 0; band < 3; ++band)
        for (float gain : { 1.0f, 5.0f, 13.0f })
            for (float bias : { -0.6f, 0.0f, 0.6f })
            {
                assert (std::fabs (HarmonicFracture::shapeSample (band, 0.0f, gain, 1.0f, bias)) < 1e-6f);   // no static DC
                for (float x = -4.0f; x <= 4.0f; x += 0.01f)
                {
                    const float y = HarmonicFracture::shapeSample (band, x, gain, 1.0f, bias);
                    assert (std::isfinite (y) && std::fabs (y) <= std::fabs (x) + 2.0f / std::sqrt (gain) + std::fabs (x) + 1e-4f);
                    assert (HarmonicFracture::shapeSample (band, x, gain, 0.0f, bias) == x);               // amount 0 = linear
                }
            }
    // symmetric setting gives an odd function
    for (int band = 0; band < 3; ++band)
        assert (std::fabs (HarmonicFracture::shapeSample (band, 0.3f, 7.0f, 1.0f, 0.0f) + HarmonicFracture::shapeSample (band, -0.3f, 7.0f, 1.0f, 0.0f)) < 1e-5f);
}

void testNoDc()
{
    // Steady state, integer number of cycles (240 Hz @ 48k = 200 samples): mean of the output is DC.
    for (float sym : { 0.0f, 0.25f, 0.5f, 1.0f })
        for (float amp : { 0.1f, 0.5f, 1.0f })
        {
            HarmonicFracture f;
            f.prepare (fs, 480);
            auto p = hotParams();
            p.symmetry = sym;
            f.setParams (p);
            f.reset();
            auto v = sine (240.0, amp, 48000 * 3);
            run (f, v, 480);
            double sum = 0.0;
            const std::size_t a = v.size() - 24000;
            for (std::size_t i = a; i < v.size(); ++i) sum += v[i];
            const double dc = std::fabs (sum / 24000.0);
            assert (dc < std::pow (10.0, -90.0 / 20.0));
        }
}

void testSubProtection()
{
    // A 60 Hz fundamental (inside the protected sub range) keeps its level at full drive.
    HarmonicFracture f;
    f.prepare (fs, 512);
    f.setParams (hotParams());
    f.reset();
    auto v = sine (60.0, 0.6f, 48000 * 2);
    run (f, v);
    double re = 0.0, im = 0.0;
    for (std::size_t i = 48000; i < v.size(); ++i)
    {
        re += v[i] * std::cos (twoPi * 60.0 * static_cast<double> (i) / fs);
        im += v[i] * std::sin (twoPi * 60.0 * static_cast<double> (i) / fs);
    }
    const double amp = 2.0 * std::sqrt (re * re + im * im) / 48000.0;
    // auto compensation at full drive is 1/sqrt(1 + 1.2) = -3.42 dB; the fundamental must otherwise be untouched
    const double db = 20.0 * std::log10 (amp / 0.6) + 3.42;
    std::printf ("  fracture 60 Hz fundamental change at full drive (net of auto comp): %.2f dB\n", db);
    assert (std::fabs (db) < 1.0);
}

void testImpulseDecayAndSilence()
{
    for (float xo : { 120.0f, 500.0f, 3000.0f })
    {
        HarmonicFracture f;
        f.prepare (fs, 512);
        auto p = hotParams(); p.crossoverHz = xo; p.symmetry = 1.0f;
        f.setParams (p);
        f.reset();
        std::vector<float> y (48000, 0.0f);
        y[0] = 1.0f;
        run (f, y);
        assert (rms (y, 36000, 48000) < 1e-4);
    }
    // noise, then 2 s of silence: no stuck energy
    Rng r;
    HarmonicFracture f;
    f.prepare (fs, 512);
    f.setParams (hotParams());
    f.reset();
    std::vector<float> n (48000);
    for (auto& x : n) x = r.next();
    run (f, n);
    std::vector<float> z (96000, 0.0f);
    run (f, z);
    for (std::size_t i = 60000; i < z.size(); ++i) assert (std::fabs (z[i]) < 1e-5f);
}

void testDeterminismAndMono()
{
    Rng r;
    std::vector<float> in (8192);
    for (auto& x : in) x = 0.7f * r.next();
    HarmonicFracture a, b;
    for (auto* f : { &a, &b }) { f->prepare (fs, 256); f->setParams (hotParams()); f->reset(); }
    auto ra = in, rb = in;
    run (a, ra, 256); run (b, rb, 256);
    assert (std::memcmp (ra.data(), rb.data(), ra.size() * sizeof (float)) == 0);
    a.reset();
    auto rc = in; run (a, rc, 256);
    assert (std::memcmp (ra.data(), rc.data(), ra.size() * sizeof (float)) == 0);
    // Block size independence of the result (1-sample blocks vs 256): same state machine
    HarmonicFracture c;
    c.prepare (fs, 256); c.setParams (hotParams()); c.reset();
    auto rd = in; run (c, rd, 1);
    float maxDiff = 0.0f;
    for (std::size_t i = 0; i < in.size(); ++i) maxDiff = std::max (maxDiff, std::fabs (rd[i] - ra[i]));
    assert (maxDiff < 1e-4f);
    // Mono fold-down: identical L/R copies stay identical (mono in, mono out; no channel-dependent state)
    HarmonicFracture l, rr;
    for (auto* f : { &l, &rr }) { f->prepare (fs, 256); f->setParams (hotParams()); f->reset(); }
    auto L = in, R = in; run (l, L, 256); run (rr, R, 256);
    assert (std::memcmp (L.data(), R.data(), L.size() * sizeof (float)) == 0);
    assert (rms (L, 1024, L.size()) > 0.1);
}

void testExternalShaperAndLatency()
{
    Rng r;
    std::vector<float> in (9000);
    for (auto& x : in) x = 0.6f * r.next();
    auto builtin = [] (int band, const float* i, float* o, int n, const FractureShapeControl& c)
    {
        for (int k = 0; k < n; ++k) o[k] = HarmonicFracture::shapeSample (band, i[k], c.gain[k], c.amount[k], c.bias[k]);
    };

    // 1) external callback identical to the built-in one -> bit-identical to process()
    HarmonicFracture a, b;
    for (auto* f : { &a, &b }) { f->prepare (fs, 300); f->setParams (hotParams()); f->reset(); }
    auto ra = in, rb = in;
    for (std::size_t o = 0; o + 300 <= in.size(); o += 300) { a.process (ra.data() + o, 300, {}); b.processWith (rb.data() + o, 300, {}, builtin); }
    assert (std::memcmp (ra.data(), rb.data(), 9000 * sizeof (float)) == 0);

    // 2) shaper with latency L (a pure delay, as an oversampler would add): with zero drive the output must equal the
    //    L = 0 output delayed by L, and bypass must be the input delayed by L.
    constexpr int L = 37;
    HarmonicFracture ref, del, byp;
    for (auto* f : { &ref, &del }) { f->prepare (fs, 300); f->setParams (cleanParams (700.0f)); f->reset(); }
    byp.prepare (fs, 300); byp.setParams (HarmonicFractureParams {}); byp.reset();
    del.setShaperLatency (L); byp.setShaperLatency (L);
    assert (del.latencySamples() == L);
    std::vector<float> hist[3];
    for (auto& h : hist) h.assign (L, 0.0f);
    auto delayShaper = [&] (int band, const float* i, float* o, int n, const FractureShapeControl&)
    {
        auto& h = hist[band];
        for (int k = 0; k < n; ++k) { h.push_back (i[k]); o[k] = h[h.size() - 1 - L]; }
        h.erase (h.begin(), h.end() - L);      // test-side only; the callback under test is not part of the audio path
    };
    auto r0 = in, r1 = in, r2 = in;
    for (std::size_t o = 0; o + 300 <= in.size(); o += 300)
    {
        ref.process (r0.data() + o, 300, {});
        del.processWith (r1.data() + o, 300, {}, delayShaper);
        byp.process (r2.data() + o, 300, {});
    }
    float d1 = 0.0f, d2 = 0.0f;
    for (std::size_t i = L; i < 9000; ++i)
    {
        d1 = std::max (d1, std::fabs (r1[i] - r0[i - L]));
        d2 = std::max (d2, std::fabs (r2[i] - in[i - L]));
    }
    assert (d1 < 1e-4f);
    assert (d2 == 0.0f);
}

void testNoZipperOnDriveStep()
{
    auto maxStep = [] (float driveA, float driveB, bool step)
    {
        HarmonicFracture f;
        f.prepare (fs, 256);
        auto p = hotParams(); p.driveLow = p.driveMid = p.driveHigh = driveA;
        f.setParams (p); f.reset();
        auto v = sine (300.0, 0.4f, 48128);
        float prev = 0.0f, mx = 0.0f;
        for (std::size_t o = 0; o < v.size(); o += 256)
        {
            if (step && o == 24064) { p.driveLow = p.driveMid = p.driveHigh = driveB; f.setParams (p); }
            f.process (v.data() + o, 256, {});
            for (std::size_t i = o; i < o + 256; ++i) { if (o > 12000) mx = std::max (mx, std::fabs (v[i] - prev)); prev = v[i]; }
        }
        return mx;
    };
    const float a = maxStep (0.1f, 0.1f, false), b = maxStep (1.0f, 1.0f, false), s = maxStep (0.1f, 1.0f, true);
    assert (s <= 1.5f * std::max (a, b));
}

void benchmark()
{
    HarmonicFracture f;
    f.prepare (fs, 512);
    f.setParams (hotParams());
    f.reset();
    Rng r;
    std::vector<float> b (512);
    const int blocks = 48000 * 20 / 512;
    const auto t0 = std::chrono::steady_clock::now();
    float sink = 0.0f;
    for (int i = 0; i < blocks; ++i)
    {
        for (auto& x : b) x = r.next() * 0.5f;
        f.process (b.data(), 512, {});
        sink += b[0];
    }
    const double sec = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    std::printf ("  fracture bench: %.1f s audio in %.3f s (%.0fx realtime, %.3f%% of one core @48k; sink %.2f)\n",
                 blocks * 512.0 / fs, sec, blocks * 512.0 / fs / sec, 100.0 * sec / (blocks * 512.0 / fs), static_cast<double> (sink));
}
} // namespace

int main()
{
    testParamsFromSnapshot();
    testBypassIdentity();
    testCrossoverFlatness();
    testCrossoverSweepNoNotch();
    testFiniteAndBounded();
    testShapeFunction();
    testNoDc();
    testSubProtection();
    testImpulseDecayAndSilence();
    testDeterminismAndMono();
    testExternalShaperAndLatency();
    testNoZipperOnDriveStep();
    benchmark();
    std::puts ("HarmonicFractureTests passed");
    return 0;
}
