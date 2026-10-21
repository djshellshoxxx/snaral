// Functional tests of the complete SnaralEngine (JUCE-free). Acceptance criteria of sub-mid-engine, macro-mapping,
// midi-voice, expansion-oversampling, host-integration (latency, tempo flags), realtime-performance (allocation counter).
#include "EngineOutputTestUtil.h"
#include "EngineRig.h"
#include "Engine/Voice.h"

#include <cstdio>
#include <functional>
#include <string>

using namespace snaral;
using namespace rig;
using T = EngineMidiEvent::Type;

static double goertzel (const std::vector<float>& v, std::size_t from, std::size_t to, double f, double sr)
{
    to = std::min (to, v.size());
    const double w = 2.0 * 3.14159265358979323846 * f / sr;
    double re = 0.0, im = 0.0;
    for (std::size_t i = from; i < to; ++i)
    {
        re += v[i] * std::cos (w * static_cast<double> (i));
        im += v[i] * std::sin (w * static_cast<double> (i));
    }
    return 2.0 * std::sqrt (re * re + im * im) / static_cast<double> (to - from);
}

static double noteHz (int n) { return 440.0 * std::pow (2.0, (n - 69) / 12.0); }

// ------------------------------------------------------------------------------------------------------------
static std::vector<float> subRender (const std::function<void (Rig&)>& mod, int note = 36, int samples = 24000, bool right = false)
{
    Rig r;
    r.soloSub();
    mod (r);
    r.noteOn (note);
    r.run (samples, 256);
    return right ? r.R : r.L;
}

static void testSubIsolation()
{
    const auto ref = subRender ([] (Rig&) {});
    CHECK (peak (ref) > 0.4);

    struct Case { const char* name; std::function<void (Rig&)> mod; };
    const Case cases[] = {
        { "bite", [] (Rig& r) { r.set (P::bite, 100); } },
        { "drive", [] (Rig& r) { r.set (P::drive_amount, 100); r.set (P::drive_type, 1); r.set (P::drive_tone, 0); r.set (P::drive_mix, 100); r.set (P::bite, 100); } },
        { "texture", [] (Rig& r) { r.set (P::noise_level, 100); r.set (P::metal_level, 100); r.set (P::metal_decay_ms, 2000); } },
        { "space", [] (Rig& r) { r.set (P::space, 100); } },
        { "filter", [] (Rig& r) { r.set (P::filter_cutoff_hz, 20); r.set (P::filter_resonance, 80); r.set (P::filter_type, 1); } },
        { "furnace", [] (Rig& r) { r.set (P::furnace_enabled, 1); r.set (P::furnace_mix, 100); r.set (P::furnace_resonance, 100); r.set (P::motion, 100); r.set (P::furnace_env_amount, 100); } },
        { "fracture", [] (Rig& r) { r.set (P::fracture_enabled, 1); r.set (P::fracture_mix, 100); r.set (P::fracture_drive_low, 100); r.set (P::fracture_drive_mid, 100); r.set (P::fracture_drive_high, 100); } },
        { "motion", [] (Rig& r) { r.set (P::motion, 100); r.set (P::contour_depth, 100); r.set (P::lfo_depth, 100); r.set (P::lfo_rate_hz, 20); r.set (P::traj_enabled, 1); r.set (P::traj_dest, 2); } },
        { "mid sources", [] (Rig& r) { r.set (P::mid_source_a, 3); r.set (P::mid_source_b, 2); r.set (P::mid_blend, 100); r.set (P::mid_table_position, 100); r.set (P::mid_pulse_width, 20); r.set (P::mid_tune_cents, 50); } },
        { "mid level/adsr", [] (Rig& r) { r.set (P::mid_level, 0); r.set (P::growl, 100); r.set (P::mid_amp_attack_ms, 500); r.set (P::mid_amp_release_ms, 5000); } },
        { "growl 0", [] (Rig& r) { r.set (P::growl, 0); } },
        { "mid mute", [] (Rig& r) { r.set (P::mid_mute, 1); } },
        { "everything mid", [] (Rig& r) {
              r.set (P::bite, 100); r.set (P::motion, 100); r.set (P::space, 100); r.set (P::drive_amount, 100); r.set (P::noise_level, 100);
              r.set (P::metal_level, 100); r.set (P::furnace_enabled, 1); r.set (P::fracture_enabled, 1); r.set (P::filter_cutoff_hz, 200);
              r.set (P::traj_enabled, 1); r.set (P::lfo_depth, 100); r.set (P::mid_blend, 50); } },
    };
    for (const auto& c : cases)
    {
        const auto out = subRender (c.mod);
        if (! identical (out, ref))
        {
            std::fprintf (stderr, "sub changed by '%s' (max diff %g)\n", c.name, maxDiff (out, ref));
            CHECK (false);
        }
    }
    // Randomised mid-side parameters (seeded), many trials: still bit-exact.
    for (int trial = 0; trial < 12; ++trial)
    {
        const auto out = subRender ([trial] (Rig& r)
        {
            static const P::Index midSide[] = { P::bite, P::space, P::motion, P::growl, P::drive_amount, P::drive_tone, P::drive_mix,
                P::noise_level, P::noise_tone, P::metal_level, P::metal_tune, P::filter_cutoff_hz, P::filter_resonance, P::contour_depth,
                P::lfo_depth, P::lfo_rate_hz, P::furnace_vowel, P::furnace_mix, P::fracture_mix, P::fracture_crossover_hz,
                P::mid_blend, P::mid_table_position, P::mid_tune_cents, P::mid_level, P::traj_depth, P::traj_curve };
            std::uint32_t s = 12345u + 977u * static_cast<std::uint32_t> (trial);
            for (const auto i : midSide)
            {
                s = s * 1664525u + 1013904223u;
                r.setPercent (i, static_cast<float> (s >> 8) / 16777216.0f);
            }
            r.set (P::furnace_enabled, trial & 1); r.set (P::fracture_enabled, (trial >> 1) & 1); r.set (P::traj_enabled, (trial >> 2) & 1);
            r.set (P::mid_source_a, static_cast<float> (trial & 3)); r.set (P::mid_source_b, static_cast<float> ((trial >> 1) & 3));
        });
        CHECK (identical (out, ref));
    }

    // With the mid layer audible and the limiter off: (total - mid only) equals the sub-only render.
    auto render = [] (int solo)
    {
        Rig r;
        r.set (P::limiter_enabled, 0);
        r.set (P::layer_solo, static_cast<float> (solo));
        r.set (P::space, 60); r.set (P::bite, 70); r.set (P::drive_amount, 50); r.set (P::noise_level, 30);
        r.noteOn (36);
        r.run (24000, 256);
        struct Out { std::vector<float> L, R; };
        return Out { r.L, r.R };
    };
    const auto both = render (0), subOnly = render (1), midOnly = render (2);
    double worst = 0.0;
    for (std::size_t i = 0; i < both.L.size(); ++i)
        worst = std::max ({ worst, std::fabs (static_cast<double> (both.L[i]) - midOnly.L[i] - subOnly.L[i]),
                            std::fabs (static_cast<double> (both.R[i]) - midOnly.R[i] - subOnly.R[i]) });
    CHECK (worst < 1.0e-6);
    CHECK (peak (midOnly.L) > 0.05 && peak (subOnly.L) > 0.4);
    std::printf ("sub isolation ok (%zu cases + 12 random, additive residual %.2e)\n", sizeof (cases) / sizeof (cases[0]), worst);
}

static void testSubCentered()
{
    for (const float space : { 0.0f, 50.0f, 100.0f })
    {
        const auto l = subRender ([space] (Rig& r) { r.set (P::space, space); r.set (P::bite, 100); });
        const auto r = subRender ([space] (Rig& rr) { rr.set (P::space, space); rr.set (P::bite, 100); }, 36, 24000, true);
        CHECK (peak (l) > 0.4);
        CHECK (identical (l, r));
    }
    // Mid audible: Space 0 -> dual mono (L == R exactly); Space 100 -> the mid is wide but the sub-only component stays centred.
    for (const float space : { 0.0f, 100.0f })
    {
        Rig r;
        r.set (P::space, space);
        r.noteOn (48);
        r.run (24000);
        double side = 0.0;
        for (std::size_t i = 0; i < r.L.size(); ++i) side = std::max (side, std::fabs (static_cast<double> (r.L[i]) - r.R[i]));
        if (space == 0.0f) CHECK (side < 1.0e-6); // <= -120 dBFS
        else CHECK (side > 0.01);
    }
    std::printf ("sub centered ok\n");
}

static void testWeightGrowl()
{
    {
        const auto z = subRender ([] (Rig& r) { r.set (P::weight, 0); });
        CHECK (peak (z) == 0.0);
        const auto a = subRender ([] (Rig& r) { r.set (P::weight, 50); r.set (P::sub_amp_attack_ms, 0); r.set (P::limiter_enabled, 0); });
        const auto b = subRender ([] (Rig& r) { r.set (P::weight, 100); r.set (P::sub_amp_attack_ms, 0); r.set (P::limiter_enabled, 0); });
        const double ratio = rmsOf (b, 6000, 20000) / rmsOf (a, 6000, 20000);
        CHECK (std::fabs (20.0 * std::log10 (ratio / 4.0)) < 0.05); // x^2: 0.25 -> 1.0
        const auto sl0 = subRender ([] (Rig& r) { r.set (P::sub_level, 0); });
        CHECK (peak (sl0) == 0.0);
    }
    auto mid = [] (float weight, float growl)
    {
        Rig r;
        r.soloMid();
        r.set (P::weight, weight);
        r.set (P::growl, growl);
        r.set (P::noise_level, 100); r.set (P::metal_level, 100); r.set (P::metal_decay_ms, 1000);
        r.noteOn (36);
        r.run (24000);
        return r.L;
    };
    CHECK (peak (mid (75, 0)) == 0.0);  // Growl = 0 silences mid AND texture
    CHECK (peak (mid (75, 100)) > 0.05);
    CHECK (identical (mid (0, 65), mid (100, 65))); // Weight never reaches the mid layer
    std::printf ("weight/growl ok\n");
}

static void testMotionBite()
{
    auto render = [] (const std::function<void (Rig&)>& mod)
    {
        Rig r;
        r.soloMid();
        r.set (P::motion, 0);
        r.set (P::bite, 0);
        r.set (P::drive_mix, 0); // drive out of the picture for the filter-part checks
        mod (r);
        r.noteOn (36);
        r.run (24000);
        return r.L;
    };
    const auto base = render ([] (Rig&) {});
    CHECK (peak (base) > 0.05);
    // Motion = 0, Bite = 0: no modulation at all -> bit-identical whatever the modulation depths and shapes.
    CHECK (identical (render ([] (Rig& r) { r.set (P::contour_depth, 100); r.set (P::contour_attack_ms, 100); r.set (P::contour_sustain, 100); }), base));
    CHECK (identical (render ([] (Rig& r) { r.set (P::lfo_depth, 100); r.set (P::lfo_rate_hz, 12); r.set (P::lfo_waveform, 3); }), base));
    CHECK (identical (render ([] (Rig& r) { r.set (P::traj_enabled, 1); r.set (P::traj_depth, 100); r.set (P::traj_dest, 2); r.set (P::traj_y2, 1.0f); }), base));
    CHECK (identical (render ([] (Rig& r) { r.set (P::furnace_enabled, 1); r.set (P::furnace_mix, 100); r.set (P::furnace_env_amount, 100); r.set (P::furnace_lfo_amount, 100); }),
                      render ([] (Rig& r) { r.set (P::furnace_enabled, 1); r.set (P::furnace_mix, 100); r.set (P::furnace_env_amount, 0); r.set (P::furnace_lfo_amount, 0); })));
    // Motion > 0 does modulate.
    const auto moved = render ([] (Rig& r) { r.set (P::motion, 100); r.set (P::contour_depth, 100); });
    CHECK (diffRmsRel (moved, base) > 0.05);
    // Bite works at Motion = 0 (filter part: contour peak offset), independent of Motion, and is bounded by contour depth only through Motion.
    const auto bite = render ([] (Rig& r) { r.set (P::bite, 100); });
    CHECK (diffRmsRel (bite, base) > 0.05);
    CHECK (identical (render ([] (Rig& r) { r.set (P::bite, 100); r.set (P::contour_depth, 0); }), bite)); // Bite does not use contour depth
    // Drive part of Bite at Motion = 0: once the contour has fully decayed (sustain 0) the filter part vanishes; the drive offset remains.
    auto driveCase = [&] (float biteVal)
    {
        Rig r;
        r.soloMid();
        r.set (P::motion, 0); r.set (P::bite, biteVal); r.set (P::contour_sustain, 0); r.set (P::contour_decay_ms, 5);
        r.set (P::drive_amount, 40); r.set (P::drive_mix, 100);
        r.noteOn (36);
        r.run (24000);
        return r.L;
    };
    const auto d0 = driveCase (0), d1 = driveCase (100);
    std::vector<float> a (d0.begin() + 12000, d0.end()), b (d1.begin() + 12000, d1.end());
    CHECK (diffRmsRel (b, a) > 0.02);
    std::printf ("motion/bite ok (bite filter %.3f, bite drive %.3f)\n", diffRmsRel (bite, base), diffRmsRel (b, a));
}

static void testLimiter()
{
    for (const float ceilingDb : { -12.0f, -6.0f, -1.0f, 0.0f })
    {
        Rig r;
        r.set (P::voice_mode, 1);
        r.set (P::weight, 100); r.set (P::growl, 100); r.set (P::output_trim_db, 12);
        r.set (P::limiter_ceiling_db, ceilingDb);
        const int chord[] = { 36, 43, 48, 52, 55, 60, 64, 67 };
        for (const int n : chord) r.noteOn (n, 127);
        r.run (48000);
        const double ceil = std::pow (10.0, ceilingDb / 20.0);
        const double pk = std::max (peak (r.L), peak (r.R));
        CHECK (pk <= ceil * 1.0059);   // safety clamp margin 0.05 dB
        CHECK (pk >= ceil * 0.9);      // the limiter is actually working at the ceiling
        CHECK (finite (r.L) && finite (r.R));

        Rig u;
        u.set (P::voice_mode, 1); u.set (P::weight, 100); u.set (P::growl, 100); u.set (P::output_trim_db, 12); u.set (P::limiter_enabled, 0);
        for (const int n : chord) u.noteOn (n, 127);
        u.run (48000);
        CHECK (peak (u.L) > 2.0 * ceil || ceilingDb == 0.0f);
    }
    std::printf ("limiter ceiling ok\n");
}

static void testVoiceModes()
{
    const double sr = 48000.0;
    // ---- mono, no legato: last-note priority, return to the held note ----
    {
        Rig r;
        r.soloSub();
        r.noteOn (36);
        r.run (12000);
        r.noteOn (40);
        r.run (12000);
        r.noteOff (40);
        r.run (12000);
        const double f1 = crossingFrequency (r.L, 6000, 11500, sr), f2 = crossingFrequency (r.L, 18000, 23500, sr),
                     f3 = crossingFrequency (r.L, 30000, 35500, sr);
        CHECK (std::fabs (1200.0 * std::log2 (f1 / noteHz (36))) < 3.0);
        CHECK (std::fabs (1200.0 * std::log2 (f2 / noteHz (40))) < 3.0);
        CHECK (std::fabs (1200.0 * std::log2 (f3 / noteHz (36))) < 3.0);
        r.noteOff (36);
        r.run (24000);
        CHECK (peak (r.L, 44000) < 1.0e-6);
        CHECK (r.eng.activeVoiceCount() == 0);
    }
    // ---- legato vs retrigger (envelope bump after the second note) ----
    auto bump = [] (bool legato)
    {
        Rig r;
        r.soloSub();
        r.set (P::legato, legato ? 1.0f : 0.0f);
        r.set (P::sub_amp_attack_ms, 0); r.set (P::sub_amp_decay_ms, 5); r.set (P::sub_amp_sustain, 50);
        r.noteOn (36);
        r.run (12000);
        r.noteOn (36 + 7);
        r.run (12000);
        return rmsOf (r.L, 12000, 12000 + 240) / rmsOf (r.L, 22000, 22240);
    };
    const double bl = bump (true), bn = bump (false);
    CHECK (bl < 1.1);
    CHECK (bn > 1.3);
    // ---- glide: first note never glides, overlapped note slides ----
    {
        Rig r;
        r.soloSub();
        r.set (P::glide_ms, 200); r.set (P::legato, 1);
        r.noteOn (36);
        r.run (12000);
        const double start = crossingFrequency (r.L, 4000, 11500, sr);
        CHECK (std::fabs (1200.0 * std::log2 (start / noteHz (36))) < 3.0);
        r.noteOn (48);
        r.run (4800);  // 100 ms into a 200 ms glide -> halfway in semitones (42)
        const double mid = crossingFrequency (r.L, 12000 + 4200, 12000 + 4800, sr);
        CHECK (mid > noteHz (36) * 1.25 && mid < noteHz (48) * 0.85);
        r.run (24000);
        const double end = crossingFrequency (r.L, 12000 + 18000, 12000 + 28000, sr);
        CHECK (std::fabs (1200.0 * std::log2 (end / noteHz (48))) < 3.0);
    }
    // ---- poly: three voices sound together, steal order, all released ----
    {
        Rig r;
        r.soloSub();
        r.set (P::voice_mode, 1);
        for (const int n : { 36, 43, 48 }) r.noteOn (n);
        r.run (24000);
        CHECK (r.eng.activeVoiceCount() == 3);
        for (const int n : { 36, 43, 48 })
            CHECK (goertzel (r.L, 6000, 24000, noteHz (n), sr) > 0.05);
        CHECK (goertzel (r.L, 6000, 24000, noteHz (40), sr) < 0.15 * goertzel (r.L, 6000, 24000, noteHz (36), sr));
        for (int n = 50; n < 58; ++n) r.noteOn (n); // 11 notes -> steals
        r.run (4800);
        CHECK (r.eng.activeVoiceCount() == 8);
        for (int n = 36; n < 70; ++n) r.noteOff (n);
        r.run (48000);
        CHECK (r.eng.activeVoiceCount() == 0);
        CHECK (peak (r.L, r.L.size() - 4800) == 0.0);
    }
    // ---- velocity: no effect by default, level maps when enabled ----
    {
        auto level = [] (int vel, float amount)
        {
            Rig r; r.soloSub(); r.set (P::velocity_to_level, amount); r.set (P::sub_amp_attack_ms, 0);
            r.noteOn (36, vel); r.run (12000);
            return rmsOf (r.L, 6000, 12000);
        };
        CHECK (std::fabs (level (20, 0) - level (127, 0)) < 1.0e-9);
        CHECK (level (20, 100) < 0.2 * level (127, 100));
    }
    // ---- sample-accurate event placement ----
    {
        Rig r;
        r.soloSub();
        r.set (P::sub_amp_attack_ms, 0);
        r.run (1, 1);
        r.clear();
        r.run (4000, 256, { { 1000, noteOnEv (60) }, { 1000, noteOnEv (64) } });
        std::size_t first = 0;
        while (first < r.L.size() && r.L[first] == 0.0f) ++first;
        CHECK (first == 1000 + static_cast<std::size_t> (r.eng.latencySamples()) + 1);
    }
    std::printf ("voice modes ok (legato bump %.2f vs retrigger %.2f)\n", bl, bn);
}

static void testSustainPedal()
{
    for (const bool poly : { true, false })
    {
        Rig r;
        r.soloSub();
        r.set (P::voice_mode, poly ? 1.0f : 0.0f);
        r.apply();
        r.eng.handleMidi (ccEv (64, 127));
        r.noteOn (48);
        r.run (4800);
        r.noteOff (48);
        r.run (24000); // far beyond the 120 ms release: pedal keeps the note
        CHECK (rmsOf (r.L, 20000, 28000) > 0.2);
        r.eng.handleMidi (ccEv (64, 0));
        r.run (24000);
        CHECK (peak (r.L, r.L.size() - 12000) < 1.0e-6);
    }
    std::printf ("sustain pedal ok\n");
}

static void testPanic()
{
    for (int variant = 0; variant < 3; ++variant)
    {
        Rig r;
        r.set (P::voice_mode, 1);
        r.set (P::sub_amp_release_ms, 5000); r.set (P::mid_amp_release_ms, 5000);
        if (variant >= 1)
        {
            r.set (P::furnace_enabled, 1); r.set (P::furnace_resonance, 100); r.set (P::furnace_mix, 100);
            r.set (P::fracture_enabled, 1); r.set (P::fracture_mix, 100); r.set (P::drive_amount, 70); r.set (P::space, 100);
        }
        if (variant == 2) r.set (P::oversampling_mode, 2);
        for (const int n : { 36, 43, 48, 52 }) r.noteOn (n);
        r.run (9600);
        CHECK (peak (r.L, 4800) > 0.1);
        r.eng.panic();
        const std::size_t t0 = r.L.size();
        r.run (4800);
        const std::size_t quiet = t0 + static_cast<std::size_t> (r.eng.latencySamples()) + 240 + 4;
        CHECK (peak (r.L, quiet) < 1.0e-6 && peak (r.R, quiet) < 1.0e-6);
        CHECK (r.eng.activeVoiceCount() == 0);
        // a new note after panic plays normally (nothing left ducked)
        r.noteOn (36);
        r.run (9600);
        CHECK (peak (r.L, r.L.size() - 4800) > 0.1);
        CHECK (finite (r.L));
    }
    // panic clears the pedal
    {
        Rig r;
        r.soloSub();
        r.eng.handleMidi (ccEv (64, 127));
        r.noteOn (48);
        r.noteOff (48);
        r.run (4800);
        r.eng.panic();
        r.noteOn (50);
        r.noteOff (50);
        r.run (24000);
        CHECK (peak (r.L, r.L.size() - 6000) < 1.0e-6); // pedal state was cleared: the note-off released it
    }
    std::printf ("panic ok\n");
}

static void testOversamplingLatencyAndAlignment()
{
    struct Mode { int idx; int mid; };
    const Mode modes[] = { { 0, 0 }, { 1, 46 }, { 2, 56 } };
    std::vector<float> refSub, refMid;
    for (const auto& m : modes)
    {
        // Sub-only: bit-exact copy of the Off render, delayed by the mid-chain latency.
        Rig s;
        s.soloSub(); s.set (P::oversampling_mode, static_cast<float> (m.idx)); s.set (P::sub_amp_attack_ms, 0);
        s.noteOn (36);
        s.run (12000);
        const int lat = s.eng.latencySamples();
        CHECK (lat == SnaralEngine::latencyForModeIndex (m.idx, 48000.0));
        CHECK (lat == SnaralEngine::latencyForModeIndex (0, 48000.0) + m.mid);
        // absolute: first non-zero output sample = latency + 1 (sine starts at 0, attack 0)
        std::size_t first = 0;
        while (first < s.L.size() && s.L[first] == 0.0f) ++first;
        CHECK (first == static_cast<std::size_t> (lat) + 1);
        if (m.idx == 0) refSub = s.L;
        else
        {
            std::vector<float> shifted (refSub.size(), 0.0f);
            for (std::size_t i = static_cast<std::size_t> (m.mid); i < shifted.size(); ++i) shifted[i] = refSub[i - static_cast<std::size_t> (m.mid)];
            CHECK (identical (shifted, s.L));
        }

        // Mid-only with every mid effect transparent: output equals the Off render delayed by the same amount.
        Rig d;
        d.soloMid(); d.set (P::oversampling_mode, static_cast<float> (m.idx)); d.set (P::growl, 40); d.set (P::drive_mix, 0);
        d.noteOn (36);
        d.run (12000);
        if (m.idx == 0) refMid = d.L;
        else
        {
            std::vector<float> shifted (refMid.size(), 0.0f);
            for (std::size_t i = static_cast<std::size_t> (m.mid); i < shifted.size(); ++i) shifted[i] = refMid[i - static_cast<std::size_t> (m.mid)];
            CHECK (identical (shifted, d.L));
        }

        // Full chain finite and loud, nonlinear stages on.
        Rig f;
        f.set (P::oversampling_mode, static_cast<float> (m.idx)); f.set (P::drive_amount, 100); f.set (P::drive_type, 1);
        f.set (P::fracture_enabled, 1); f.set (P::fracture_mix, 100); f.set (P::fracture_drive_mid, 100); f.set (P::space, 70);
        f.noteOn (48);
        f.run (24000);
        CHECK (finite (f.L) && finite (f.R) && peak (f.L, 4000) > 0.1);
    }
    // Sub/mid alignment: mid and sub of the same note are phase-related; the chord render with sub+mid equals the sum of the
    // separately delayed layers (both layers carry exactly the same delay).
    {
        auto total = [] (int solo, int os)
        {
            Rig r; r.set (P::limiter_enabled, 0); r.set (P::layer_solo, static_cast<float>(solo)); r.set (P::oversampling_mode, static_cast<float> (os)); r.set (P::drive_mix, 0);
            r.set (P::growl, 40);
            r.noteOn (36); r.run (12000); return r.L;
        };
        for (const int os : { 1, 2 })
        {
            const auto both = total (0, os), sub = total (1, os), mid = total (2, os);
            double worst = 0.0;
            for (std::size_t i = 0; i < both.size(); ++i) worst = std::max (worst, std::fabs (static_cast<double> (both[i]) - sub[i] - mid[i]));
            CHECK (worst < 1.0e-6);
            const auto subOff = total (1, 0), midOff = total (2, 0);
            const int m = os == 1 ? 46 : 56;
            for (std::size_t i = static_cast<std::size_t> (m); i < both.size(); ++i)
            {
                CHECK (sub[i] == subOff[i - static_cast<std::size_t> (m)]);
                CHECK (mid[i] == midOff[i - static_cast<std::size_t> (m)]);
            }
        }
    }
    // Mode switching during a sustained note: finite, no huge steps.
    {
        Rig r;
        r.set (P::drive_amount, 60); r.set (P::fracture_enabled, 1); r.set (P::fracture_mix, 100);
        r.noteOn (48);
        r.run (4800);
        int mode = 0;
        double maxStep = 0.0;
        for (int i = 0; i < 40; ++i)
        {
            mode = (mode + 1) % 3;
            r.set (P::oversampling_mode, static_cast<float> (mode));
            r.run (1200, 256);
        }
        CHECK (finite (r.L) && finite (r.R));
        for (std::size_t i = 4800 + 1; i < r.L.size(); ++i) maxStep = std::max (maxStep, std::fabs (static_cast<double> (r.L[i]) - r.L[i - 1]));
        CHECK (maxStep < 1.0);
        std::printf ("  oversampling switch max step %.3f\n", maxStep);
    }
    std::printf ("oversampling latency/alignment ok\n");
}

static void testBypass()
{
    Rig r;
    r.noteOn (48);
    r.run (12000);
    CHECK (rmsOf (r.L, 9000, 12000) > 0.1);
    r.set (P::bypass, 1);
    r.run (4800);
    CHECK (peak (r.L, 12000 + 481) == 0.0 && peak (r.R, 12000 + 481) == 0.0); // silent after 10 ms
    CHECK (peak (r.L, 12000, 12000 + 480) > 0.0);
    // linear 10 ms fade: |x[k]| <= A (1 - k/480) (A = pre-bypass peak, small margin for the slowly evolving tone)
    const double A = peak (r.L, 6000, 12000);
    for (std::size_t k = 0; k < 480; ++k)
        CHECK (std::fabs (r.L[12000 + k]) <= 1.05 * A * (1.0 - static_cast<double> (k) / 480.0) + 1.0e-4);
    CHECK (r.eng.activeVoiceCount() == 1); // the voice keeps running while bypassed
    r.set (P::bypass, 0);
    r.run (4800);
    CHECK (rmsOf (r.L, r.L.size() - 2400, r.L.size()) > 0.1); // resumes mid note
    std::printf ("bypass ok\n");
}

static void testTempoAndSharedLfo()
{
    Rig r;
    r.set (P::lfo_mode, 1);
    r.run (256);
    HostTiming t;
    t.bpmValid = false;
    r.eng.setHostTiming (t);
    CHECK (r.eng.diagnostics().tempoUnavailable());
    CHECK (r.eng.diagnostics().syncFallbackActive());       // sync selected, never saw a tempo
    t.bpm = 120.0; t.bpmValid = true;
    r.eng.setHostTiming (t);
    CHECK (! r.eng.diagnostics().tempoUnavailable());
    CHECK (! r.eng.diagnostics().syncFallbackActive());
    t.bpmValid = false;
    r.eng.setHostTiming (t);
    CHECK (r.eng.diagnostics().tempoUnavailable());          // flag raised ...
    CHECK (! r.eng.diagnostics().syncFallbackActive());      // ... but the last valid tempo keeps being used
    t.bpm = 5.0; t.bpmValid = true;                          // out of the 20..999 range is invalid
    r.eng.setHostTiming (t);
    CHECK (r.eng.diagnostics().tempoUnavailable());
    t.bpm = std::nan (""); r.eng.setHostTiming (t);
    CHECK (r.eng.diagnostics().tempoUnavailable());

    // Sync cycle length: 1/4 note = one beat. 60/120/180 BPM -> 48000/24000/16000 samples within 1 sample.
    for (const double bpm : { 60.0, 120.0, 180.0 })
    {
        SharedLfo l;
        l.prepare (48000.0);
        LfoParams p;
        p.sync = true; p.division = 6; p.wave = LfoWave::Saw; p.retrigger = false;
        l.setParams (p);
        l.setTiming (bpm, true, false, false, 0.0);
        const double expected = 48000.0 * 60.0 / bpm;
        CHECK (std::fabs (l.rateHz() - bpm / 60.0) < 1e-12);
        // measure the saw wrap (phase reset) distance
        double prevPhase = l.phase();
        std::vector<int> wraps;
        for (int i = 0; i < static_cast<int> (3.2 * expected); ++i)
        {
            l.process();
            if (l.phase() < prevPhase) wraps.push_back (i);
            prevPhase = l.phase();
        }
        CHECK (wraps.size() >= 3);
        CHECK (std::fabs (static_cast<double> (wraps[1] - wraps[0]) - expected) <= 1.0);
        CHECK (std::fabs (static_cast<double> (wraps[2] - wraps[1]) - expected) <= 1.0);
    }
    // PPQ realignment: playing transport at ppq 2.25 (1/4 division => phase 0.25), then a loop jump back to 0.5 -> phase 0.5.
    {
        SharedLfo l;
        l.prepare (48000.0);
        LfoParams p; p.sync = true; p.division = 6; p.retrigger = false;
        l.setParams (p);
        l.setTiming (120.0, true, true, true, 2.25);
        CHECK (std::fabs (l.phase() - 0.25) < 1e-9);
        for (int i = 0; i < 12000; ++i) l.process();                // 0.5 beat later
        l.setTiming (120.0, true, true, true, 2.75);                 // consistent: no realign (phase continuous)
        CHECK (std::fabs (l.phase() - 0.75) < 1e-6);
        l.setTiming (120.0, true, true, true, 0.5);                  // loop jump
        CHECK (std::fabs (l.phase() - 0.5) < 1e-9);
        l.setTiming (120.0, true, false, true, 0.5);                 // stopped: free running, no NaN
        for (int i = 0; i < 100; ++i) CHECK (std::isfinite (l.process()));
    }
    std::printf ("tempo / shared lfo ok\n");
}

static void testDiagnostics()
{
    Rig r;
    r.noteOn (48);
    r.run (12000);
    const auto& d = r.eng.diagnostics();
    CHECK (d.peak (0) > 0.1 && d.peak (1) > 0.1);
    CHECK (d.rms (0) > 0.01 && d.rms (0) <= d.peak (0) + 1e-6f);
    CHECK (d.activeVoices() == 1);
    CHECK (d.cpuLoad() > 0.0f);
    CHECK (d.latencySamples() == r.eng.latencySamples());
    std::vector<float> scope (512);
    CHECK (d.copyLatest (scope.data(), scope.size()) == 512);
    for (std::size_t i = 0; i < 512; ++i)
        CHECK (std::fabs (scope[i] - 0.5f * (r.L[r.L.size() - 512 + i] + r.R[r.R.size() - 512 + i])) < 1e-6f);
    CHECK (d.scopeWritten() == r.L.size());
    {   // Sub and Mid scope taps (cdl-signal-visualization): both carry signal; the sub tap is the centred layer
        std::vector<float> sub (2048), mid (2048);
        CHECK (d.copyLatestLayer (0, sub.data(), sub.size()) == 2048 && d.copyLatestLayer (1, mid.data(), mid.size()) == 2048);
        CHECK (rig::rmsOf (sub, 0, sub.size()) > 0.01 && rig::rmsOf (mid, 0, mid.size()) > 0.01);
        Rig s;
        s.soloSub();
        s.noteOn (48);
        s.run (12000);
        std::vector<float> t (2048), m2 (2048);
        s.eng.diagnostics().copyLatestLayer (0, t.data(), t.size());
        s.eng.diagnostics().copyLatestLayer (1, m2.data(), m2.size());
        CHECK (rig::rmsOf (t, 0, t.size()) > 0.01 && rig::rmsOf (m2, 0, m2.size()) < 1e-6);
    }
    std::printf ("diagnostics ok (cpu load %.4f)\n", d.cpuLoad());
}

static void testNoAllocationAndDeterminism()
{
    auto script = [] (Rig& r, bool counted)
    {
        std::uint32_t s = 4242u;
        auto rnd = [&s] { s = s * 1664525u + 1013904223u; return static_cast<float> (s >> 8) / 16777216.0f; };
        r.set (P::voice_mode, 1);
        r.set (P::fracture_enabled, 1); r.set (P::furnace_enabled, 1); r.set (P::traj_enabled, 1); r.set (P::oversampling_mode, 2);
        r.set (P::lfo_mode, 1);
        for (int b = 0; b < 300; ++b)
        {
            std::vector<Event> ev;
            const int note = 30 + static_cast<int> (rnd() * 40);
            if (rnd() < 0.4f) ev.push_back ({ static_cast<int> (rnd() * 200), noteOnEv (note) });
            if (rnd() < 0.3f) ev.push_back ({ 150 + static_cast<int> (rnd() * 100), noteOffEv (note) });
            std::sort (ev.begin(), ev.end(), [] (const Event& a, const Event& b2) { return a.at < b2.at; });
            r.setPercent (P::filter_cutoff_hz, rnd());
            r.setPercent (P::drive_amount, rnd());
            r.setPercent (P::space, rnd());
            if (counted)
            {
                NoAlloc guard;
                // run() allocates its own scratch vectors, so call the engine directly while counting
                r.eng.setParameters (r.p);
                HostTiming t; t.bpm = 100 + b % 50; t.bpmValid = true; t.playing = true; t.ppqValid = true; t.ppqPosition = b * 0.1;
                r.eng.setHostTiming (t);
                float l[256], rr[256];
                int off = 0;
                for (const auto& e : ev)
                {
                    if (e.at > off) { r.eng.render (l + off, rr + off, e.at - off); off = e.at; }
                    r.eng.handleMidi (e.e);
                }
                r.eng.render (l + off, rr + off, 256 - off);
                CHECK (guard.count() == 0);
                for (int i = 0; i < 256; ++i) { r.L.push_back (l[i]); r.R.push_back (rr[i]); }
            }
            else
                r.run (256, 256, ev);
        }
        r.eng.panic();
    };
    Rig a, b;
    script (a, true);
    script (b, false);
    // setHostTiming differs between the two scripts, so compare a second pair with identical calls instead:
    Rig c, d;
    script (c, true);
    script (d, true);
    CHECK (identical (c.L, d.L) && identical (c.R, d.R));
    CHECK (finite (a.L) && finite (a.R) && finite (b.L));
    std::printf ("no allocation in render/midi/params, determinism ok\n");
}

static void testPrepareRelease()
{
    auto play = [] (Rig& r)
    {
        r.set (P::voice_mode, 1);
        r.set (P::noise_level, 30); r.set (P::furnace_enabled, 1); r.set (P::oversampling_mode, 1);
        r.noteOn (40); r.noteOn (47);
        r.run (6000, 128);
    };
    Rig fresh;
    play (fresh);
    Rig r;
    const double rates[] = { 44100, 48000, 88200, 96000, 192000 };
    const int blocks[] = { 1, 7, 64, 256, 1024, 4096 };
    for (int i = 0; i < 40; ++i)
    {
        r.eng.prepare (rates[i % 5], blocks[i % 6]);
        r.eng.setParameters (r.p);
        r.noteOn (36 + i % 12);
        std::vector<float> l (300), rr (300);
        r.eng.render (l.data(), rr.data(), 300);
        for (int k = 0; k < 300; ++k) CHECK (std::isfinite (l[k]) && std::isfinite (rr[k]));
        r.eng.panic();
    }
    // parameters survive prepare(); after a final prepare at 48 kHz the engine behaves like a fresh one
    r.eng.prepare (48000.0, 256);
    r.L.clear(); r.R.clear();
    r.sr = 48000.0;
    play (r);
    CHECK (identical (fresh.L, r.L) && identical (fresh.R, r.R));
    std::printf ("repeated prepare ok\n");
}

static void testNotes12to96()
{
    const double sr = 48000.0;
    double worstCents = 0.0;
    for (int note = 12; note <= 96; ++note)
    {
        Rig r;
        r.soloSub();
        r.noteOn (note);
        r.run (note < 24 ? 36000 : 18000, 512);
        CHECK (finite (r.L));
        const double f = crossingFrequency (r.L, 4800, r.L.size(), sr);
        const double cents = 1200.0 * std::log2 (f / noteHz (note));
        worstCents = std::max (worstCents, std::fabs (cents));
        CHECK (std::fabs (cents) < 2.0);
        Rig m;
        m.soloMid();
        m.noteOn (note);
        m.run (4800, 512);
        CHECK (finite (m.L) && peak (m.L) > 0.001);
    }
    // master tune / transpose / sub fine tune / bend
    {
        Rig r; r.soloSub(); r.set (P::master_tune_hz, 432); r.set (P::transpose_semitones, 12); r.set (P::sub_tune_cents, 50);
        r.noteOn (45); r.run (24000);
        const double f = crossingFrequency (r.L, 4800, r.L.size(), sr);
        CHECK (std::fabs (1200.0 * std::log2 (f / (432.0 * std::pow (2.0, (45 + 12 + 0.5 - 69) / 12.0)))) < 2.0); // note 45 + 12 st + 50 ct, A4 = 432 Hz
    }
    {
        Rig r; r.soloSub(); r.set (P::pitch_bend_range, 12);
        r.noteOn (48);
        r.run (4800);
        r.eng.handleMidi (bendEv (8191));
        r.run (12000);
        const double f = crossingFrequency (r.L, 9600, r.L.size(), sr);
        CHECK (std::fabs (1200.0 * std::log2 (f / noteHz (60))) < 2.0);
        r.eng.handleMidi (bendEv (0));
        r.run (12000);
        const double f0 = crossingFrequency (r.L, r.L.size() - 6000, r.L.size(), sr);
        CHECK (std::fabs (1200.0 * std::log2 (f0 / noteHz (48))) < 2.0);
    }
    std::printf ("notes 12..96 ok (worst %.3f cents)\n", worstCents);
}

static void testSampleRates()
{
    for (const double sr : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
    {
        Rig r (sr);
        r.set (P::voice_mode, 1); r.set (P::oversampling_mode, 2); r.set (P::fracture_enabled, 1); r.set (P::furnace_enabled, 1);
        r.set (P::noise_level, 50); r.set (P::metal_level, 50); r.set (P::space, 80);
        for (const int n : { 24, 36, 48, 60 }) r.noteOn (n);
        r.run (static_cast<int> (sr * 0.25), 480);
        CHECK (finite (r.L) && finite (r.R));
        CHECK (peak (r.L) > 0.1 && peak (r.L) <= 1.0);
        CHECK (r.eng.latencySamples() == SnaralEngine::latencyForModeIndex (2, sr));
    }
    std::printf ("sample rates ok\n");
}

int main()
{
    testSubIsolation();
    testSubCentered();
    testWeightGrowl();
    testMotionBite();
    testLimiter();
    testVoiceModes();
    testSustainPedal();
    testPanic();
    testOversamplingLatencyAndAlignment();
    testBypass();
    testTempoAndSharedLfo();
    testDiagnostics();
    testNoAllocationAndDeterminism();
    testPrepareRelease();
    testNotes12to96();
    testSampleRates();
    std::printf ("SnaralEngineTests: all passed\n");
    return 0;
}
