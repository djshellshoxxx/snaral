#include "TestSupport.h"

#include <cstring>
#include <iostream>

#include "Core/Randomizer.h"
#include "State/Pure/SnapshotUtil.h"

using namespace snaral;
using namespace snaral::rnd;
using namespace snaral::testing;

namespace
{
std::uint64_t hashSnapshot (const ParamSnapshot& s)
{
    std::uint64_t h = 1469598103934665603ull;
    for (float f : s.v)
    {
        std::uint32_t u; std::memcpy (&u, &f, 4);
        h = (h ^ u) * 1099511628211ull;
    }
    return h;
}

/// Platform-tolerant hash for golden vectors: values quantised to 1e-3 of their normalised range
/// (libm exp/log may differ in the last ulp between toolchains; the exact-bit check is done same-build above).
std::uint64_t goldenHash (const ParamSnapshot& s)
{
    std::uint64_t h = 1469598103934665603ull;
    for (int i = 0; i < parameterCount; ++i)
    {
        const auto& d = parameterTable[static_cast<std::size_t> (i)];
        const auto q = static_cast<std::uint32_t> (std::lround (d.toNormalized (s.v[static_cast<std::size_t> (i)]) * 1000.0f));
        h = (h ^ q) * 1099511628211ull;
    }
    return h;
}

bool neverRandomized (int i)
{
    static const char* ids[] = { "output_trim_db", "limiter_enabled", "limiter_ceiling_db", "master_tune_hz", "transpose_semitones",
                                 "pitch_bend_range", "voice_mode", "velocity_to_level", "velocity_to_attack", "sub_tune_cents",
                                 "anchor_enabled", "anchor_note", "anchor_strength", "oversampling_mode", "bypass", "layer_solo",
                                 "sub_mute", "mid_mute" };
    for (auto id : ids) if (parameterTable[static_cast<std::size_t> (i)].id == id) return true;
    return false;
}

void testExclusionsAndRegistry()
{
    int n = 0;
    for (int i = 0; i < parameterCount; ++i)
    {
        if (neverRandomized (i)) CHECK_MSG (! isRandomizable (i), std::string (parameterTable[static_cast<std::size_t> (i)].id));
        n += isRandomizable (i) ? 1 : 0;
    }
    CHECK (n > 50);
    // macros have no lock group
    for (auto id : { "weight", "growl", "bite", "motion", "space" })
        CHECK (isRandomizable (findParameterIndex (id)) && lockGroupOf (findParameterIndex (id)) == lockNone);
}

void testWindowsAndSweep()
{
    const ParamSnapshot base;
    // Random source states (valid) incl. extremes
    int notFullyChanged = 0;
    for (std::uint32_t seed = 0; seed < 100000; ++seed)
    {
        const ParamSnapshot src = (seed % 7 == 0) ? randomSnapshot (seed) : base;
        const auto r = randomize (src, seed, (seed & 1) ? 0u : (seed >> 3) & lockAll, 1.0f);
        CHECK (r.ok);
        CHECK (snapshotIsValid (r.snapshot));
        for (int i = 0; i < parameterCount; ++i)
        {
            const float v = r.snapshot.v[static_cast<std::size_t> (i)];
            if (! isRandomizable (i))
            {
                CHECK (std::memcmp (&v, &src.v[static_cast<std::size_t> (i)], sizeof (float)) == 0);
                continue;
            }
            float lo, hi;
            safeWindow (i, lo, hi);
            const auto& d = parameterTable[static_cast<std::size_t> (i)];
            CHECK_MSG (v >= lo - 1e-3f && v <= hi + 1e-3f || v == src.v[static_cast<std::size_t> (i)],
                       std::string (d.id) + " " + std::to_string (v));
            CHECK (v >= d.minimum && v <= d.maximum);
        }
        // sanity bounds from the spec
        CHECK (r.snapshot[P::weight] >= 30.0f && r.snapshot[P::growl] >= 20.0f && r.snapshot[P::bite] <= 70.0f);
        CHECK (r.snapshot[P::space] <= 80.0f);
        CHECK (r.snapshot[P::output_trim_db] == src[P::output_trim_db]);
        CHECK (r.snapshot[P::limiter_enabled] == src[P::limiter_enabled]);
        CHECK (r.snapshot[P::limiter_ceiling_db] == src[P::limiter_ceiling_db]);
        if ((seed & 1) || ! ((seed >> 3) & lockMotion))
        {
            CHECK (r.snapshot[P::traj_x2] < r.snapshot[P::traj_x3] - 0.04f);
            CHECK (r.snapshot[P::traj_x3] < r.snapshot[P::traj_x4] - 0.04f);
            CHECK (r.snapshot[P::traj_x2] > 0.0f && r.snapshot[P::traj_x4] < 1.0f);
        }
        notFullyChanged += r.changedCount == 0 ? 1 : 0;
    }
    CHECK (notFullyChanged == 0);
}

void testDeterminismAndLocks()
{
    const ParamSnapshot base;
    for (std::uint32_t seed : { 0u, 1u, 0xDEADBEEFu })
    {
        const auto a = randomize (base, seed), b = randomize (base, seed);
        CHECK (hashSnapshot (a.snapshot) == hashSnapshot (b.snapshot));
        CHECK (a.changedCount > 20);
    }
    CHECK (hashSnapshot (randomize (base, 1).snapshot) != hashSnapshot (randomize (base, 2).snapshot));

    // golden vectors (algorithm version 1). Regenerate deliberately if the algorithm version changes.
    const std::uint64_t golden0 = goldenHash (randomize (base, 0).snapshot);
    const std::uint64_t golden1 = goldenHash (randomize (base, 1).snapshot);
    const std::uint64_t goldenD = goldenHash (randomize (base, 0xDEADBEEFu).snapshot);
    std::cout << "golden v1 hashes: " << golden0 << " " << golden1 << " " << goldenD << "\n";
    CHECK (golden0 == 12746623522058368183ull);
    CHECK (golden1 == 10346535307260873102ull);
    CHECK (goldenD == 9658786207883575694ull);

    // lock independence: unlocked groups identical regardless of which other groups are locked
    for (std::uint32_t seed = 1; seed < 400; ++seed)
    {
        const auto full = randomize (base, seed, lockNone);
        for (LockMask locks : std::initializer_list<LockMask> { lockVoice, lockSub | lockMotion, lockDrive | lockTexture | lockEffects, lockAll })
        {
            const auto part = randomize (base, seed, locks);
            for (int i = 0; i < parameterCount; ++i)
            {
                const bool locked = isRandomizable (i) && (locks & lockGroupOf (i)) != 0;
                const float want = locked ? base.v[static_cast<std::size_t> (i)] : full.snapshot.v[static_cast<std::size_t> (i)];
                // trajectory ordering fix-up can differ if motion is locked partly; motion is all-or-nothing, so equal
                CHECK_MSG (part.snapshot.v[static_cast<std::size_t> (i)] == want, std::string (parameterTable[static_cast<std::size_t> (i)].id));
            }
        }
        // all locked: only macros change
        const auto onlyMacros = randomize (base, seed, lockAll);
        for (int i = 0; i < parameterCount; ++i)
            if (onlyMacros.snapshot.v[static_cast<std::size_t> (i)] != base.v[static_cast<std::size_t> (i)])
                CHECK (i == P::weight || i == P::growl || i == P::bite || i == P::motion || i == P::space);
    }
}

void testAmountAndInvalid()
{
    const ParamSnapshot base;
    CHECK (hashSnapshot (randomize (base, 77, lockNone, 0.0f).snapshot) == hashSnapshot (base));
    CHECK (hashSnapshot (randomize (base, 77, lockNone, 1.0f).snapshot) == hashSnapshot (randomize (base, 77).snapshot));
    const auto half = randomize (base, 77, lockNone, 0.5f);
    const auto full = randomize (base, 77, lockNone, 1.0f);
    CHECK (half.ok && snapshotIsValid (half.snapshot));
    // continuous macros land between source and target
    const float lo = std::min (base[P::weight], full.snapshot[P::weight]) - 1e-3f, hi = std::max (base[P::weight], full.snapshot[P::weight]) + 1e-3f;
    CHECK (half.snapshot[P::weight] >= lo && half.snapshot[P::weight] <= hi);
    for (std::uint32_t s = 0; s < 3000; ++s)
        CHECK (snapshotIsValid (randomize (randomSnapshot (s), s, s % 64, (s % 11) / 10.0f).snapshot));

    ParamSnapshot bad = base;
    bad[P::weight] = 500.0f;
    const auto r = randomize (bad, 5);
    CHECK (! r.ok && ! r.error.empty() && hashSnapshot (r.snapshot) == hashSnapshot (bad));
    bad = base; bad[P::bite] = std::nanf ("");
    CHECK (! randomize (bad, 5).ok);
}

void testReset()
{
    ParamSnapshot s = randomSnapshot (3);
    const auto r = initializedPatch (s);
    const ParamSnapshot d;
    for (int i = 0; i < parameterCount; ++i)
    {
        const auto& def = parameterTable[static_cast<std::size_t> (i)];
        CHECK (r.v[static_cast<std::size_t> (i)] == (def.inPreset() ? d.v[static_cast<std::size_t> (i)] : s.v[static_cast<std::size_t> (i)]));
    }
    CHECK (snapshotIsValid (r));
}

void testSeedText()
{
    CHECK (seedToHex (0xDEADBEEFu) == "deadbeef" && seedToHex (1) == "00000001");
    std::uint32_t s = 0;
    CHECK (seedFromHex ("DEADBEEF", s) && s == 0xDEADBEEFu);
    CHECK (seedFromHex ("0x1f", s) && s == 0x1f);
    CHECK (! seedFromHex ("", s) && ! seedFromHex ("123456789", s) && ! seedFromHex ("xyz", s));
    CHECK (seedFromClock() != seedFromClock() || seedFromClock() != seedFromClock());
}

void testContextActions()
{
    const int cutoff = P::filter_cutoff_hz;
    CHECK (copyValueText (cutoff, 1200.0f) == "1200");
    CHECK (copyValueText (P::mid_tune_cents, -3.5f) == "-3.5");
    auto ok = [] (int p, const char* t) { auto r = parsePasteText (p, t); CHECK_MSG (r.ok, std::string (t) + " : " + r.error); return r.value; };
    auto bad = [] (int p, const char* t) { auto r = parsePasteText (p, t); CHECK_MSG (! r.ok && ! r.error.empty(), t); };
    CHECK (ok (cutoff, "1200") == 1200.0f);
    CHECK (ok (cutoff, " 1200 Hz ") == 1200.0f);
    CHECK (ok (cutoff, "1.2k") == 1200.0f);
    CHECK (ok (cutoff, "1.2 kHz") == 1200.0f);
    CHECK (ok (cutoff, "20") == 20.0f);
    CHECK (ok (cutoff, "20000") == 20000.0f);
    bad (cutoff, "19"); bad (cutoff, "20001"); bad (cutoff, "21k"); bad (cutoff, "abc"); bad (cutoff, ""); bad (cutoff, "12 ms");
    bad (cutoff, "nan"); bad (cutoff, "inf"); bad (cutoff, "1e999"); bad (cutoff, "1200 Hz Hz"); bad (cutoff, "12,5");
    CHECK (ok (P::weight, "50%") == 50.0f);
    bad (P::weight, "101"); bad (P::weight, "-1"); bad (P::weight, "50 ms");
    CHECK (ok (P::glide_ms, "150 ms") == 150.0f);
    CHECK (ok (P::output_trim_db, "-6 dB") == -6.0f);
    CHECK (ok (P::output_trim_db, "+3") == 3.0f);
    bad (P::output_trim_db, "13");
    CHECK (ok (P::pitch_bend_range, "5") == 5.0f);
    bad (P::pitch_bend_range, "5.5"); bad (P::pitch_bend_range, "13");
    CHECK (ok (P::filter_type, "bandpass") == 1.0f);
    CHECK (ok (P::filter_type, "0") == 0.0f);
    bad (P::filter_type, "2"); bad (P::filter_type, "0.5"); bad (P::filter_type, "notachoice");
    CHECK (ok (P::legato, "on") == 1.0f && ok (P::legato, "0") == 0.0f);
    bad (P::legato, "2");
    // out-of-range is never clamped: nothing returned as value on failure
    CHECK (! parsePasteText (cutoff, "99999").ok);
    // round trip copy -> paste for every parameter value at default
    for (int i = 0; i < parameterCount; ++i)
    {
        const auto& d = parameterTable[static_cast<std::size_t> (i)];
        const auto t = copyValueText (i, d.defaultValue);
        const auto r = parsePasteText (i, t);
        CHECK_MSG (r.ok && r.value == d.defaultValue, std::string (d.id) + " '" + t + "'");
        CHECK (resetParameterValue (i) == d.defaultValue);
    }
}
} // namespace

int main()
{
    testExclusionsAndRegistry();
    testContextActions();
    testReset();
    testSeedText();
    testAmountAndInvalid();
    testDeterminismAndLocks();
    testWindowsAndSweep();
    std::cout << "RandomizeTests OK\n";
    return 0;
}
