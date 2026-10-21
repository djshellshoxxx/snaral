// Snarl Trajectory boundary tests (spec: expansion-snarl-trajectory.md).
#include "SynthTestUtil.h"

#include "Dsp/Trajectory.h"

#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <vector>

using namespace snaral;

namespace
{
using Pts = std::array<float, trajNumPoints>;

struct Lcg
{
    std::uint32_t s = 987654321u;
    float next01() { s = s * 1664525u + 1013904223u; return static_cast<float> (s >> 8) / 16777216.0f; }
};

HostTiming timing (double bpm, bool valid = true)
{
    HostTiming t;
    t.bpm = bpm;
    t.bpmValid = valid;
    return t;
}

void checkLayout (const Pts& x, const Pts& y)
{
    CHECK (x[0] == 0.0f && x[4] == 1.0f);
    for (int i = 0; i < 4; ++i)
        CHECK (x[static_cast<std::size_t> (i + 1)] - x[static_cast<std::size_t> (i)] >= trajMinGap - 1.0e-6f);
    for (const float v : y)
        CHECK (std::isfinite (v) && v >= 0.0f && v <= 1.0f);
}

void testMonotonicXSafety()
{
    // Dragging a point cannot cross a neighbour or create a zero/negative segment.
    Pts x { 0.0f, 0.15f, 0.40f, 0.70f, 1.0f }, y { 0.2f, 0.9f, 0.5f, 0.3f, 0.2f };
    x[2] = 0.99f; // drag point 3 far to the right
    TrajectoryCurve::sanitizePoints (x, y);
    CHECK_NEAR (x[2], 1.0f - 2.0f * trajMinGap, 1e-6); // pushed against its right neighbours' minimum gaps
    checkLayout (x, y);
    x = { 0.0f, 0.15f, 0.40f, 0.70f, 1.0f };
    x[2] = 0.0f; // drag point 3 to the far left
    TrajectoryCurve::sanitizePoints (x, y);
    CHECK_NEAR (x[2], x[1] + trajMinGap, 1e-6);
    checkLayout (x, y);

    // Exhaustive grid of extreme x2..x4, then random fuzz including non-finite values.
    const float vals[] = { -5.0f, 0.0f, 0.01f, 0.5f, 0.97f, 1.0f, 7.0f, std::nanf (""), std::numeric_limits<float>::infinity() };
    for (const float a : vals)
        for (const float b : vals)
            for (const float c : vals)
            {
                Pts xs { 0.0f, a, b, c, 1.0f }, ys { a, b, c, b, a };
                TrajectoryCurve::sanitizePoints (xs, ys);
                checkLayout (xs, ys);
                Pts xs2 = xs, ys2 = ys; // idempotent
                TrajectoryCurve::sanitizePoints (xs2, ys2);
                CHECK (xs2 == xs && ys2 == ys);
            }
    Lcg r;
    for (int i = 0; i < 20000; ++i)
    {
        Pts xs { r.next01() * 3.0f - 1.0f, r.next01() * 3.0f - 1.0f, r.next01() * 3.0f - 1.0f, r.next01() * 3.0f - 1.0f, r.next01() * 3.0f - 1.0f };
        Pts ys { r.next01() * 3.0f - 1.0f, r.next01(), r.next01(), r.next01(), r.next01() * 2.0f };
        TrajectoryCurve::sanitizePoints (xs, ys);
        checkLayout (xs, ys);
    }
    // In-range points that already satisfy the layout are untouched.
    Pts gx { 0.0f, 0.2f, 0.45f, 0.8f, 1.0f }, gy { 0.1f, 0.2f, 0.3f, 0.4f, 0.5f };
    const Pts gx0 = gx, gy0 = gy;
    TrajectoryCurve::sanitizePoints (gx, gy);
    CHECK (gx == gx0 && gy == gy0);
    std::cout << "monotonic-x safety (grid + 20000 fuzz) OK\n";
}

void testCurveEndpointsAndBounds()
{
    Lcg r;
    for (const float tension : { 0.0f, 0.25f, 0.5f, 1.0f })
    {
        for (int trial = 0; trial < 400; ++trial)
        {
            Pts x { 0.0f, r.next01(), r.next01(), r.next01(), 1.0f }, y { r.next01(), r.next01(), r.next01(), r.next01(), r.next01() };
            if (trial % 5 == 0) y = { 0.0f, 1.0f, 0.0f, 1.0f, 0.0f };      // extreme zig-zag
            if (trial % 7 == 0) y = { 1.0f, 1.0f, 0.0f, 0.0f, 1.0f };      // flat segments
            if (trial % 11 == 0) x = { 0.0f, 0.02f, 0.04f, 0.06f, 1.0f };   // tightest left cluster
            if (trial % 13 == 0) x = { 0.0f, 0.94f, 0.96f, 0.98f, 1.0f };
            TrajectoryCurve c;
            c.set (x, y, tension);
            const auto& xs = c.xs();
            const auto& ys = c.ys();
            checkLayout (xs, ys);

            CHECK_NEAR (c.eval (0.0f), ys[0], 1e-6);
            CHECK_NEAR (c.eval (1.0f), ys[4], 1e-6);
            for (std::size_t k = 0; k < 5; ++k)
                CHECK_NEAR (c.eval (xs[k]), ys[k], 1e-5);

            // Per segment: stays between its end values (no overshoot, hence inside 0..1) and is monotone.
            for (std::size_t k = 0; k < 4; ++k)
            {
                const float lo = std::min (ys[k], ys[k + 1]), hi = std::max (ys[k], ys[k + 1]);
                float prev = ys[k];
                const float dir = ys[k + 1] >= ys[k] ? 1.0f : -1.0f;
                for (int i = 0; i <= 40; ++i)
                {
                    const float u = xs[k] + (xs[k + 1] - xs[k]) * static_cast<float> (i) / 40.0f;
                    const float v = c.eval (u);
                    CHECK (std::isfinite (v));
                    CHECK (v >= lo - 1e-5f && v <= hi + 1e-5f);
                    CHECK (v >= 0.0f && v <= 1.0f);
                    CHECK ((v - prev) * dir >= -1.0e-5f);
                    prev = v;
                }
            }
            // out of range / non-finite phase
            CHECK (c.eval (-4.0f) == c.eval (0.0f) && c.eval (9.0f) == c.eval (1.0f));
            CHECK (std::isfinite (c.eval (std::nanf (""))));
        }
    }
    // tension 0 is exactly piecewise linear
    TrajectoryCurve lin;
    lin.set ({ 0.0f, 0.2f, 0.5f, 0.8f, 1.0f }, { 0.0f, 1.0f, 0.2f, 0.9f, 0.4f }, 0.0f);
    CHECK_NEAR (lin.eval (0.1f), 0.5, 1e-6);
    CHECK_NEAR (lin.eval (0.35f), 0.6, 1e-6);
    CHECK_NEAR (lin.eval (0.65f), 0.55, 1e-6);
    // tension 100 % is smooth (continuous slope at the interior points)
    TrajectoryCurve sm;
    sm.set ({ 0.0f, 0.2f, 0.5f, 0.8f, 1.0f }, { 0.1f, 0.5f, 0.9f, 0.6f, 0.2f }, 1.0f);
    const float eps = 1.0e-3f;
    for (const float xk : { 0.2f, 0.5f, 0.8f })
    {
        const float left = (sm.eval (xk) - sm.eval (xk - eps)) / eps;
        const float right = (sm.eval (xk + eps) - sm.eval (xk)) / eps;
        CHECK (std::fabs (left - right) < 0.05f);
    }
    std::cout << "curve endpoint values, no overshoot, per-segment monotonicity (1600 random layouts x 4 tensions) OK\n";
}

void testPlaybackMatchesCurve()
{
    for (const double sr : { 44100.0, 48000.0, 96000.0 })
    {
        Trajectory t;
        t.prepare (sr);
        TrajectoryParams p;
        p.enabled = true;
        p.sync = false;
        p.lengthMs = 600.0f;
        p.curve01 = 0.5f;
        p.loop = false;
        t.setParams (p);
        t.reset();

        const double L = std::round (0.6 * sr);
        CHECK (t.lengthSamples() == L);
        CHECK (t.process() == t.curve().eval (0.0f)); // idle value = first point
        t.noteOn();
        std::vector<float> out;
        const int total = static_cast<int> (L) + 2000;
        for (int n = 0; n < total; ++n)
            out.push_back (t.process());

        double maxErr = 0.0;
        for (int n = 0; n < static_cast<int> (L); ++n)
            maxErr = std::max (maxErr, static_cast<double> (std::fabs (out[static_cast<std::size_t> (n)] - t.curve().eval (static_cast<float> (n) / static_cast<float> (L)))));
        CHECK (maxErr < 2.0e-5);

        // Control points hit within one sample.
        const auto& xs = t.curve().xs();
        const auto& ys = t.curve().ys();
        CHECK (out[0] == ys[0]);
        for (std::size_t k = 1; k < 4; ++k)
        {
            const int idx = static_cast<int> (std::floor (xs[k] * L));
            const float a = out[static_cast<std::size_t> (idx)], b = out[static_cast<std::size_t> (idx + 1)];
            CHECK (ys[k] >= std::min (a, b) - 1e-5f && ys[k] <= std::max (a, b) + 1e-5f);
        }
        // One-shot: holds the return point after the length.
        for (int n = static_cast<int> (L) + 1; n < total; ++n)
            CHECK_NEAR (out[static_cast<std::size_t> (n)], ys[4], 1e-6);
    }
    std::cout << "playback equals the exported control curve within 2e-5, control points hit within one sample, one-shot holds the end\n";
}

void testLoop()
{
    const double sr = 48000.0;
    Trajectory t;
    t.prepare (sr);
    TrajectoryParams p;
    p.enabled = true; p.lengthMs = 100.0f; p.loop = true; p.curve01 = 1.0f;
    t.setParams (p);
    t.reset();
    t.noteOn();
    const int L = static_cast<int> (t.lengthSamples());
    std::vector<float> out;
    for (int n = 0; n < 5 * L; ++n) out.push_back (t.process());
    for (int n = 0; n < 4 * L; ++n)
        CHECK_NEAR (out[static_cast<std::size_t> (n)], out[static_cast<std::size_t> (n + L)], 1e-4); // periodic (y1 == y5 default)

    // y1 != y5: the wrap does not click.
    p.y = { 0.05f, 0.9f, 0.5f, 0.3f, 0.95f };
    t.setParams (p);
    t.reset();
    t.noteOn();
    float prev = t.process();
    double maxStep = 0.0;
    for (int n = 0; n < 5 * L; ++n)
    {
        const float v = t.process();
        maxStep = std::max (maxStep, static_cast<double> (std::fabs (v - prev)));
        prev = v;
    }
    CHECK (maxStep < 0.05);
    std::cout << "loop periodic; click-free wrap (max step " << maxStep << ")\n";
}

void testSyncAndFallback()
{
    const double sr = 48000.0;
    const char* names[6] = { "1/16", "1/8", "1/4", "1/2", "1/1", "2/1" };
    const auto& def = paramDef (P::traj_length_div);
    CHECK (def.numChoices == 6);
    for (int i = 0; i < 6; ++i)
        CHECK (def.choices[static_cast<std::size_t> (i)] == names[i]);

    Trajectory t;
    t.prepare (sr);
    for (const double bpm : { 60.0, 120.0, 180.0 })
        for (int d = 0; d < 6; ++d)
        {
            TrajectoryParams p;
            p.sync = true;
            p.division = d;
            p.lengthMs = 600.0f;
            t.setParams (p);
            t.setHostTiming (timing (bpm));
            CHECK (! t.usingFallbackLength());
            const double expectedMs = trajBeatsPerDivision[d] * 60000.0 / bpm;
            CHECK_NEAR (t.lengthMsEffective(), expectedMs, std::max (1.0, 1000.0 / sr));
        }
    CHECK_NEAR (trajBeatsPerDivision[2] * 60000.0 / 120.0, 500.0, 1e-9); // 1/4 note at 120 BPM

    // Tempo change moves the length; free (ms) mode ignores the tempo.
    TrajectoryParams p;
    p.sync = true; p.division = 2; p.lengthMs = 777.0f;
    t.setParams (p);
    t.setHostTiming (timing (120.0));
    CHECK_NEAR (t.lengthMsEffective(), 500.0, 0.05);
    t.setHostTiming (timing (60.0));
    CHECK_NEAR (t.lengthMsEffective(), 1000.0, 0.05);
    p.sync = false;
    t.setParams (p);
    CHECK_NEAR (t.lengthMsEffective(), 777.0, 0.05);
    CHECK (! t.usingFallbackLength());

    // No valid tempo: fall back to the ms length and say so.
    p.sync = true;
    t.setParams (p);
    t.setHostTiming (timing (120.0, false));
    CHECK (t.usingFallbackLength());
    CHECK_NEAR (t.lengthMsEffective(), 777.0, 0.05);
    for (const double bad : { 0.0, -1.0, std::numeric_limits<double>::quiet_NaN() })
    {
        t.setHostTiming (timing (bad, true));
        CHECK (t.usingFallbackLength());
        CHECK_NEAR (t.lengthMsEffective(), 777.0, 0.05);
    }

    // ms range clamps
    p.sync = false; p.lengthMs = 1.0f;
    t.setParams (p);
    CHECK_NEAR (t.lengthMsEffective(), 50.0, 0.05);
    p.lengthMs = 1.0e9f;
    t.setParams (p);
    CHECK_NEAR (t.lengthMsEffective(), 4000.0, 0.05);

    // Running playback reaches the end of a synced one-shot after the synced length.
    p.sync = true; p.division = 1; p.lengthMs = 600.0f;   // 1/8 at 120 BPM = 250 ms
    t.setParams (p);
    t.setHostTiming (timing (120.0));
    t.reset();
    t.noteOn();
    const int L = static_cast<int> (t.lengthSamples());
    CHECK (L == static_cast<int> (std::lround (0.25 * sr)));
    for (int n = 0; n < L - 1; ++n) t.process();
    const float nearEnd = t.process();
    CHECK_NEAR (nearEnd, t.curve().ys()[4], 2.0e-3);
    std::cout << "sync lengths (6 divisions x 60/120/180 BPM), tempo tracking, ms mode, fallback flag OK\n";
}

void testRetriggerAndLegato()
{
    const double sr = 48000.0;
    Trajectory t;
    t.prepare (sr);
    TrajectoryParams p;
    p.enabled = true; p.lengthMs = 200.0f; p.curve01 = 0.5f;
    t.setParams (p);
    t.reset();
    CHECK (! t.isRunning());
    t.noteOn();
    float v = 0.0f;
    for (int i = 0; i < 2500; ++i) v = t.process(); // somewhere near the peak
    CHECK (std::fabs (v - t.curve().ys()[0]) > 0.05f);

    // legato hook: no restart while running
    const float before = v;
    t.noteOn (false);
    const float after = t.process();
    CHECK (std::fabs (after - before) < 0.01f);

    // retrigger restarts, continuous at the join, then converges to the curve start
    t.noteOn (true);
    const float joined = t.process();
    CHECK (std::fabs (joined - after) < 0.01f);
    float last = joined;
    for (int i = 0; i < static_cast<int> (0.01 * sr); ++i) last = t.process();
    CHECK (std::fabs (last - t.curve().eval (0.01f / 0.2f)) < 0.01f);
    // after the 2 ms de-click offset has run out the output equals the curve exactly
    t.noteOn (true);
    for (int i = 0; i < 200; ++i) t.process();
    const float onCurve = t.process();
    CHECK_NEAR (onCurve, t.curve().eval (200.0f / static_cast<float> (t.lengthSamples())), 1e-4);

    // retrigger from idle: starts exactly on the first point
    t.reset();
    t.noteOn (false);
    CHECK (t.isRunning());
    CHECK (t.process() == t.curve().ys()[0]);
    std::cout << "retrigger click-safe, legato hook OK\n";
}

void testExtremesDeterminismParams()
{
    // Extreme curve + shortest length + retrigger spam stays finite and in 0..1.
    Trajectory t;
    t.prepare (192000.0);
    TrajectoryParams p;
    p.lengthMs = 50.0f; p.curve01 = 1.0f; p.loop = true;
    p.x = { 0.0f, 0.02f, 0.04f, 0.06f, 1.0f };
    p.y = { 0.0f, 1.0f, 0.0f, 1.0f, 0.0f };
    t.setParams (p);
    t.reset();
    for (int i = 0; i < 400000; ++i)
    {
        if (i % 777 == 0) t.noteOn (true);
        const float v = t.process();
        CHECK (std::isfinite (v) && v >= 0.0f && v <= 1.0f);
    }

    auto run = [] (Trajectory& tr)
    {
        std::vector<float> out;
        TrajectoryParams q;
        q.sync = true; q.division = 3; q.loop = true; q.curve01 = 0.8f;
        tr.setParams (q);
        tr.setHostTiming (timing (133.0));
        tr.reset();
        tr.noteOn();
        for (int i = 0; i < 30000; ++i)
        {
            if (i == 9000) tr.noteOn (true);
            if (i == 15000) tr.setHostTiming (timing (90.0));
            out.push_back (tr.process());
        }
        return out;
    };
    Trajectory a, b;
    a.prepare (44100.0);
    b.prepare (44100.0);
    const auto r1 = run (a), r2 = run (a), r3 = run (b);
    CHECK (std::memcmp (r1.data(), r2.data(), r1.size() * sizeof (float)) == 0);
    CHECK (std::memcmp (r1.data(), r3.data(), r1.size() * sizeof (float)) == 0);

    ParamSnapshot s; // defaults from the parameter table
    auto d = TrajectoryParams::from (s);
    CHECK (! d.enabled && ! d.sync && d.lengthMs == 600.0f && d.division == 3 && d.dest == TrajDest::Cutoff);
    CHECK_NEAR (d.depth01, 0.7, 1e-6);
    CHECK_NEAR (d.curve01, 0.5, 1e-6);
    CHECK (! d.loop);
    CHECK (d.x[0] == 0.0f && d.x[4] == 1.0f && d.x[1] == 0.15f && d.x[2] == 0.40f && d.x[3] == 0.70f);
    CHECK (d.y[0] == 0.20f && d.y[1] == 0.90f && d.y[2] == 0.50f && d.y[3] == 0.30f && d.y[4] == 0.20f);
    s[P::traj_enabled] = 1.0f;
    s[P::traj_time_mode] = 1.0f;
    s[P::traj_length_div] = 5.0f;
    s[P::traj_dest] = 2.0f;
    s[P::traj_loop] = 1.0f;
    s[P::traj_x3] = 0.55f;
    s[P::traj_y5] = 0.8f;
    d = TrajectoryParams::from (s);
    CHECK (d.enabled && d.sync && d.division == 5 && d.dest == TrajDest::Both && d.loop);
    CHECK (d.x[2] == 0.55f && d.y[4] == 0.8f);

    // Preset/state round-trip: parameter values -> params -> curve control points keep order and values.
    TrajectoryCurve c;
    c.set (d.x, d.y, d.curve01);
    for (std::size_t k = 0; k < 5; ++k)
    {
        CHECK (c.xs()[k] == d.x[k]);
        CHECK (c.ys()[k] == d.y[k]);
    }
    std::cout << "extremes, determinism, snapshot decoding, control-point round trip OK\n";
}

void testNoAllocation()
{
    Trajectory t;
    t.prepare (48000.0);
    TrajectoryParams p;
    p.sync = true; p.loop = true;
    t.setParams (p);
    float sink = 0.0f;
    synthtest::NoAllocScope scope;
    t.setHostTiming (timing (128.0));
    t.reset();
    t.noteOn();
    for (int i = 0; i < 50000; ++i)
    {
        if (i % 9000 == 0) { t.noteOn (true); t.setParams (p); t.setHostTiming (timing (100.0 + i / 1000.0)); }
        sink += t.process();
    }
    CHECK (scope.count() == 0);
    CHECK (std::isfinite (sink));
}
} // namespace

int main()
{
    testMonotonicXSafety();
    testCurveEndpointsAndBounds();
    testPlaybackMatchesCurve();
    testLoop();
    testSyncAndFallback();
    testRetriggerAndLegato();
    testExtremesDeterminismParams();
    testNoAllocation();
    std::cout << "TrajectoryTests passed\n";
    return 0;
}
