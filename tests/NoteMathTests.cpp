#ifdef NDEBUG
#undef NDEBUG
#endif
#include "Engine/NoteMath.h"

#include <cassert>
#include <cmath>
#include <iostream>

using namespace snaral;

static double centsOff (double hz, double ref) { return 1200.0 * std::log2 (hz / ref); }

int main()
{
    // --- tuning: A4 reference at endpoints and default, within 0.1 cent (we expect far tighter)
    assert (std::fabs (noteToHz (69, 440.0) - 440.0) < 1e-9);
    for (const double a4 : { 430.0, 440.0, 450.0 })
    {
        assert (std::fabs (noteToHz (69, a4) - a4) < 1e-9);
        for (int n = 0; n <= 127; ++n)
        {
            const long double ref = static_cast<long double> (a4) * std::pow (2.0L, (static_cast<long double> (n) - 69.0L) / 12.0L);
            assert (std::fabs (centsOff (noteToHz (n, a4), static_cast<double> (ref))) < 0.1);
        }
    }
    // A4=430/450 absolute values for note 57 (A3) = a4/2
    assert (std::fabs (noteToHz (57, 430.0) - 215.0) < 1e-9 && std::fabs (noteToHz (57, 450.0) - 225.0) < 1e-9);
    // guaranteed range 24..84 and extended 12..96 are finite and monotonic
    for (int n = 12; n < 96; ++n) assert (noteToHz (n + 1) > noteToHz (n));

    // --- transpose +-12 = exactly one octave, with and without layer cents; both layers (same pitch chain)
    for (int n = 24; n <= 84; ++n)
        for (const double cents : { 0.0, 7.0, -13.5 })
        {
            const double base = noteToHz (n, 440.0, 0.0, cents);
            assert (std::fabs (noteToHz (n, 440.0, 12.0, cents) / base - 2.0) < 1e-12);
            assert (std::fabs (noteToHz (n, 440.0, -12.0, cents) / base - 0.5) < 1e-12);
            assert (std::fabs (noteToHz (n, 440.0, 24.0, cents) / base - 4.0) < 1e-12);
        }
    // layer cents come last: +100 cents == +1 semitone
    assert (std::fabs (noteToHz (60, 440.0, 0.0, 100.0) / noteToHz (61, 440.0, 0.0, 0.0) - 1.0) < 1e-12);
    // transpose is applied before cents, so transpose + cents equals the same total semitones
    assert (std::fabs (noteToHz (60, 440.0, 3.0, 50.0) - semitonesToHz (63.5)) < 1e-9);

    // --- pitch bend endpoints / centre for every range, within 0.01 semitone (we are exact)
    for (int range = 1; range <= 12; ++range)
    {
        assert (std::fabs (bendSemitones (-8192, range) + range) < 1e-9);
        assert (bendSemitones (0, range) == 0.0);
        assert (std::fabs (bendSemitones (8191, range) - range) < 1e-9);
        assert (std::fabs (bendSemitones (4096, range) - range * 4096.0 / 8191.0) < 1e-9);
        assert (std::fabs (bendSemitones (-4096, range) + range * 0.5) < 1e-9);
    }
    assert (std::fabs (bendSemitones (100000, 2) - 2.0) < 1e-9 && std::fabs (bendSemitones (-100000, 2) + 2.0) < 1e-9);
    assert (bendSemitones (0, 2) == 0.0);
    // bend after transpose: bend does not scale with transpose
    assert (std::fabs ((transposedPitch (60, 5) + bendSemitones (8191, 2)) - 67.0) < 1e-9);

    // --- velocity: defaults have no effect; enabled is monotonic
    for (int v = 0; v <= 127; ++v)
    {
        assert (velocityToLevelGain (v, 0.0f) == 1.0f);
        assert (velocityToAttackScale (v, 0.0f) == 1.0f);
    }
    for (const float amt : { 25.0f, 50.0f, 100.0f })
    {
        float pg = -1.0f, ps = 2.0f;
        for (int v = 1; v <= 127; ++v)
        {
            const float g = velocityToLevelGain (v, amt), s = velocityToAttackScale (v, amt);
            assert (g >= pg && g >= 0.0f && g <= 1.0f);
            assert (s <= ps && s >= 0.0f && s <= 1.0f);
            if (v > 1) { assert (g > pg); assert (s < ps); }
            pg = g; ps = s;
        }
        assert (velocityToLevelGain (127, amt) == 1.0f);
    }
    assert (velocityToLevelGain (1, 100.0f) == 0.0f && velocityToAttackScale (127, 100.0f) == 0.0f);

    // --- glide: exact timing, linear in semitones (constant Hz ratio per sample), 0 ms disables
    {
        const double sr = 48000.0;
        PitchGlide g;
        g.prepare (sr);
        g.reset (36.0);
        g.setTarget (48.0, 100.0f, true);
        const int N = 4800;
        double prev = 36.0, firstRatio = 0.0;
        for (int i = 1; i <= N; ++i)
        {
            const double v = g.next();
            assert (v > prev && std::fabs (v - (36.0 + 12.0 * i / N)) < 1e-9);
            const double ratio = semitonesToHz (v) / semitonesToHz (prev);
            if (i == 1) firstRatio = ratio; else assert (std::fabs (ratio - firstRatio) < 1e-9);
            if (i < N) assert (g.isGliding());
            prev = v;
        }
        assert (prev == 48.0 && ! g.isGliding() && g.next() == 48.0);
        // downward + retarget mid-glide restarts from the current pitch, full glide time again
        g.setTarget (36.0, 100.0f, true);
        for (int i = 0; i < 2400; ++i) g.next();
        const double mid = g.current();
        assert (std::fabs (mid - 42.0) < 1e-9);
        g.setTarget (60.0, 50.0f, true);
        for (int i = 0; i < 2399; ++i) g.next();
        assert (g.current() < 60.0 && g.isGliding());
        assert (g.next() == 60.0 && ! g.isGliding());
        // disabled: 0 ms, or glide not allowed (initial note-on) => snap
        g.setTarget (30.0, 0.0f, true);
        assert (g.current() == 30.0 && ! g.isGliding());
        g.setTarget (40.0, 200.0f, false);
        assert (g.current() == 40.0 && g.next() == 40.0);
        // glide time at other sample rates: 500 ms at 44.1 kHz
        PitchGlide h;
        h.prepare (44100.0);
        h.reset (24.0);
        h.setTarget (84.0, 500.0f, true);
        for (int i = 0; i < 22049; ++i) h.next();
        assert (h.isGliding());
        assert (h.next() == 84.0 && ! h.isGliding());
    }

    std::cout << "NoteMathTests passed\n";
    return 0;
}
