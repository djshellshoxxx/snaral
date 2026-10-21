// Tests for Drive, Texture, Width, Limiter and OutputTrim against docs/specs/processing-output.md.
#include "EngineOutputTestUtil.h"
#include "Dsp/Drive.h"
#include "Dsp/Limiter.h"
#include "Dsp/Texture.h"
#include "Dsp/Width.h"

#include <algorithm>
#include <iostream>

using namespace snaral;

static constexpr double kFs = 48000.0;

static std::vector<float> makeSine (double f, float amp, std::size_t n, double fs = kFs)
{
    std::vector<float> v (n);
    for (std::size_t i = 0; i < n; ++i) v[i] = static_cast<float> (amp * std::sin (2.0 * pi * f / fs * static_cast<double> (i)));
    return v;
}
static std::vector<float> makeSaw (double f, float amp, std::size_t n, double fs = kFs)
{
    std::vector<float> v (n);
    double ph = 0.0;
    for (auto& x : v) { x = static_cast<float> (amp * (2.0 * ph - 1.0)); ph += f / fs; if (ph >= 1.0) ph -= 1.0; }
    return v;
}
static void runDrive (Drive& d, std::vector<float>& v, int block, Oversampler* os = nullptr)
{
    for (std::size_t off = 0; off < v.size(); off += static_cast<std::size_t> (block))
        d.process (v.data() + off, static_cast<int> (std::min<std::size_t> (static_cast<std::size_t> (block), v.size() - off)), os);
}

// ---------------------------------------------------------------- Drive
static void testDriveFiniteBounded()
{
    Drive d; d.prepare (kFs, 512);
    for (const auto type : { DriveType::WarmSaturation, DriveType::WaveFold })
        for (const float amt : { 0.0f, 50.0f, 100.0f })
            for (const float bite : { 0.0f, 12.0f })
            {
                DriveParams p; p.type = type; p.amountPercent = amt; p.biteOffsetDb = bite; p.tonePercent = 100.0f;
                d.setParams (p); d.reset();
                std::vector<float> v (4096);
                tu::Lcg r;
                for (std::size_t i = 0; i < v.size(); ++i)
                {
                    v[i] = (i % 97 == 0) ? 1.0e30f : (i % 89 == 0 ? -3.0e38f : (i % 83 == 0 ? INFINITY : (i % 79 == 0 ? NAN : 2.0f * r.next())));
                }
                runDrive (d, v, 512);
                CHECK (tu::allFinite (v));
                for (const float x : v) CHECK (std::fabs (x) < 80.0f);
            }
    std::cout << "  drive finite/bounded for hostile input: ok\n";
}

static void testDriveDcAndSilence()
{
    for (const auto type : { DriveType::WarmSaturation, DriveType::WaveFold })
    {
        Drive d; d.prepare (kFs, 512);
        DriveParams p; p.type = type; p.amountPercent = 80.0f; p.tonePercent = 100.0f;
        d.setParams (p); d.reset();
        auto v = makeSine (110.0, 0.3f, 48000);
        for (auto& x : v) x += 0.25f;   // DC offset on the input
        runDrive (d, v, 256);
        double mean = 0.0;
        for (std::size_t i = 24000; i < 48000; ++i) mean += v[i];
        mean /= 24000.0;
        CHECK (std::fabs (mean) < 0.01);
        std::vector<float> z (48000, 0.0f);          // then silence: output decays to zero, no stuck DC
        runDrive (d, z, 256);
        CHECK (tu::rms (z, 40000, 48000) < 1.0e-5);
    }
    std::cout << "  drive DC blocked and no stuck state after silence: ok\n";
}

static double driveLoudnessSpreadDb (DriveType type, bool saw, float peak, float bite, float tone)
{
    double lo = 1.0e9, hi = -1.0e9;
    for (int amt = 0; amt <= 100; amt += 10)
    {
        Drive d; d.prepare (kFs, 512);
        DriveParams p; p.type = type; p.amountPercent = static_cast<float> (amt); p.tonePercent = tone; p.biteOffsetDb = bite;
        d.setParams (p); d.reset();
        auto v = saw ? makeSaw (110.0, peak, 48000) : makeSine (110.0, peak, 48000);
        runDrive (d, v, 256);
        const double l = tu::db (tu::rms (v, 24000, 48000));
        lo = std::min (lo, l); hi = std::max (hi, l);
    }
    return hi - lo;
}

static void testDriveCompensation()
{
    double worst = 0.0;
    for (const auto type : { DriveType::WarmSaturation, DriveType::WaveFold })
        for (const bool saw : { false, true })
            for (const float bite : { 0.0f, 12.0f })
            {
                const double spread = driveLoudnessSpreadDb (type, saw, 0.35f, bite, 50.0f);   // fixture: 110 Hz, -9 dBFS peak
                std::cout << "  loudness spread type " << (type == DriveType::WaveFold ? "fold" : "warm") << (saw ? " saw " : " sine ")
                          << "bite " << bite << " dB: " << spread << " dB\n";
                worst = std::max (worst, spread);
                CHECK (spread <= 3.0);
            }
    std::cout << "  drive compensation worst spread " << worst << " dB (limit 3.0)\n";
}

static void testDriveMixAndTypes()
{
    Drive d; d.prepare (kFs, 512);
    DriveParams p; p.amountPercent = 100.0f; p.mixPercent = 0.0f;
    d.setParams (p); d.reset();
    auto in = makeSine (220.0, 0.5f, 8000), v = in;
    runDrive (d, v, 300);
    for (std::size_t i = 0; i < v.size(); ++i) CHECK (std::fabs (v[i] - in[i]) < 1.0e-6f);   // mix 0 = dry exactly

    // the two characters respond differently
    auto a = makeSine (110.0, 0.5f, 8000), b = a;
    p.mixPercent = 100.0f; p.amountPercent = 70.0f; p.tonePercent = 100.0f;
    p.type = DriveType::WarmSaturation; d.setParams (p); d.reset(); runDrive (d, a, 256);
    p.type = DriveType::WaveFold; d.setParams (p); d.reset(); runDrive (d, b, 256);
    double diff = 0.0;
    for (std::size_t i = 4000; i < 8000; ++i) diff += std::fabs (a[i] - b[i]);
    CHECK (diff / 4000.0 > 0.02);

    // Bite offset adds bounded drive: total never exceeds maxTotalDb
    DriveParams q; q.amountPercent = 100.0f; q.biteOffsetDb = 1000.0f;
    CHECK (q.totalDriveDb() <= DriveParams::maxTotalDb + 1.0e-4f);
    ParamSnapshot s; s[P::bite] = 100.0f; s[P::drive_amount] = 0.0f;
    CHECK (std::fabs (DriveParams::from (s).totalDriveDb() - 12.0f) < 1.0e-4f);
    std::cout << "  drive mix/type/bite mapping: ok\n";
}

static void testDriveOversampled()
{
    // Drive 0 + wave_fold is the identity for |x| <= 1, so the oversampled/aligned path must equal delayed dry.
    for (const auto mode : { OversamplingMode::X2, OversamplingMode::X4 })
    {
        Oversampler os; os.prepare (kFs, 512); os.setMode (mode);
        Drive d; d.prepare (kFs, 512);
        DriveParams p; p.type = DriveType::WaveFold; p.amountPercent = 0.0f; p.tonePercent = 100.0f; p.mixPercent = 50.0f;
        d.setParams (p); d.reset();
        auto in = makeSine (330.0, 0.4f, 12000), v = in;
        runDrive (d, v, 256, &os);
        const int lat = os.latencySamples();
        double err = 0.0;
        for (std::size_t i = 6000; i < 12000; ++i) err = std::max (err, static_cast<double> (std::fabs (v[i] - in[i - static_cast<std::size_t> (lat)])));
        std::cout << "  oversampled drive (" << oversamplingFactor (mode) << "x) aligned-identity max error " << err << "\n";
        CHECK (err < 0.01);
    }
    // Aliasing improves with oversampling for a hard-driven non-bandlimited shape.
    auto alias = [] (OversamplingMode mode)
    {
        constexpr std::size_t N = 16384; constexpr int bin = 1027;
        Oversampler os; os.prepare (kFs, 512); os.setMode (mode);
        Drive d; d.prepare (kFs, 512);
        DriveParams p; p.type = DriveType::WaveFold; p.amountPercent = 100.0f; p.tonePercent = 100.0f;
        d.setParams (p); d.reset();
        std::vector<float> v (2 * N);
        for (std::size_t i = 0; i < v.size(); ++i) v[i] = static_cast<float> (0.35 * std::sin (2.0 * pi * bin * static_cast<double> (i) / static_cast<double> (N)));
        runDrive (d, v, 512, &os);
        const auto sp = tu::powerSpectrum (v.data() + N, N);
        double al = 0.0;
        for (std::size_t k = 1; k < sp.size(); ++k) if (k % bin != 0) al += sp[k];
        double total = 0.0;
        for (std::size_t k = 1; k < sp.size(); ++k) total += sp[k];
        return 10.0 * std::log10 (std::max (al, 1.0e-30) / total);
    };
    const double a0 = alias (OversamplingMode::Off), a2 = alias (OversamplingMode::X2), a4 = alias (OversamplingMode::X4);
    std::cout << "  drive (wave_fold, 36 dB) alias vs total: Off " << a0 << " dB, 2x " << a2 << " dB, 4x " << a4 << " dB\n";
    CHECK (a2 < a0 && a4 < a2);
}

// ---------------------------------------------------------------- Texture
static void testTextureOffSilent()
{
    for (const double fs : { 44100.0, 96000.0 })
    {
        TextureLayer t; t.prepare (fs, 1u);
        t.setParams (TextureParams {});   // levels 0
        t.noteOn (40.0f);
        std::vector<float> buf (4096, 0.0f);
        t.addTo (buf.data(), 4096);
        for (const float x : buf) CHECK (x == 0.0f);
    }
    // Growl = 0 silences even at max levels
    ParamSnapshot s; s[P::noise_level] = 100.0f; s[P::metal_level] = 100.0f; s[P::growl] = 0.0f;
    TextureLayer t; t.prepare (kFs, 7u); t.setParams (TextureParams::from (s)); t.noteOn (36.0f);
    std::vector<float> buf (8192, 0.0f);
    t.addTo (buf.data(), 8192);
    for (const float x : buf) CHECK (x == 0.0f);
    // Ramp growl up: texture appears; ramp back down to 0: silent again, exactly
    s[P::growl] = 100.0f; t.setParams (TextureParams::from (s));
    t.addTo (buf.data(), 8192);
    CHECK (tu::rms (buf, 4096, 8192) > 0.01);
    s[P::growl] = 0.0f; t.setParams (TextureParams::from (s));
    std::vector<float> after (48000, 0.0f);
    t.addTo (after.data(), 48000);
    for (std::size_t i = 24000; i < after.size(); ++i) CHECK (after[i] == 0.0f);
    std::cout << "  texture silent at level 0 and at Growl 0: ok\n";
}

static void testNoise()
{
    TextureParams p; p.noiseLevel = 1.0f; p.noiseTone = 1.0f;
    auto gen = [&] (std::uint32_t seed, float tone)
    {
        NoiseGen n; n.prepare (kFs, seed); p.noiseTone = tone;
        n.setParams (p.noiseLevel, p.noiseTone, 1.0f);
        std::vector<float> v (20000, 0.0f);
        n.addTo (v.data(), 20000);
        return v;
    };
    const auto a = gen (5, 1.0f), b = gen (5, 1.0f), c = gen (6, 1.0f);
    CHECK (a == b);                       // deterministic
    CHECK (a != c);                       // seed matters
    CHECK (tu::allFinite (a));
    float peak = 0.0f;
    for (const float x : a) peak = std::max (peak, std::fabs (x));
    CHECK (peak <= NoiseGen::maxAmplitude + 1.0e-6f);
    // tone: dark noise has far less sample-to-sample energy than white noise
    const auto dark = gen (5, 0.0f);
    auto hf = [] (const std::vector<float>& v) { double e = 0.0; for (std::size_t i = 1000; i < v.size(); ++i) { const double d = v[i] - v[i - 1]; e += d * d; } return e; };
    std::cout << "  noise HF energy dark/white: " << hf (dark) / hf (a) << "\n";
    CHECK (hf (dark) < 0.1 * hf (a));
}

static void testMetal()
{
    static_assert (MetalBank::maxPartials <= 6);
    // Stable, finite and bounded for every note/tune/decay at max level and several sample rates.
    double worstPeak = 0.0;
    for (const double fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (const float note : { 0.0f, 24.0f, 36.0f, 60.0f, 96.0f, 127.0f })
            for (const float tune : { 0.0f, 0.5f, 1.0f })
                for (const float decay : { 10.0f, 300.0f, 2000.0f })
                {
                    MetalBank m; m.prepare (fs); m.setLevel (1.0f, 1.0f);
                    m.noteOn (note, tune, decay, 1.0f);
                    std::vector<float> v (static_cast<std::size_t> (fs * 0.5), 0.0f);
                    m.addTo (v.data(), static_cast<int> (v.size()));
                    CHECK (tu::allFinite (v));
                    for (const float x : v) { worstPeak = std::max (worstPeak, static_cast<double> (std::fabs (x))); CHECK (std::fabs (x) <= MetalBank::maxAmplitude + 1.0e-4f); }
                }
    std::cout << "  metal worst peak at max level: " << worstPeak << " (bound " << MetalBank::maxAmplitude << ")\n";

    // tune tracking: +12 semitones doubles the dominant frequency
    auto domFreq = [] (float note)
    {
        MetalBank m; m.prepare (kFs); m.setLevel (1.0f, 1.0f); m.noteOn (note, 0.5f, 2000.0f);
        std::vector<float> v (16384, 0.0f);
        m.addTo (v.data(), 16384);
        const auto sp = tu::powerSpectrum (v.data(), 16384);
        std::size_t best = 1;
        for (std::size_t k = 1; k < sp.size(); ++k) if (sp[k] > sp[best]) best = k;
        return static_cast<double> (best) * kFs / 16384.0;
    };
    const double f36 = domFreq (36.0f), f48 = domFreq (48.0f);
    std::cout << "  metal dominant frequency note 36: " << f36 << " Hz, note 48: " << f48 << " Hz\n";
    CHECK (std::fabs (f48 / f36 - 2.0) < 0.03);

    // per-note decay: partial 0 reaches -60 dB at metal_decay_ms; whole bank is small afterwards and goes inactive
    MetalBank m; m.prepare (kFs); m.setLevel (1.0f, 1.0f); m.noteOn (36.0f, 0.5f, 100.0f);
    std::vector<float> v (48000 * 3, 0.0f);
    for (std::size_t off = 0; off < v.size(); off += 480) m.addTo (v.data() + off, 480);
    double early = 0.0, late = 0.0;
    for (std::size_t i = 0; i < 480; ++i) early = std::max (early, static_cast<double> (std::fabs (v[i])));
    for (std::size_t i = 9600; i < 9600 + 480; ++i) late = std::max (late, static_cast<double> (std::fabs (v[i])));   // 200-210 ms
    std::cout << "  metal peak first 10 ms " << early << ", at 200 ms " << late << "\n";
    CHECK (late < early * 0.01);
    CHECK (! m.isActive());
    CHECK (std::fabs (v.back()) == 0.0f);

    // level 0 -> exact zero even right after noteOn; growlGain 0 -> exact zero
    MetalBank z; z.prepare (kFs); z.setLevel (0.0f, 1.0f); z.noteOn (40.0f, 0.5f, 500.0f);
    std::vector<float> zz (2048, 0.0f); z.addTo (zz.data(), 2048);
    for (const float x : zz) CHECK (x == 0.0f);
}

// ---------------------------------------------------------------- Width
static void testWidth()
{
    Width w; w.prepare (kFs, 512);
    // width 0: identical L/R, equal to mid
    w.setParams ({ 0.0f }); w.reset();
    tu::Lcg r;
    std::vector<float> mid (8192), L (8192), R (8192);
    for (auto& x : mid) x = 0.5f * r.next();
    w.process (mid.data(), L.data(), R.data(), 8192);
    for (std::size_t i = 0; i < mid.size(); ++i) { CHECK (L[i] == R[i]); CHECK (L[i] == mid[i]); }

    // width 100%: channels differ, decorrelated; mono fold-down keeps the mid
    struct Sig { const char* name; std::vector<float> v; };
    std::vector<Sig> sigs;
    { std::vector<float> nz (16384); for (auto& x : nz) x = 0.5f * r.next(); sigs.push_back ({ "noise", nz }); }
    sigs.push_back ({ "saw 110", makeSaw (110.0, 0.5f, 16384) });
    sigs.push_back ({ "sine 55", makeSine (55.0, 0.5f, 16384) });
    sigs.push_back ({ "sine 2k", makeSine (2000.0, 0.5f, 16384) });
    sigs.push_back ({ "sine 12k", makeSine (12000.0, 0.5f, 16384) });
    double worstFold = 0.0;
    for (const float pct : { 25.0f, 50.0f, 100.0f, 120.0f })
        for (const auto& s : sigs)
        {
            Width ww; ww.prepare (kFs, 512); ww.setParams ({ pct }); ww.reset();
            std::vector<float> l (s.v.size()), rr (s.v.size());
            // let the 20 ms smoother settle first
            for (int pass = 0; pass < 2; ++pass) ww.process (s.v.data(), l.data(), rr.data(), static_cast<int>(s.v.size()));
            std::vector<float> mono (s.v.size());
            for (std::size_t i = 0; i < mono.size(); ++i) mono[i] = 0.5f * (l[i] + rr[i]);
            const double fold = tu::db (tu::rms (mono, 8192, 16384) / tu::rms (s.v, 8192, 16384));
            worstFold = std::min (worstFold, fold);
            CHECK (fold >= -20.0);
            CHECK (tu::allFinite (l) && tu::allFinite (rr));
        }
    std::cout << "  width worst mono fold-down level vs mid: " << worstFold << " dB (requirement >= -20)\n";

    // decorrelation at 100% (space = 83.3 %): normalised correlation of L and R for noise below 0.9
    Width w1; w1.prepare (kFs, 512); w1.setParams ({ 100.0f }); w1.reset();
    std::vector<float> n2 (16384), l1 (16384), r1 (16384);
    for (auto& x : n2) x = 0.5f * r.next();
    w1.process (n2.data(), l1.data(), r1.data(), 16384);
    double lr = 0.0, ll = 0.0, rrr = 0.0;
    for (std::size_t i = 8192; i < 16384; ++i) { lr += l1[i] * r1[i]; ll += l1[i] * l1[i]; rrr += r1[i] * r1[i]; }
    const double corr = lr / std::sqrt (ll * rrr);
    std::cout << "  width 100% L/R correlation on noise: " << corr << "\n";
    CHECK (corr < 0.9 && corr > -0.9);

    // Space macro mapping reaches 0..120 %
    ParamSnapshot s; s[P::space] = 100.0f;
    CHECK (std::fabs (WidthParams::from (s).widthPercent - 120.0f) < 1.0e-4f);

    // width ramps smoothly back to exactly mono (no stuck state)
    w1.setParams ({ 0.0f });
    for (int i = 0; i < 10; ++i) w1.process (n2.data(), l1.data(), r1.data(), 16384);
    for (std::size_t i = 0; i < 1024; ++i) CHECK (l1[i] == r1[i]);

    // hostile input stays finite
    std::vector<float> bad (1024, NAN), lb (1024), rb (1024);
    bad[10] = INFINITY; bad[20] = 1.0e38f;
    w1.process (bad.data(), lb.data(), rb.data(), 1024);
    CHECK (tu::allFinite (lb) && tu::allFinite (rb));
    std::cout << "  width: mono at 0, decorrelated at 100, safe fold-down: ok\n";
}

// ---------------------------------------------------------------- Limiter
struct LimResult { double maxOutDb; std::uint64_t clamps; };

static LimResult runLimiter (const std::vector<float>& in, float ceilingDb, int block)
{
    Limiter lim; lim.prepare (kFs, 1024);
    lim.setParams ({ true, ceilingDb }); lim.reset();
    std::vector<float> l = in, r = in;
    for (std::size_t off = 0; off < in.size(); off += static_cast<std::size_t> (block))
    {
        const int n = static_cast<int> (std::min<std::size_t> (static_cast<std::size_t> (block), in.size() - off));
        lim.process (l.data() + off, r.data() + off, n);
    }
    CHECK (tu::allFinite (l));
    float pk = 0.0f;
    for (const float x : l) pk = std::max (pk, std::fabs (x));
    return { tu::db (pk), lim.safetyClampHits() };
}

static void testLimiterCeiling()
{
    // Operating envelope: band-limited content up to 12 kHz (0.25 fs), peak up to +12 dBFS, ceilings -12..0.
    double worstOver = -100.0;
    std::uint64_t clamps = 0;
    for (const float ceil : { -12.0f, -6.0f, -1.0f, 0.0f })
        for (const float amp : { 0.5f, 1.0f, 2.0f, 4.0f })
        {
            for (const double f : { 40.0, 110.0, 440.0, 3000.0, 12000.0 })
            {
                auto s = makeSine (f, amp, 24000);
                const auto res = runLimiter (s, ceil, 256);
                worstOver = std::max (worstOver, res.maxOutDb - ceil); clamps += res.clamps;
                CHECK (res.maxOutDb <= ceil + 0.1);
            }
            for (const double f : { 55.0, 110.0, 800.0 })
            {
                auto s = makeSaw (f, amp, 24000);
                const auto res = runLimiter (s, ceil, 100);
                worstOver = std::max (worstOver, res.maxOutDb - ceil); clamps += res.clamps;
                CHECK (res.maxOutDb <= ceil + 0.1);
            }
            // sudden loud burst after quiet, and a square wave (worst case for sample peaks)
            std::vector<float> burst (24000, 0.01f);
            for (std::size_t i = 6000; i < 9000; ++i) burst[i] = amp * ((i / 60) % 2 ? 1.0f : -1.0f);
            const auto res = runLimiter (burst, ceil, 64);
            worstOver = std::max (worstOver, res.maxOutDb - ceil); clamps += res.clamps;
            CHECK (res.maxOutDb <= ceil + 0.1);
        }
    std::cout << "  limiter worst output over ceiling: " << worstOver << " dB (limit +0.1), safety clamp hits " << clamps << "\n";
    CHECK (clamps == 0);
}

static void testLimiterLatencyBypassStuck()
{
    Limiter lim; lim.prepare (kFs, 512);
    const int lat = lim.latencySamples();
    std::cout << "  limiter latency " << lat << " samples (" << 1000.0 * lat / kFs << " ms @48k)\n";
    CHECK (lat >= 2 && lat <= 64);
    // enabled but below the ceiling: pure delay of exactly `lat`
    std::vector<float> l (4000, 0.0f), r;
    l[300] = 0.1f; r = l;
    lim.setParams ({ true, -1.0f }); lim.reset();
    lim.process (l.data(), r.data(), 4000);
    std::size_t pk = 0;
    for (std::size_t i = 0; i < l.size(); ++i) if (std::fabs (l[i]) > std::fabs (l[pk])) pk = i;
    CHECK (static_cast<int> (pk) == 300 + lat);
    CHECK (l[pk] == 0.1f);
    // bypass keeps the same latency and is bit-exact
    Limiter lb; lb.prepare (kFs, 512);
    lb.setParams ({ false, -1.0f }); lb.reset();
    std::vector<float> a (4000, 0.0f); a[300] = 3.0f; auto b = a;
    lb.process (a.data(), b.data(), 4000);
    CHECK (a[300 + static_cast<std::size_t> (lat)] == 3.0f);
    CHECK (lb.latencySamples() == lat);

    // enable/bypass toggling while limiting a loud sine: no click (steps stay near the sine's own slope)
    Limiter lt; lt.prepare (kFs, 256);
    lt.setParams ({ true, -12.0f }); lt.reset();
    auto sig = makeSine (110.0, 2.0f, 46080);
    std::vector<float> out = sig, out2 = sig;
    for (std::size_t off = 0; off < sig.size(); off += 256)
    {
        if (off % 5120 == 0) lt.setParams ({ (off / 5120) % 2 == 0, -12.0f });
        lt.process (out.data() + off, out2.data() + off, 256);
    }
    float maxStep = 0.0f;
    for (std::size_t i = 1; i < out.size(); ++i) maxStep = std::max (maxStep, std::fabs (out[i] - out[i - 1]));
    const float naturalStep = 2.0f * static_cast<float> (2.0 * pi * 110.0 / kFs);   // unlimited sine max step
    std::cout << "  limiter toggle: max step " << maxStep << " vs unlimited sine " << naturalStep << "\n";
    CHECK (maxStep < 1.25f * naturalStep);

    // no stuck state: after a loud burst the gain returns to exactly 1 and output equals delayed input
    Limiter ls; ls.prepare (kFs, 512);
    ls.setParams ({ true, -12.0f }); ls.reset();
    std::vector<float> loud (9600, 3.0f), quiet (96000, 0.0f);
    for (std::size_t i = 0; i < quiet.size(); ++i) quiet[i] = static_cast<float> (0.05 * std::sin (0.05 * static_cast<double> (i)));
    ls.process (loud.data(), nullptr, static_cast<int> (loud.size()));
    CHECK (ls.currentGain() < 0.2f);
    ls.process (quiet.data(), nullptr, static_cast<int> (quiet.size()));
    CHECK (ls.currentGain() == 1.0f);
    // sweep ceiling and enable randomly, then come back to a benign state
    tu::Lcg rnd;
    std::vector<float> blk (128);
    for (int k = 0; k < 400; ++k)
    {
        ls.setParams ({ rnd.next() > 0.0f, -12.0f * (0.5f + 0.5f * rnd.next()) });
        for (auto& x : blk) x = 2.0f * rnd.next();
        ls.process (blk.data(), blk.data(), 128);   // mono (aliased) path
        CHECK (tu::allFinite (blk));
    }
    ls.setParams ({ true, -1.0f });
    std::vector<float> tail (96000, 0.02f);
    ls.process (tail.data(), nullptr, static_cast<int> (tail.size()));
    CHECK (ls.currentGain() == 1.0f);
    CHECK (tail.back() == 0.02f);

    // hostile input and denormals
    Limiter lh; lh.prepare (kFs, 512); lh.setParams ({ true, -6.0f }); lh.reset();
    std::vector<float> h (2048, 0.0f), h2;
    for (std::size_t i = 0; i < h.size(); ++i) h[i] = (i % 50 == 0) ? NAN : ((i % 71 == 0) ? INFINITY : ((i % 3 == 0) ? 1.0e-40f : 1.0e30f * ((i & 1) ? 1.0f : -1.0f)));
    h2 = h;
    lh.process (h.data(), h2.data(), 2048);
    CHECK (tu::allFinite (h) && tu::allFinite (h2));
    for (const float x : h) CHECK (std::fpclassify (x) != FP_SUBNORMAL);
    std::vector<float> dn (4096, 1.0e-40f);
    lh.process (dn.data(), nullptr, 4096);
    for (const float x : dn) CHECK (std::fpclassify (x) != FP_SUBNORMAL);
}

static void testTrim()
{
    OutputTrim t; t.prepare (kFs);
    for (const float db : { -24.0f, -6.0f, 0.0f, 6.0f, 12.0f, -100.0f, 100.0f, -3.0f })
    {
        t.setTrimDb (db);
        std::vector<float> a (24000, 1.0f), b = a;
        t.process (a.data(), b.data(), 24000);
        const float expect = dbToGain (clampf (db, -24.0f, 12.0f));
        CHECK (a.back() == expect && b.back() == expect);       // settles exactly, never stuck short of target
        CHECK (t.isSettled());
        CHECK (tu::allFinite (a));
    }
    // smooth: a 36 dB jump does not step instantly
    t.setTrimDb (-24.0f);
    std::vector<float> a (24000, 1.0f); t.process (a.data(), nullptr, 24000);
    t.setTrimDb (12.0f);
    std::vector<float> c (1, 1.0f);
    t.process (c.data(), nullptr, 1);
    CHECK (c[0] < 0.2f);
    std::cout << "  output trim settles exactly and smoothly across -24..+12 dB: ok\n";
}

static void testNoAllocationAndSubIsolation()
{
    Drive d; d.prepare (kFs, 512);
    Oversampler os; os.prepare (kFs, 512);
    TextureLayer tl; tl.prepare (kFs, 3u);
    Width w; w.prepare (kFs, 512);
    Limiter lim; lim.prepare (kFs, 512);
    OutputTrim trim; trim.prepare (kFs);
    std::vector<float> mid (512), l (512), r (512);
    tu::Lcg rnd;
    for (auto& x : mid) x = 0.4f * rnd.next();
    NoAlloc guard;
    for (int i = 0; i < 200; ++i)
    {
        DriveParams dp; dp.type = (i & 1) ? DriveType::WaveFold : DriveType::WarmSaturation; dp.amountPercent = static_cast<float> (i % 100);
        d.setParams (dp);
        os.setMode (i % 3 == 0 ? OversamplingMode::X4 : (i % 3 == 1 ? OversamplingMode::X2 : OversamplingMode::Off));
        TextureParams tp; tp.noiseLevel = 0.5f; tp.metalLevel = 0.5f; tl.setParams (tp);
        if (i % 10 == 0) tl.noteOn (36.0f + static_cast<float> (i % 24));
        tl.addTo (mid.data(), 512);
        d.process (mid.data(), 512, &os);
        w.setParams ({ static_cast<float> ((i * 7) % 121) });
        w.process (mid.data(), l.data(), r.data(), 512);
        lim.setParams ({ (i % 4) != 0, -1.0f - static_cast<float> (i % 10) });
        trim.setTrimDb (static_cast<float> ((i % 37) - 24));
        trim.process (l.data(), r.data(), 512);
        lim.process (l.data(), r.data(), 512);
        for (auto& x : mid) x = 0.4f * rnd.next();
    }
    CHECK (guard.count() == 0);
    std::cout << "  zero heap allocations in 200 blocks of Drive+Os+Texture+Width+Trim+Limiter: ok\n";

    // Sub isolation: these blocks only ever receive the mid buffer; summing a sub after Width adds it
    // identically to both channels, so L-R is independent of the sub (checked at width 120 %).
    Width w2; w2.prepare (kFs, 512); w2.setParams ({ 120.0f }); w2.reset();
    auto m = makeSaw (80.0, 0.3f, 4096), L1 = m, R1 = m, L2 = m, R2 = m;
    w2.process (m.data(), L1.data(), R1.data(), 4096);
    Width w3; w3.prepare (kFs, 512); w3.setParams ({ 120.0f }); w3.reset();
    w3.process (m.data(), L2.data(), R2.data(), 4096);
    auto sub = makeSine (45.0, 0.6f, 4096);
    for (std::size_t i = 0; i < 4096; ++i) { L2[i] += sub[i]; R2[i] += sub[i]; }
    for (std::size_t i = 0; i < 4096; ++i) CHECK (std::fabs ((L2[i] - R2[i]) - (L1[i] - R1[i])) < 1.0e-5f);
}

int main()
{
    std::cout << std::unitbuf << "OutputStageTests\n";
    testDriveFiniteBounded();
    testDriveDcAndSilence();
    testDriveCompensation();
    testDriveMixAndTypes();
    testDriveOversampled();
    testTextureOffSilent();
    testNoise();
    testMetal();
    testWidth();
    testLimiterCeiling();
    testLimiterLatencyBypassStuck();
    testTrim();
    testNoAllocationAndSubIsolation();
    std::cout << "OutputStageTests passed\n";
    return 0;
}
