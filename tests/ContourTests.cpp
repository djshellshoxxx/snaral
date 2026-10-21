// Contour + MotionRouting boundary tests (spec: motion-filter.md contour contract + acceptance 1, 2, 7).
#include "SynthTestUtil.h"

#include "Dsp/Contour.h"
#include "Dsp/Oscillators.h"

#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <vector>

using namespace snaral;

namespace
{
// Independent reference shapes (explicit formulas, k = 4).
double refShape (int shape, double x)
{
    const double k = 4.0;
    if (shape == 0) return x;
    if (shape == 1) return (std::exp (k * x) - 1.0) / (std::exp (k) - 1.0);
    return 1.0 - (std::exp (k * (1.0 - x)) - 1.0) / (std::exp (k) - 1.0);
}

ContourParams makeParams (int shape, float a, float d, float s, float r)
{
    ContourParams p;
    p.attackMs = a; p.decayMs = d; p.sustain01 = s; p.releaseMs = r;
    p.shape = static_cast<ContourShape> (shape);
    p.depth01 = 1.0f;
    return p;
}

void testShapes()
{
    for (int s = 0; s < 3; ++s)
    {
        CHECK_NEAR (contourShapeFn (static_cast<ContourShape> (s), 0.0f), 0.0, 1e-6);
        CHECK_NEAR (contourShapeFn (static_cast<ContourShape> (s), 1.0f), 1.0, 1e-6);
        float prev = -1.0f;
        for (float x = 0.0f; x <= 1.0f; x += 0.01f)
        {
            const float v = contourShapeFn (static_cast<ContourShape> (s), x);
            CHECK (v >= prev - 1e-7f);
            CHECK_NEAR (v, refShape (s, x), 1e-5);
            prev = v;
        }
        CHECK (contourShapeFn (static_cast<ContourShape> (s), -3.0f) == 0.0f);
        CHECK (contourShapeFn (static_cast<ContourShape> (s), 9.0f) == 1.0f);
    }
    CHECK (contourShapeFn (ContourShape::EaseIn, 0.5f) < 0.5f);
    CHECK (contourShapeFn (ContourShape::EaseOut, 0.5f) > 0.5f);
    CHECK_NEAR (contourShapeFn (ContourShape::EaseOut, 0.3f), 1.0 - contourShapeFn (ContourShape::EaseIn, 0.7f), 1e-6);
    std::cout << "shapes OK\n";
}

void testTrajectoryMatchesClosedForm()
{
    for (const double sr : { 44100.0, 48000.0, 96000.0 })
        for (int shape = 0; shape < 3; ++shape)
        {
            const float aMs = 12.0f, dMs = 140.0f, sus = 0.35f, rMs = 90.0f;
            Contour c;
            c.prepare (sr);
            c.setParams (makeParams (shape, aMs, dMs, sus, rMs));
            c.reset();
            const int aN = static_cast<int> (std::lround (aMs * 0.001 * sr));
            const int dN = static_cast<int> (std::lround (dMs * 0.001 * sr));
            const int rN = static_cast<int> (std::lround (rMs * 0.001 * sr));

            c.noteOn();
            double maxErr = 0.0;
            int peakIndex = -1;
            const int holdEnd = aN + dN + 3000;
            for (int i = 0; i < holdEnd; ++i)
            {
                double ref;
                if (i < aN) ref = refShape (shape, static_cast<double> (i) / aN);
                else if (i < aN + dN) ref = 1.0 + (sus - 1.0) * refShape (shape, static_cast<double> (i - aN) / dN);
                else ref = sus;
                const float v = c.process();
                maxErr = std::max (maxErr, std::fabs (v - ref));
                if (peakIndex < 0 && v >= 0.999999f) peakIndex = i;
            }
            CHECK (maxErr < 2.0e-5);       // exact trajectory, well inside one sample
            CHECK (peakIndex == aN);        // peak reached exactly at the end of the attack
            CHECK (c.stage() == Contour::Stage::Sustain);

            // release from sustain
            c.noteOff();
            maxErr = 0.0;
            for (int j = 0; j < rN + 10; ++j)
            {
                const double ref = j < rN ? sus * (1.0 - refShape (shape, static_cast<double> (j) / rN)) : 0.0;
                const float v = c.process();
                maxErr = std::max (maxErr, std::fabs (v - ref));
                if (j == rN - 1) CHECK (c.stage() == Contour::Stage::Idle); // returned to baseline after exactly rN samples
            }
            CHECK (maxErr < 2.0e-5);
            CHECK (! c.isActive() && c.value() == 0.0f);
        }
    std::cout << "A/D/S/R trajectory equals the closed form (3 shapes x 3 rates), peak and return timing exact\n";
}

void testNoteOffDuringAttackAndDecay()
{
    const double sr = 48000.0;
    Contour c;
    c.prepare (sr);
    c.setParams (makeParams (1, 100.0f, 200.0f, 0.2f, 60.0f));
    c.reset();
    c.noteOn();
    float v = 0.0f;
    for (int i = 0; i < 2400; ++i) v = c.process(); // 50 ms into a 100 ms attack
    const float vr = v;
    CHECK (vr > 0.0f && vr < 1.0f);
    c.noteOff();
    CHECK (c.stage() == Contour::Stage::Release);
    const int rN = static_cast<int> (std::lround (0.060 * sr));
    float first = c.process();
    CHECK_NEAR (first, vr, 1e-6); // release starts from the current value, no jump
    float prev = first;
    for (int j = 1; j < rN; ++j)
    {
        v = c.process();
        CHECK (v <= prev + 1e-7f); // monotone return
        prev = v;
    }
    CHECK (c.process() == 0.0f && ! c.isActive());

    // note-off in decay
    c.reset();
    c.noteOn();
    for (int i = 0; i < 4800 + 2000; ++i) v = c.process();
    CHECK (c.stage() == Contour::Stage::Attack || c.stage() == Contour::Stage::Decay);
    c.noteOff();
    CHECK (c.process() >= 0.0f);
    std::cout << "note-off during attack/decay releases from the current value OK\n";
}

void testRetriggerAndLegato()
{
    const double sr = 48000.0;
    Contour c;
    c.prepare (sr);
    c.setParams (makeParams (0, 5.0f, 100.0f, 0.4f, 300.0f));
    c.reset();
    c.noteOn();
    for (int i = 0; i < 6000; ++i) c.process();
    CHECK (c.stage() == Contour::Stage::Sustain);
    const float sus = c.value();

    // legato hook: retrigger=false while held keeps the contour untouched
    c.noteOn (false);
    CHECK (c.stage() == Contour::Stage::Sustain);
    CHECK (c.process() == sus);

    // retrigger from sustain: attack starts from the current value, no downward jump
    c.noteOn (true);
    CHECK (c.stage() == Contour::Stage::Attack);
    CHECK_NEAR (c.process(), sus, 1e-6);

    // retrigger during release starts at the current release value
    c.reset();
    c.noteOn();
    for (int i = 0; i < 6000; ++i) c.process();
    c.noteOff();
    float v = 0.0f;
    for (int i = 0; i < 3000; ++i) v = c.process();
    CHECK (v > 0.0f && c.stage() == Contour::Stage::Release);
    c.noteOn (false); // legato but not held (release) -> starts a new attack
    CHECK (c.stage() == Contour::Stage::Attack);
    CHECK_NEAR (c.process(), v, 1e-3);

    // idle legato note-on starts normally
    c.reset();
    c.noteOn (false);
    CHECK (c.stage() == Contour::Stage::Attack);

    // zero attack: peak on the first sample
    c.setParams (makeParams (0, 0.0f, 100.0f, 0.5f, 100.0f));
    c.reset();
    c.noteOn();
    CHECK (c.process() == 1.0f);

    // no-op calls on idle
    c.reset();
    c.noteOff();
    CHECK (c.stage() == Contour::Stage::Idle && c.process() == 0.0f);
    std::cout << "retrigger / legato hooks OK\n";
}

void testSustainEditIsSmooth()
{
    Contour c;
    c.prepare (48000.0);
    c.setParams (makeParams (0, 5.0f, 50.0f, 0.2f, 100.0f));
    c.reset();
    c.noteOn();
    for (int i = 0; i < 20000; ++i) c.process();
    c.setParams (makeParams (0, 5.0f, 50.0f, 0.9f, 100.0f));
    float prev = c.value();
    double maxStep = 0.0;
    for (int i = 0; i < 4800; ++i)
    {
        const float v = c.process();
        maxStep = std::max (maxStep, static_cast<double> (std::fabs (v - prev)));
        prev = v;
    }
    CHECK (maxStep < 0.01);
    CHECK_NEAR (prev, 0.9, 1e-3);
    std::cout << "live sustain edit smoothed (max step " << maxStep << ")\n";
}

void testMotionRouting()
{
    ParamSnapshot s;
    s[P::filter_cutoff_hz] = 1000.0f;
    s[P::mid_table_position] = 50.0f;
    s[P::contour_depth] = 50.0f;
    s[P::lfo_depth] = 100.0f;
    s[P::bite] = 0.0f;

    // Motion = 0: no contour / LFO movement for any input (static-input fixture).
    s[P::motion] = 0.0f;
    s[P::bite] = 100.0f; // Bite peak offset rides on the contour and is scaled by Motion too
    {
        const auto r = MotionRouting::from (s);
        for (float c = 0.0f; c <= 1.0f; c += 0.125f)
            for (float l = -1.0f; l <= 1.0f; l += 0.25f)
            {
                const auto o = r.evaluate (c, l, 0.0f);
                CHECK (o.cutoffHz == 1000.0f);
                CHECK (o.position == 0.5f);
            }
    }

    // Motion = 1: contour peak = depth * 60 st (+ Bite), LFO = +-24 st at 100 %.
    s[P::motion] = 100.0f;
    s[P::bite] = 0.0f;
    {
        const auto r = MotionRouting::from (s);
        CHECK_NEAR (r.evaluate (1.0f, 0.0f, 0.0f).cutoffHz, 1000.0 * std::exp2 (30.0 / 12.0), 0.01);
        CHECK_NEAR (r.evaluate (0.0f, 1.0f, 0.0f).cutoffHz, 1000.0 * std::exp2 (24.0 / 12.0), 0.01);
        CHECK_NEAR (r.evaluate (0.0f, -1.0f, 0.0f).cutoffHz, 1000.0 * std::exp2 (-24.0 / 12.0), 0.01);
        CHECK (r.evaluate (1.0f, 1.0f, 0.0f).cutoffHz == 20000.0f); // 54 st above 1 kHz would exceed 20 kHz: clamped
        CHECK_NEAR (r.evaluate (1.0f, -1.0f, 0.0f).cutoffHz, 1000.0 * std::exp2 (6.0 / 12.0), 0.01); // adds in the semitone domain
        CHECK_NEAR (r.evaluate (1.0f, 0.0f, 0.0f).position, 0.5 + 0.5 * 0.25, 1e-6);
        CHECK_NEAR (r.evaluate (0.0f, -1.0f, 0.0f).position, 0.5 - 0.25, 1e-6);
    }
    // Bite adds exactly 18 st at 100 % (peak offset), scaled by Motion.
    s[P::bite] = 100.0f;
    {
        const auto r = MotionRouting::from (s);
        CHECK_NEAR (r.evaluate (1.0f, 0.0f, 0.0f).cutoffHz, 1000.0 * std::exp2 ((30.0 + 18.0) / 12.0), 0.05);
        CHECK_NEAR (r.evaluate (0.0f, 0.0f, 0.0f).cutoffHz, 1000.0, 1e-3); // peak offset only at the contour peak
    }
    s[P::motion] = 50.0f;
    {
        const auto r = MotionRouting::from (s);
        CHECK_NEAR (r.evaluate (1.0f, 0.0f, 0.0f).cutoffHz, 1000.0 * std::exp2 (0.5 * 48.0 / 12.0), 0.05);
        CHECK_NEAR (r.evaluate (0.0f, 1.0f, 0.0f).cutoffHz, 1000.0 * std::exp2 (0.5 * 24.0 / 12.0), 0.05);
    }

    // Equal semitone steps give equal frequency ratios (log domain) and the clamp holds at both ends.
    CHECK_NEAR (modulatedCutoffHz (440.0f, 12.0f), 880.0, 1e-3);
    CHECK_NEAR (modulatedCutoffHz (440.0f, -12.0f), 220.0, 1e-3);
    CHECK (modulatedCutoffHz (20000.0f, 60.0f) == 20000.0f);
    CHECK (modulatedCutoffHz (20.0f, -60.0f) == 20.0f);
    CHECK (modulatedCutoffHz (1.0f, 0.0f) == 20.0f);
    CHECK (modulatedCutoffHz (1.0e9f, 0.0f) == 20000.0f);
    CHECK (modulatedCutoffHz (std::nanf (""), 0.0f) > 20.0f);
    CHECK (modulatedCutoffHz (1000.0f, std::nanf ("")) == 1000.0f);
    CHECK (modulatedCutoffHz (1000.0f, std::numeric_limits<float>::infinity()) <= 20000.0f);
    CHECK (modulatedCutoffHz (1000.0f, -std::numeric_limits<float>::infinity()) >= 20.0f);

    // Position bounded 0..1 whatever the inputs.
    CHECK (modulatedPosition (0.9f, 5.0f) == 1.0f);
    CHECK (modulatedPosition (0.1f, -5.0f) == 0.0f);
    CHECK (modulatedPosition (std::nanf (""), 0.3f) == 0.3f);
    for (float motion = 0.0f; motion <= 100.0f; motion += 25.0f)
        for (float base = 0.0f; base <= 100.0f; base += 25.0f)
        {
            s[P::motion] = motion;
            s[P::mid_table_position] = base;
            s[P::contour_depth] = 100.0f;
            s[P::lfo_depth] = 100.0f;
            const auto r = MotionRouting::from (s);
            for (const float c : { 0.0f, 1.0f, 5.0f, -5.0f, std::nanf ("") })
                for (const float l : { -9.0f, -1.0f, 1.0f, 9.0f })
                {
                    const auto o = r.evaluate (c, l, 1.0f);
                    CHECK (o.position >= 0.0f && o.position <= 1.0f);
                    CHECK (o.cutoffHz >= 20.0f && o.cutoffHz <= 20000.0f);
                }
        }

    CHECK_NEAR (motionScaledDepth (0.5f, 0.25f), 0.125, 1e-7);
    CHECK (motionScaledDepth (0.5f, 0.0f) == 0.0f);
    CHECK_NEAR (motionScaledDepth (2.0f, 1.0f), 1.0, 1e-7);
    std::cout << "MotionRouting: Motion=0 static, formulas, log-domain clamp, position bound OK\n";
}

void testTrajectoryReplacement()
{
    ParamSnapshot s;
    s[P::motion] = 100.0f;
    s[P::contour_depth] = 50.0f;
    s[P::lfo_depth] = 0.0f;
    s[P::bite] = 0.0f;
    s[P::traj_enabled] = 1.0f;
    s[P::traj_depth] = 50.0f;

    s[P::traj_dest] = 0.0f; // cutoff
    auto r = MotionRouting::from (s);
    CHECK (r.contourSt == 0.0f && r.trajSt == 30.0f);
    CHECK (r.contourPos > 0.0f && r.trajPos == 0.0f);

    s[P::traj_dest] = 1.0f; // source_position
    r = MotionRouting::from (s);
    CHECK (r.contourSt == 30.0f && r.trajSt == 0.0f);
    CHECK (r.contourPos == 0.0f && r.trajPos > 0.0f);

    s[P::traj_dest] = 2.0f; // both
    r = MotionRouting::from (s);
    CHECK (r.contourSt == 0.0f && r.trajSt == 30.0f && r.contourPos == 0.0f && r.trajPos == 0.25f);

    // The trajectory is not scaled by Motion.
    s[P::motion] = 0.0f;
    r = MotionRouting::from (s);
    CHECK (r.trajSt == 30.0f);
    CHECK_NEAR (r.evaluate (1.0f, 0.0f, 1.0f).cutoffHz, 1200.0 * std::exp2 (30.0 / 12.0), 0.05);

    s[P::traj_enabled] = 0.0f;
    r = MotionRouting::from (s);
    CHECK (r.trajSt == 0.0f && r.trajPos == 0.0f);
    std::cout << "trajectory replaces the contour on its destinations OK\n";
}

void testDeterminismSubIsolationParams()
{
    auto run = [] (Contour& c)
    {
        std::vector<float> out;
        c.setParams (makeParams (2, 7.0f, 90.0f, 0.45f, 120.0f));
        c.reset();
        c.noteOn();
        for (int i = 0; i < 12000; ++i)
        {
            if (i == 5000) c.noteOff();
            if (i == 7000) c.noteOn();
            if (i == 9000) c.noteOff();
            out.push_back (c.process());
        }
        return out;
    };
    Contour a, b;
    a.prepare (48000.0);
    b.prepare (48000.0);
    const auto r1 = run (a), r2 = run (a), r3 = run (b);
    CHECK (std::memcmp (r1.data(), r2.data(), r1.size() * sizeof (float)) == 0);
    CHECK (std::memcmp (r1.data(), r3.data(), r1.size() * sizeof (float)) == 0);

    // Sub path takes only a frequency: exercising every modulation block cannot change it (acceptance 7).
    auto sub = [&] (bool disturb)
    {
        SubOscillator o;
        o.prepare (48000.0);
        o.setWaveform (SubWave::Sine);
        o.reset (0.0);
        Contour c;
        c.prepare (48000.0);
        std::vector<float> out;
        for (int i = 0; i < 8000; ++i)
        {
            if (disturb)
            {
                if (i % 1000 == 0) { c.setParams (makeParams (i / 1000 % 3, 0.0f, 20.0f + static_cast<float> (i % 7), 0.5f, 30.0f)); c.noteOn(); }
                c.process();
            }
            out.push_back (o.process (41.2f));
        }
        return out;
    };
    CHECK (sub (false) == sub (true));

    ParamSnapshot s;
    s[P::contour_attack_ms] = 40.0f;
    s[P::contour_decay_ms] = 250.0f;
    s[P::contour_sustain] = 60.0f;
    s[P::contour_release_ms] = 777.0f;
    s[P::contour_shape] = 2.0f;
    s[P::contour_depth] = 80.0f;
    const auto p = ContourParams::from (s);
    CHECK (p.attackMs == 40.0f && p.decayMs == 250.0f && p.releaseMs == 777.0f && p.shape == ContourShape::EaseOut);
    CHECK_NEAR (p.sustain01, 0.6, 1e-6);
    CHECK_NEAR (p.depth01, 0.8, 1e-6);

    // parameter extremes stay finite and bounded
    Contour c;
    c.prepare (192000.0);
    for (const float att : { 0.0f, 500.0f })
        for (const float d : { 5.0f, 3000.0f })
        {
            ContourParams q = makeParams (1, att, d, 1.0f, 5.0f);
            q.sustain01 = std::nanf ("");
            c.setParams (q);
            c.reset();
            c.noteOn();
            for (int i = 0; i < 50000; ++i)
            {
                const float v = c.process();
                CHECK (std::isfinite (v) && v >= 0.0f && v <= 1.0f);
            }
            c.noteOff();
            for (int i = 0; i < 2000; ++i) CHECK (std::isfinite (c.process()));
        }
    std::cout << "determinism, sub isolation, params, extremes OK\n";
}

void testNoAllocation()
{
    Contour c;
    c.prepare (48000.0);
    MotionRouting r = MotionRouting::from (ParamSnapshot {});
    float sink = 0.0f;
    synthtest::NoAllocScope scope;
    c.noteOn();
    for (int i = 0; i < 20000; ++i)
    {
        if (i == 10000) c.noteOff();
        const float v = c.process();
        sink += r.evaluate (v, 0.3f, 0.2f).cutoffHz;
    }
    CHECK (scope.count() == 0);
    CHECK (std::isfinite (sink));
}
} // namespace

int main()
{
    testShapes();
    testTrajectoryMatchesClosedForm();
    testNoteOffDuringAttackAndDecay();
    testRetriggerAndLegato();
    testSustainEditIsSmooth();
    testMotionRouting();
    testTrajectoryReplacement();
    testDeterminismSubIsolationParams();
    testNoAllocation();
    std::cout << "ContourTests passed\n";
    return 0;
}
