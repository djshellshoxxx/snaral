#include "EngineOutputTestUtil.h"
#include "Dsp/Oversampler.h"

#include <algorithm>
#include <iostream>

using namespace snaral;

static constexpr double kFs = 48000.0;

static std::vector<float> runIdentity (OversamplingMode mode, const std::vector<float>& in, int block)
{
    Oversampler os;
    os.prepare (kFs, 1024);
    os.setMode (mode);
    std::vector<float> out (in);
    for (std::size_t off = 0; off < in.size(); off += static_cast<std::size_t> (block))
    {
        const int n = static_cast<int> (std::min<std::size_t> (static_cast<std::size_t> (block), in.size() - off));
        os.process (out.data() + off, n, [] (float*, int) {});
    }
    return out;
}

static void testLatencyAndOff()
{
    for (const auto mode : { OversamplingMode::Off, OversamplingMode::X2, OversamplingMode::X4 })
    {
        std::vector<float> in (2048, 0.0f);
        in[200] = 1.0f;
        // process the impulse in odd-sized blocks to cross chunk boundaries
        const auto out = runIdentity (mode, in, 331);
        std::size_t peak = 0;
        for (std::size_t i = 0; i < out.size(); ++i) if (std::fabs (out[i]) > std::fabs (out[peak])) peak = i;
        const int measured = static_cast<int> (peak) - 200;
        Oversampler os; os.prepare (kFs, 64); os.setMode (mode); os.commitMode();
        std::cout << "  mode " << oversamplingFactor (mode) << "x: reported latency " << os.latencySamples()
                  << ", measured " << measured << ", peak " << out[peak] << "\n";
        CHECK (measured == os.latencySamples());
        CHECK (os.latencySamples() == Oversampler::latencyFor (mode));
        if (mode == OversamplingMode::Off)
        {
            CHECK (os.latencySamples() == 0);
            for (std::size_t i = 0; i < in.size(); ++i) CHECK (out[i] == in[i]);   // bit-exact pass-through
        }
        else
            CHECK (out[peak] > 0.9f && out[peak] < 1.01f);   // band-limited impulse peaks slightly below 1
    }
}

static void testPassbandFlatness()
{
    // Steady sine gain through an identity nonlinearity, up to 0.40 * fs.
    for (const auto mode : { OversamplingMode::X2, OversamplingMode::X4 })
    {
        double worst = 0.0;
        for (const double f : { 60.0, 500.0, 2000.0, 6000.0, 10000.0, 14000.0, 17000.0, 19200.0 })
        {
            std::vector<float> in (8192);
            for (std::size_t i = 0; i < in.size(); ++i) in[i] = static_cast<float> (0.5 * std::sin (2.0 * pi * f / kFs * static_cast<double> (i)));
            const auto out = runIdentity (mode, in, 256);
            const double g = tu::db (tu::rms (out, 4096, 8192) / tu::rms (in, 4096, 8192));
            worst = std::max (worst, std::fabs (g));
            CHECK (std::fabs (g) < 0.1);
        }
        std::cout << "  " << oversamplingFactor (mode) << "x passband: worst |gain error| to 19.2 kHz @48k = " << worst << " dB\n";
    }
}

static double aliasDb (OversamplingMode mode, float drive)
{
    constexpr std::size_t N = 16384;
    constexpr int bin = 1027;   // coherent tone: 1027 periods in N samples (3008.8 Hz at 48k)
    std::vector<float> in (N * 2);
    for (std::size_t i = 0; i < in.size(); ++i)
        in[i] = static_cast<float> (0.5 * std::sin (2.0 * pi * bin * static_cast<double> (i) / static_cast<double> (N)));
    Oversampler os; os.prepare (kFs, 1024); os.setMode (mode);
    std::vector<float> out (in);
    for (std::size_t off = 0; off < out.size(); off += 512)
        os.process (out.data() + off, 512, [drive] (float* b, int n) { for (int i = 0; i < n; ++i) b[i] = std::clamp (b[i] * drive, -1.0f, 1.0f); });
    const auto p = tu::powerSpectrum (out.data() + N, N);   // second half: steady state, still coherent
    double harm = 0.0, alias = 0.0, fund = p[bin];
    for (std::size_t k = 1; k < p.size(); ++k)
    {
        const bool isHarm = (k % bin) == 0;
        (isHarm ? harm : alias) += p[k];
    }
    (void) harm;
    return 10.0 * std::log10 (std::max (alias, 1.0e-30) / fund);
}

static void testAliasRejection()
{
    const double off = aliasDb (OversamplingMode::Off, 8.0f);
    const double x2 = aliasDb (OversamplingMode::X2, 8.0f);
    const double x4 = aliasDb (OversamplingMode::X4, 8.0f);
    std::cout << "  alias power vs fundamental, hard clip x8 of 3.0 kHz sine @48k: Off " << off << " dB, 2x " << x2 << " dB, 4x " << x4 << " dB\n";
    CHECK (x2 < off - 10.0);
    CHECK (x4 < x2 - 6.0);
}

static void testDryAlignment()
{
    for (const auto mode : { OversamplingMode::X2, OversamplingMode::X4 })
    {
        Oversampler os; os.prepare (kFs, 256); os.setMode (mode); os.commitMode();
        DelayAlign dry; dry.prepare (Oversampler::maxLatency); dry.setDelay (os.latencySamples());
        std::vector<float> in (6000), wet, aligned;
        for (std::size_t i = 0; i < in.size(); ++i) in[i] = static_cast<float> (0.4 * std::sin (2.0 * pi * 220.0 / kFs * static_cast<double> (i)));
        wet = in;
        for (std::size_t off = 0; off < wet.size(); off += 200)
            os.process (wet.data() + off, 200, [] (float*, int) {});
        for (const float x : in) aligned.push_back (dry.process (x));
        double err = 0.0;
        for (std::size_t i = 2000; i < 6000; ++i) err = std::max (err, static_cast<double> (std::fabs (wet[i] - aligned[i])));
        std::cout << "  " << oversamplingFactor (mode) << "x dry/wet alignment max error " << err << "\n";
        CHECK (err < 1.0e-3);
    }
}

static void testBlockIndependenceAndSwitching()
{
    std::vector<float> in (5000);
    tu::Lcg r;
    for (auto& x : in) x = 0.5f * r.next();
    const auto a = runIdentity (OversamplingMode::X4, in, 5000 / 5);
    const auto b = runIdentity (OversamplingMode::X4, in, 37);
    const auto c = runIdentity (OversamplingMode::X4, in, 1);
    for (std::size_t i = 0; i < in.size(); ++i) { CHECK (a[i] == b[i]); CHECK (a[i] == c[i]); }

    // mode changes every block during a sustained tone: finite, bounded, no big discontinuities
    Oversampler os; os.prepare (kFs, 256);
    std::vector<float> sig (256), all;
    double ph = 0.0;
    const OversamplingMode seq[] = { OversamplingMode::Off, OversamplingMode::X2, OversamplingMode::X4, OversamplingMode::X2, OversamplingMode::Off, OversamplingMode::X4 };
    for (int blk = 0; blk < 60; ++blk)
    {
        os.setMode (seq[blk % 6]);
        for (auto& s : sig) { s = static_cast<float> (0.5 * std::sin (ph)); ph += 2.0 * pi * 110.0 / kFs; }
        os.process (sig.data(), 256, [] (float* b2, int n) { for (int i = 0; i < n; ++i) b2[i] = std::tanh (2.0f * b2[i]); });
        all.insert (all.end(), sig.begin(), sig.end());
    }
    CHECK (tu::allFinite (all));
    float maxAbs = 0.0f, maxStep = 0.0f;
    for (std::size_t i = 1; i < all.size(); ++i) { maxAbs = std::max (maxAbs, std::fabs (all[i])); maxStep = std::max (maxStep, std::fabs (all[i] - all[i - 1])); }
    std::cout << "  mode-switch stress: max |y| " << maxAbs << ", max step " << maxStep << "\n";
    CHECK (maxAbs < 1.0f);
    CHECK (maxStep < 0.05f);   // natural 110 Hz step is ~0.007; no full-scale click at a switch
}

static void testNoAllocation()
{
    Oversampler os; os.prepare (kFs, 512); os.setMode (OversamplingMode::X4);
    std::vector<float> buf (512, 0.1f);
    NoAlloc guard;
    for (int i = 0; i < 100; ++i)
    {
        os.setMode (i % 3 == 0 ? OversamplingMode::X4 : (i % 3 == 1 ? OversamplingMode::X2 : OversamplingMode::Off));
        os.process (buf.data(), 512, [] (float* b, int n) { for (int k = 0; k < n; ++k) b[k] *= 0.5f; });
    }
    CHECK (guard.count() == 0);
}

int main()
{
    std::cout << std::unitbuf << "OversamplerTests\n";
    testLatencyAndOff();
    testPassbandFlatness();
    testAliasRejection();
    testDryAlignment();
    testBlockIndependenceAndSwitching();
    testNoAllocation();
    std::cout << "OversamplerTests passed\n";
    return 0;
}
