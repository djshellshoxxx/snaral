// MIDI-learn hook (Engine/CcHook.h): publisher validation, seqlock under contention, SPSC rings, CC conversion endpoints,
// reserved messages, override rules. JUCE-free.
#include "EngineOutputTestUtil.h"
#include "Engine/CcHook.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

using namespace snaral;

static CcMapping map (P::Index p, int cc, int ch = 0, float mn = 0.0f, float mx = 1.0f, bool inv = false)
{
    CcMapping m;
    m.paramIndex = static_cast<std::int16_t> (p); m.cc = static_cast<std::uint8_t> (cc); m.channel = static_cast<std::uint8_t> (ch);
    m.minNorm = mn; m.maxNorm = mx; m.invert = inv;
    return m;
}

static void testClassifierAndReserved()
{
    for (const int cc : { 0, 32, 6, 38, 64, 98, 99, 100, 101, 120, 121, 122, 123, 124, 125, 126, 127 }) CHECK (isReservedCc (cc));
    for (const int cc : { 1, 7, 10, 74, 71, 33, 63, 65, 97, 102, 119 }) CHECK (! isReservedCc (cc));
    CHECK (isCcLearnable (P::bite) && isCcLearnable (P::weight) && isCcLearnable (P::output_trim_db));
    CHECK (isCcLearnable (P::legato) && isCcLearnable (P::anchor_enabled) && isCcLearnable (P::anchor_strength));
    CHECK (isCcLearnable (P::furnace_enabled) && isCcLearnable (P::traj_depth));
    for (const auto i : { P::master_tune_hz, P::limiter_enabled, P::limiter_ceiling_db, P::bypass, P::sub_mute, P::mid_mute,
                          P::voice_mode, P::layer_solo, P::oversampling_mode, P::drive_type, P::anchor_note, P::pitch_bend_range,
                          P::transpose_semitones })
        CHECK (! isCcLearnable (i));
    CHECK (! isCcLearnable (-1) && ! isCcLearnable (parameterCount));
}

static void testPublisher()
{
    CcMapPublisher pub;
    CcMapTable t;
    std::uint32_t seq = 0;
    CHECK (! pub.fetch (t, seq) || t.count == 0);
    CcMapping ok[] = { map (P::bite, 74), map (P::space, 1, 3, 0.2f, 0.8f, true) };
    CHECK (pub.publish (ok, 2));
    CHECK (pub.fetch (t, seq));
    CHECK (t.count == 2 && t.maps[0].paramIndex == P::bite && t.maps[0].cc == 74 && t.maps[0].channel == 0);
    CHECK (t.maps[1].paramIndex == P::space && t.maps[1].channel == 3 && t.maps[1].invert && t.maps[1].minNorm == 0.2f && t.maps[1].maxNorm == 0.8f);
    CHECK (! pub.fetch (t, seq)); // unchanged

    // rejections leave the published table untouched
    const CcMapping badCc[] = { map (P::bite, 64) };
    const CcMapping badTarget[] = { map (P::voice_mode, 10) };
    const CcMapping badRange[] = { map (P::bite, 10, 0, 0.8f, 0.2f) };
    const CcMapping badChannel[] = { map (P::bite, 10, 17) };
    const CcMapping dupTarget[] = { map (P::bite, 10), map (P::bite, 11) };
    const CcMapping dupPair[] = { map (P::bite, 10), map (P::weight, 10) };
    const CcMapping omniOverlap[] = { map (P::bite, 10, 0), map (P::weight, 10, 5) };
    const CcMapping nanRange[] = { map (P::bite, 10, 0, std::nanf (""), 1.0f) };
    CHECK (! pub.publish (badCc, 1) && ! pub.publish (badTarget, 1) && ! pub.publish (badRange, 1) && ! pub.publish (badChannel, 1));
    CHECK (! pub.publish (dupTarget, 2) && ! pub.publish (dupPair, 2) && ! pub.publish (omniOverlap, 2) && ! pub.publish (nanRange, 1));
    CHECK (! pub.fetch (t, seq));
    // distinct channels on the same cc are fine
    const CcMapping twoCh[] = { map (P::bite, 10, 1), map (P::weight, 10, 2) };
    CHECK (pub.publish (twoCh, 2));
    // 16 ok, 17 rejected
    CcMapping many[17];
    int n = 0;
    for (int i = 0; i < parameterCount && n < 17; ++i)
        if (isCcLearnable (i)) many[n] = map (static_cast<P::Index> (i), 10 + n), ++n;
    CHECK (n == 17 && pub.publish (many, 16) && ! pub.publish (many, 17));
    CHECK (pub.publish (nullptr, 0));
    CHECK (pub.fetch (t, seq) && t.count == 0);
    std::printf ("publisher ok\n");
}

static void testSeqlockContention()
{
    CcMapPublisher pub;
    std::atomic<bool> stop { false };
    std::atomic<long> published { 0 };
    // Every published table has count k and all entries encode k in their cc: a torn read would mix values.
    std::thread writer ([&]
    {
        int k = 1;
        while (! stop.load())
        {
            CcMapping m[16];
            int n = 0;
            for (int i = 0; i < parameterCount && n < k; ++i)
                if (isCcLearnable (i)) m[n] = map (static_cast<P::Index> (i), 10 + k), ++n;
            // same cc for all entries is only valid on distinct channels:
            for (int j = 0; j < n; ++j) m[j].channel = static_cast<std::uint8_t> (1 + j);
            CHECK (pub.publish (m, n));
            published.fetch_add (1);
            k = k % 16 + 1;
            std::this_thread::yield();
        }
    });
    CcMapTable t;
    std::uint32_t seq = 0;
    long fetched = 0;
    const auto start = std::chrono::steady_clock::now();
    while (published.load() < 20000 || std::chrono::steady_clock::now() - start < std::chrono::milliseconds (300))
    {
        if (std::chrono::steady_clock::now() - start > std::chrono::seconds (5)) break;
        if (pub.fetch (t, seq))
        {
            ++fetched;
            CHECK (t.count >= 1 && t.count <= 16);
            for (int j = 0; j < t.count; ++j)
            {
                CHECK (t.maps[static_cast<std::size_t> (j)].cc == 10 + t.count);
                CHECK (t.maps[static_cast<std::size_t> (j)].channel == 1 + j);
            }
        }
    }
    stop = true;
    writer.join();
    CHECK (published.load() > 1000 && fetched > 10);
    std::printf ("seqlock ok (%ld tables published, %ld observed, never torn)\n", published.load(), fetched);
}

static void testRings()
{
    SpscRing<int, 64> ring;
    int v = 0;
    CHECK (! ring.pop (v));
    for (int i = 0; i < 64; ++i) CHECK (ring.push (i));
    CHECK (! ring.push (99) && ring.dropped() == 1);
    for (int i = 0; i < 64; ++i) { CHECK (ring.pop (v)); CHECK (v == i); }
    CHECK (! ring.pop (v));

    SpscRing<int, 1024> r2;
    std::atomic<bool> done { false };
    long sum = 0, count = 0;
    std::thread producer ([&]
    {
        for (int i = 1; i <= 200000;)
            if (r2.push (i)) ++i;
        done = true;
    });
    int last = 0;
    while (count < 200000)
        if (r2.pop (v)) { CHECK (v == last + 1); last = v; sum += v; ++count; }
    producer.join();
    CHECK (count == 200000 && sum == 200000L * 200001L / 2);
    std::printf ("rings ok\n");
}

static void testRouter()
{
    CcMapPublisher pub;
    CcRouter router;
    router.reset();
    CcMapping maps[] = { map (P::bite, 74), map (P::space, 1, 2, 0.25f, 0.75f, true), map (P::legato, 20) };
    CHECK (pub.publish (maps, 3));
    router.refresh (pub);

    // endpoints / conversion
    CHECK (ccToNormalised (0, maps[0]) == 0.0f && ccToNormalised (127, maps[0]) == 1.0f);
    CHECK (ccToNormalised (0, maps[1]) == 0.75f && ccToNormalised (127, maps[1]) == 0.25f);
    float prev = -1.0f;
    for (int v = 0; v <= 127; ++v) { const float n = ccToNormalised (v, maps[0]); CHECK (n > prev || v == 0); prev = n; }

    CcApplied ap;
    CcSeen seen;
    CHECK (! router.handleCc (0, 64, 127));                  // reserved: never consumed
    CHECK (! router.handleCc (0, 50, 10));                   // unmapped
    CHECK (! router.seenQueue().pop (seen));                 // not learning -> nothing captured
    CHECK (router.handleCc (5, 74, 127));
    CHECK (router.appliedQueue().pop (ap) && ap.paramIndex == P::bite && ap.normalised == 1.0f);
    CHECK (! router.handleCc (0, 1, 64));                    // channel 2 only (1-based) -> channel 0 is not matched
    CHECK (router.handleCc (1, 1, 0));
    CHECK (router.appliedQueue().pop (ap) && ap.paramIndex == P::space && ap.normalised == 0.75f);
    CHECK (router.handleCc (0, 20, 127));                    // bool target
    CHECK (router.appliedQueue().pop (ap) && ap.paramIndex == P::legato && ap.normalised == 1.0f);

    // learn capture: first eligible CC; reserved ignored
    router.setLearnActive (true);
    CHECK (! router.handleCc (3, 64, 100));
    router.handleCc (3, 71, 99);
    CHECK (router.seenQueue().pop (seen) && seen.channel == 3 && seen.cc == 71 && seen.value == 99);
    CHECK (! router.seenQueue().pop (seen));
    router.setLearnActive (false);

    // override semantics against a snapshot
    router.handleCc (5, 74, 127);                            // bite -> 100
    ParamSnapshot s;
    s[P::bite] = 20.0f;
    router.applyOverrides (s);
    CHECK (s[P::bite] == 100.0f);                            // unchanged parameter: CC wins
    s[P::bite] = 20.0f;
    router.applyOverrides (s);
    CHECK (s[P::bite] == 100.0f);                            // sticky while the parameter has not moved
    s[P::bite] = 55.0f;                                      // moved by UI / automation
    router.applyOverrides (s);
    CHECK (s[P::bite] == 55.0f);
    s[P::bite] = 20.0f;
    router.applyOverrides (s);
    CHECK (s[P::bite] == 20.0f);                             // override was dropped for good
    router.handleCc (5, 74, 64);
    s[P::bite] = 20.0f;
    router.applyOverrides (s);
    const float expected = paramDef (P::bite).fromNormalized (ccToNormalised (64, maps[0]));
    CHECK (s[P::bite] == expected);
    s[P::bite] = expected;                                   // message thread caught up
    router.applyOverrides (s);
    s[P::bite] = 33.0f;
    router.applyOverrides (s);
    CHECK (s[P::bite] == 33.0f);

    // table change clears overrides
    router.handleCc (5, 74, 127);
    CcMapping other[] = { map (P::growl, 74) };
    CHECK (pub.publish (other, 1));
    router.refresh (pub);
    s[P::bite] = 20.0f;
    router.applyOverrides (s);
    CHECK (s[P::bite] == 20.0f);

    // CC storm: all 128 CCs x 16 channels, no allocation
    {
        NoAlloc guard;
        for (int i = 0; i < 200000; ++i) router.handleCc (i & 15, (i >> 4) & 127, (i * 7) & 127);
        ParamSnapshot s2;
        router.applyOverrides (s2);
        CHECK (guard.count() == 0);
    }
    std::printf ("router ok\n");
}

int main()
{
    testClassifierAndReserved();
    testPublisher();
    testSeqlockContention();
    testRings();
    testRouter();
    std::printf ("CcHookTests: all passed\n");
    return 0;
}
