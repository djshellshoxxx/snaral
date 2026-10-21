// Sub Anchor (docs/specs/expansion-sub-anchor.md): closed-form table, bound/monotonic sweep, glide click render, disabled
// identity, mid untouched, registry check. JUCE-free.
#include "EngineOutputTestUtil.h"
#include "EngineRig.h"
#include "Engine/SubAnchor.h"

#include <cstdio>
#include <functional>
#include <limits>

using namespace snaral;
using namespace rig;

static void testClosedForm()
{
    // reference values at s = 1 (tolerance 0.001 dB), halved at s = 0.5, odd symmetry
    const struct { double d, db; } ref[] = { { 12, -9.1391 }, { 24, -11.5683 }, { 36, -11.9407 } };
    for (const auto& r : ref)
        for (const int a : { 24, 36, 60 })
        {
            CHECK (std::fabs (subAnchorGainDb (a + r.d, a, 1.0) - r.db) < 1.0e-3);
            CHECK (std::fabs (subAnchorGainDb (a - r.d, a, 1.0) + r.db) < 1.0e-3);
            CHECK (std::fabs (subAnchorGainDb (a + r.d, a, 0.5) - 0.5 * r.db) < 1.0e-3);
        }
    // exactly 1.0f at the anchor, for non-finite input and for strength 0
    for (int a = 24; a <= 60; ++a)
    {
        CHECK (subAnchorGain (static_cast<double> (a), a, 1.0) == 1.0f);
        CHECK (subAnchorGain (static_cast<double> (a) + 5.0, a, 0.0) == 1.0f);
    }
    CHECK (subAnchorGain (std::nan (""), 36, 1.0) == 1.0f);
    CHECK (subAnchorGain (std::numeric_limits<double>::infinity(), 36, 0.5) == 1.0f);

    // strictly decreasing in p (s > 0) and bounded |gainDb| < 12 s, across anchors, pitches 0..127 and strengths
    for (int a = 24; a <= 60; ++a)
        for (const double s : { 0.25, 0.5, 1.0 })
        {
            double prev = 1.0e9;
            for (int p = 0; p <= 127; ++p)
            {
                const double db = subAnchorGainDb (p, a, s);
                CHECK (db < prev);
                CHECK (std::fabs (db) < 12.0 * s);
                prev = db;
            }
        }
    // continuous pitch too
    for (double p = 0.0; p < 127.0; p += 0.37)
        CHECK (subAnchorGainDb (p, 36, 1.0) > subAnchorGainDb (p + 0.01, 36, 1.0));
    std::printf ("sub anchor closed form ok\n");
}

static void testRegistry()
{
    const char* ids[] = { "anchor_enabled", "anchor_note", "anchor_strength" };
    for (const char* id : ids)
        CHECK (findParameterIndex (id) >= 0);
    const auto& note = paramDef (P::anchor_note);
    CHECK (note.minimum == 24.0f && note.maximum == 60.0f && note.defaultValue == 36.0f && note.isInteger());
    const auto& st = paramDef (P::anchor_strength);
    CHECK (st.minimum == 0.0f && st.maximum == 100.0f && st.defaultValue == 50.0f);
    CHECK (! paramDef (P::anchor_enabled).defaultValue);
    std::printf ("sub anchor registry ok\n");
}

static std::vector<float> render (const std::function<void (Rig&)>& mod, int note, int n = 36000, int layerSolo = 1)
{
    Rig r;
    r.set (P::layer_solo, static_cast<float> (layerSolo));
    r.set (P::limiter_enabled, 0);
    r.set (P::sub_amp_attack_ms, 0);
    mod (r);
    r.noteOn (note);
    r.run (n);
    return r.L;
}

static void testEngine()
{
    // Disabled == strength 0 == baseline, bit for bit; at the anchor note enabled == baseline too.
    const auto base = render ([] (Rig&) {}, 48);
    CHECK (identical (render ([] (Rig& r) { r.set (P::anchor_enabled, 1); r.set (P::anchor_strength, 0); r.set (P::anchor_note, 36); }, 48), base));
    CHECK (identical (render ([] (Rig& r) { r.set (P::anchor_enabled, 0); r.set (P::anchor_strength, 100); }, 48), base));
    CHECK (identical (render ([] (Rig& r) { r.set (P::anchor_enabled, 1); r.set (P::anchor_strength, 100); r.set (P::anchor_note, 48); }, 48), base));

    // Octave above the anchor: -9.1391 dB at s = 1, -4.5696 dB at s = 0.5 (steady state, after the 10 ms smoother settled)
    for (const float strength : { 100.0f, 50.0f })
    {
        const auto on = render ([strength] (Rig& r) { r.set (P::anchor_enabled, 1); r.set (P::anchor_strength, strength); r.set (P::anchor_note, 36); }, 48);
        const double dB = 20.0 * std::log10 (rmsOf (on, 12000, 36000) / rmsOf (base, 12000, 36000));
        const double expect = subAnchorGainDb (48.0, 36, strength / 100.0);
        CHECK (std::fabs (dB - expect) < 0.01);
    }
    // Below the anchor it boosts (+9.14 dB two octaves... one octave below)
    {
        const auto base24 = render ([] (Rig&) { }, 24);
        const auto on = render ([] (Rig& r) { r.set (P::anchor_enabled, 1); r.set (P::anchor_strength, 100); r.set (P::anchor_note, 36); }, 24);
        const double dB = 20.0 * std::log10 (rmsOf (on, 12000, 36000) / rmsOf (base24, 12000, 36000));
        CHECK (std::fabs (dB - 9.1391) < 0.01);
    }
    // Mid layer bit-identical with the anchor on and off; the anchor only touches the sub.
    {
        const auto midOff = render ([] (Rig& r) { r.set (P::anchor_enabled, 0); }, 60, 24000, 2);
        const auto midOn = render ([] (Rig& r) { r.set (P::anchor_enabled, 1); r.set (P::anchor_strength, 100); r.set (P::anchor_note, 24); }, 60, 24000, 2);
        CHECK (peak (midOff) > 0.05);
        CHECK (identical (midOff, midOn));
    }
    // Glide across 36 semitones with the anchor on: finite, no click (largest sample step vs the same glide without anchor
    // stays small relative to the steady level) and sub L/R identical.
    {
        auto glideRun = [] (bool anchor)
        {
            Rig r;
            r.soloSub();
            r.set (P::limiter_enabled, 0);
            r.set (P::legato, 1); r.set (P::glide_ms, 500);
            r.set (P::anchor_enabled, anchor ? 1.0f : 0.0f); r.set (P::anchor_strength, 100); r.set (P::anchor_note, 36);
            r.noteOn (24);
            r.run (12000);
            r.noteOn (60);        // +36 semitones, 500 ms glide
            r.run (36000);
            return std::pair { r.L, r.R };
        };
        const auto [l, r] = glideRun (true);
        const auto [l0, r0] = glideRun (false);
        CHECK (finite (l) && identical (l, r));
        double worst = 0.0;
        for (std::size_t i = 13000; i < l.size(); ++i) worst = std::max (worst, std::fabs (static_cast<double> (l[i]) - l[i - 1]));
        double worst0 = 0.0;
        for (std::size_t i = 13000; i < l0.size(); ++i) worst0 = std::max (worst0, std::fabs (static_cast<double> (l0[i]) - l0[i - 1]));
        const double steady = peak (l, 6000, 12000);
        std::printf ("  glide: max step %.5f (no anchor %.5f), steady level %.3f\n", worst, worst0, steady);
        CHECK (worst <= worst0 * 1.01 + 0.001 * steady);    // the anchor adds no step (-60 dBFS relative to the steady level)
        CHECK (peak (l, 14000) > 0.0);
    }
    std::printf ("sub anchor engine ok\n");
}

int main()
{
    testClosedForm();
    testRegistry();
    testEngine();
    std::printf ("SubAnchorTests: all passed\n");
    return 0;
}
