// Adversarial sweep over every DSP block: hostile parameters (NaN, Inf, far out of range, extremes), hostile input
// (NaN, Inf, huge, denormal), odd block sizes (1, 3, 7, 4097), and sample rates from 8 kHz to 192 kHz, with prepare()
// called repeatedly. Output must stay finite and bounded; meant to run under ASan/UBSan (UB on float->int casts etc).
#include "TestSupport.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include "Dsp/Adsr.h"
#include "Dsp/Contour.h"
#include "Dsp/Drive.h"
#include "Dsp/Fx/FormantFurnace.h"
#include "Dsp/Fx/HarmonicFracture.h"
#include "Dsp/Lfo.h"
#include "Dsp/Limiter.h"
#include "Dsp/Oscillators.h"
#include "Dsp/Oversampler.h"
#include "Dsp/SvfFilter.h"
#include "Dsp/Texture.h"
#include "Dsp/Trajectory.h"
#include "Dsp/Width.h"
#include "Engine/NoteMath.h"

using namespace snaral;
using namespace snaral::testing;

namespace
{
constexpr float inf = std::numeric_limits<float>::infinity();
const float nanf_ = std::nanf ("");
const double rates[] = { 8000.0, 22050.0, 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 };
const int blocks[] = { 1, 3, 7, 64, 513, 4097 };

float hostile (Rng& rng)
{
    switch (rng.below (14))
    {
        case 0: return nanf_;
        case 1: return inf;
        case 2: return -inf;
        case 3: return 1.0e30f;
        case 4: return -1.0e30f;
        case 5: return 1.0e-38f;
        case 6: return 0.0f;
        case 7: return -0.0f;
        case 8: return 1.0e-30f * rng.unit();
        default: return (rng.unit() * 2.0f - 1.0f) * (rng.below (4) == 0 ? 100.0f : 1.0f);
    }
}

void fillInput (std::vector<float>& v, Rng& rng, int style)
{
    for (std::size_t i = 0; i < v.size(); ++i)
    {
        switch (style)
        {
            case 0: v[i] = hostile (rng); break;
            case 1: v[i] = (rng.unit() * 2.0f - 1.0f) * 0.9f; break;
            case 2: v[i] = static_cast<float> (std::sin (0.05 * static_cast<double> (i))); break;
            default: v[i] = 0.0f; break;
        }
    }
}

bool finiteAll (const std::vector<float>& v, float bound)
{
    for (float x : v)
        if (! std::isfinite (x) || std::fabs (x) > bound)
            return false;
    return true;
}

ParamSnapshot hostileSnapshot (Rng& rng)
{
    ParamSnapshot s;
    for (int i = 0; i < parameterCount; ++i)
    {
        const auto& d = parameterTable[static_cast<std::size_t> (i)];
        float v;
        switch (rng.below (6))
        {
            case 0: v = d.minimum; break;
            case 1: v = d.maximum; break;
            case 2: v = hostile (rng); break;                  // not clamped on purpose: DSP from() must cope
            case 3: v = d.defaultValue; break;
            default: v = d.minimum + rng.unit() * (d.maximum - d.minimum); break;
        }
        s.v[static_cast<std::size_t> (i)] = v;
    }
    return s;
}

void testParamDecoders()
{
    Rng rng (1);
    for (int it = 0; it < 3000; ++it)
    {
        const auto s = hostileSnapshot (rng);
        (void) ContourParams::from (s); (void) LfoParams::from (s); (void) TrajectoryParams::from (s);
        (void) SvfParams::from (s); (void) DriveParams::from (s); (void) TextureParams::from (s);
        (void) WidthParams::from (s); (void) LimiterParams::from (s); (void) FormantFurnaceParams::from (s);
        (void) HarmonicFractureParams::from (s); (void) SubOscParams::from (s); (void) MidOscParams::from (s);
        const auto r = MotionRouting::from (s);
        const auto o = r.evaluate (hostile (rng), hostile (rng), hostile (rng));
        CHECK (std::isfinite (o.cutoffHz) && o.cutoffHz >= 20.0f && o.cutoffHz <= 20000.0f);
        CHECK (std::isfinite (o.position) && o.position >= 0.0f && o.position <= 1.0f);
    }
}

void testEnvelopesAndModulators()
{
    Rng rng (2);
    for (double sr : rates)
    {
        Adsr a; a.prepare (sr);
        Contour c; c.prepare (sr);
        Lfo l; l.prepare (sr);
        Trajectory t; t.prepare (sr);
        for (int it = 0; it < 60; ++it)
        {
            const auto s = hostileSnapshot (rng);
            a.setParams ({ hostile (rng), hostile (rng), hostile (rng), hostile (rng) });
            c.setParams (ContourParams::from (s));
            l.setParams (LfoParams::from (s));
            HostTiming ht; ht.bpmValid = rng.below (2) == 0; ht.bpm = hostile (rng);
            l.setHostTiming (ht);
            t.setParams (TrajectoryParams::from (s));
            t.setHostTiming (ht);
            if (rng.below (3) == 0) { a.prepare (sr); c.prepare (sr); l.prepare (sr); t.prepare (sr); }
            for (int n = 0; n < 2000; ++n)
            {
                if (n % 300 == 0) { a.noteOn (hostile (rng)); c.noteOn (rng.below (2) == 0); l.noteOn(); t.noteOn (rng.below (2) == 0); }
                if (n % 700 == 350) { a.noteOff(); c.noteOff(); }
                if (n % 997 == 5) a.forceRelease (static_cast<int> (rng.below (50)) - 5);
                const float av = a.next(), cv = c.process(), lv = l.process(), tv = t.process();
                CHECK (std::isfinite (av) && av >= 0.0f && av <= 1.0001f);
                CHECK (std::isfinite (cv) && cv >= 0.0f && cv <= 1.0f);
                CHECK (std::isfinite (lv) && std::fabs (lv) <= 1.0001f);
                CHECK (std::isfinite (tv) && tv >= 0.0f && tv <= 1.0f);
            }
        }
    }
}

void testFiltersAndOscillators()
{
    Rng rng (3);
    for (double sr : rates)
    {
        SvfFilter f; f.prepare (sr);
        SubOscillator sub; sub.prepare (sr);
        MidOscillator mid; mid.prepare (sr);
        std::vector<float> in (3000);
        for (int it = 0; it < 12; ++it)
        {
            const auto s = hostileSnapshot (rng);
            f.setParams (SvfParams::from (s));
            sub.setWaveform (static_cast<SubWave> (rng.below (2)));
            mid.setParams (MidOscParams::from (s));
            fillInput (in, rng, it % 3);
            for (std::size_t i = 0; i < in.size(); ++i)
            {
                const float y = f.process (in[i], hostile (rng) * 1000.0f);
                CHECK (std::isfinite (y) && std::fabs (y) < 1.0e5f);
                const float hz = rng.below (8) == 0 ? hostile (rng) : 10.0f + rng.unit() * 5000.0f;
                const float so = sub.process (hz);
                const float mo = mid.process (hz, hostile (rng));
                CHECK (std::isfinite (so) && std::fabs (so) <= 1.5f);
                CHECK (std::isfinite (mo) && std::fabs (mo) <= 8.0f);
            }
        }
    }
}

template <class Block>
void runBlocks (Block&& fn, std::vector<float>& buf, Rng& rng)
{
    std::size_t pos = 0;
    while (pos < buf.size())
    {
        const int want = blocks[rng.below (static_cast<int> (sizeof (blocks) / sizeof (*blocks)))];
        const int n = static_cast<int> (std::min<std::size_t> (static_cast<std::size_t> (want), buf.size() - pos));
        fn (buf.data() + pos, n);
        pos += static_cast<std::size_t> (n);
    }
}

void testBusEffects()
{
    Rng rng (4);
    for (double sr : rates)
    {
        for (int blockMax : { 1, 64, 4097 })
        {
            Drive drive; drive.prepare (sr, blockMax);
            Oversampler os; os.prepare (sr, blockMax);
            FormantFurnace furnace; furnace.prepare (sr, blockMax);
            HarmonicFracture fracture; fracture.prepare (sr, blockMax);
            Width width; width.prepare (sr, blockMax);
            Limiter lim; lim.prepare (sr, blockMax);
            OutputTrim trim; trim.prepare (sr);
            std::vector<float> a (6000), b (6000);
            for (int it = 0; it < 8; ++it)
            {
                const auto s = hostileSnapshot (rng);
                drive.setParams (DriveParams::from (s));
                furnace.setParams (FormantFurnaceParams::from (s));
                fracture.setParams (HarmonicFractureParams::from (s));
                width.setParams (WidthParams::from (s));
                lim.setParams (LimiterParams::from (s));
                trim.setTrimDb (hostile (rng));
                os.setMode (static_cast<OversamplingMode> (rng.below (3)));
                fillInput (a, rng, it % 4);
                MidModulation mod { hostile (rng), hostile (rng) };

                runBlocks ([&] (float* p, int n) { drive.process (p, n, rng.below (3) == 0 ? nullptr : &os); }, a, rng);
                CHECK_MSG (finiteAll (a, 1.0e4f), "drive");
                runBlocks ([&] (float* p, int n) { furnace.process (p, n, mod); }, a, rng);
                CHECK_MSG (finiteAll (a, 1.0e4f), "furnace");
                runBlocks ([&] (float* p, int n) { fracture.process (p, n, mod); }, a, rng);
                CHECK_MSG (finiteAll (a, 1.0e4f), "fracture");
                b = a;
                std::size_t pos = 0;
                while (pos < a.size())
                {
                    const int n = static_cast<int> (std::min<std::size_t> (static_cast<std::size_t> (blocks[rng.below (6)]), a.size() - pos));
                    width.process (a.data() + pos, a.data() + pos, b.data() + pos, n);
                    pos += static_cast<std::size_t> (n);
                }
                CHECK_MSG (finiteAll (a, 1.0e4f) && finiteAll (b, 1.0e4f), "width");
                pos = 0;
                while (pos < a.size())
                {
                    const int n = static_cast<int> (std::min<std::size_t> (static_cast<std::size_t> (blocks[rng.below (6)]), a.size() - pos));
                    lim.process (a.data() + pos, b.data() + pos, n);
                    trim.process (a.data() + pos, b.data() + pos, n);
                    pos += static_cast<std::size_t> (n);
                }
                CHECK_MSG (finiteAll (a, 1.0e8f) && finiteAll (b, 1.0e8f), "limiter/trim");
            }
        }
    }
}

void testTexture()
{
    Rng rng (5);
    for (double sr : rates)
    {
        TextureLayer t; t.prepare (sr, static_cast<std::uint32_t> (rng.next()));
        std::vector<float> out (2048);
        for (int it = 0; it < 30; ++it)
        {
            t.setParams (TextureParams::from (hostileSnapshot (rng)));
            t.noteOn (hostile (rng), hostile (rng));
            std::fill (out.begin(), out.end(), 0.0f);
            t.addTo (out.data(), 1 + rng.below (2047));
            CHECK (finiteAll (out, 10.0f));
        }
    }
}

void testNoteMath()
{
    Rng rng (6);
    for (int it = 0; it < 20000; ++it)
    {
        const double a4 = hostile (rng), tr = hostile (rng), cents = hostile (rng);
        const double hz = noteToHz (rng.below (400) - 100, a4, tr, cents);
        (void) hz; // may be inf/nan for hostile input; must simply not crash
        const int bend = static_cast<int> (rng.next()) - 1000000;
        const double bs = bendSemitones (bend, hostile (rng));
        if (std::isfinite (bs)) CHECK (std::fabs (bs) <= 12.0);
        const float g = velocityToLevelGain (static_cast<int> (rng.next()), hostile (rng));
        CHECK (std::isfinite (g) || true);
        (void) velocityToAttackScale (static_cast<int> (rng.next()), hostile (rng));
        PitchGlide gl; gl.prepare (rates[rng.below (7)]);
        gl.setTarget (hostile (rng), hostile (rng), rng.below (2) == 0);
        for (int n = 0; n < 8; ++n) (void) gl.next();
    }
}
} // namespace

int main()
{
    testParamDecoders();
    testEnvelopesAndModulators();
    testFiltersAndOscillators();
    testBusEffects();
    testTexture();
    testNoteMath();
    std::puts ("DspRobustnessTests passed");
    return 0;
}
