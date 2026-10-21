#include "TestSupport.h"

#include <iostream>
#include <thread>

#include "Core/MidiLearn.h"

using namespace snaral;
using namespace snaral::midi;
using namespace snaral::testing;

namespace
{
MidiMapping mk (int target, int cc, int ch = 0, float mn = 0.0f, float mx = 1.0f, bool inv = false)
{
    MidiMapping m; m.target = target; m.cc = cc; m.channel = ch; m.min = mn; m.max = mx; m.invert = inv;
    return m;
}

void testClassifier()
{
    // reserved set
    for (int cc : { 0, 6, 32, 38, 64, 98, 99, 100, 101, 120, 121, 122, 123, 124, 125, 126, 127 })
        CHECK_MSG (isReservedCc (cc), std::to_string (cc));
    for (int cc : { 1, 7, 10, 11, 74, 97, 102, 119 })
        CHECK_MSG (! isReservedCc (cc), std::to_string (cc));
    CHECK (isReservedCc (-1) && isReservedCc (128));

    // learnable checked against the registry
    int n = 0;
    for (int i = 0; i < parameterCount; ++i)
    {
        const auto& d = parameterTable[static_cast<std::size_t> (i)];
        bool expect = d.type != ParamType::Choice && d.automatable() && (d.flags & pfInteger) == 0;
        if (d.id == "master_tune_hz" || d.id == "limiter_enabled" || d.id == "limiter_ceiling_db" || d.id == "bypass"
            || d.id == "sub_mute" || d.id == "mid_mute")
            expect = false;
        CHECK_MSG (isLearnable (i) == expect, std::string (d.id));
        n += expect ? 1 : 0;
    }
    CHECK (n > 40);
    for (auto id : { "weight", "growl", "bite", "motion", "space", "output_trim_db", "legato", "anchor_enabled", "anchor_strength",
                     "filter_cutoff_hz", "traj_enabled", "furnace_mix", "fracture_mix" })
        CHECK_MSG (isLearnable (findParameterIndex (id)), id);
    for (auto id : { "voice_mode", "layer_solo", "oversampling_mode", "bypass", "master_tune_hz", "limiter_enabled",
                     "traj_x2", "pitch_bend_range", "transpose_semitones", "anchor_note" })
        CHECK_MSG (! isLearnable (findParameterIndex (id)), id);
    CHECK (learnableParameters().size() == static_cast<std::size_t> (n));
}

void testConversion()
{
    const auto m = mk (P::bite, 74);
    CHECK (convertCc (m, 0) == 0.0f);
    CHECK (convertCc (m, 127) == 1.0f);
    CHECK (convertCc (m, 1) > 0.0f);
    CHECK (std::abs (convertCc (m, 64) - 64.0f / 127.0f) < 1e-6f);
    const auto inv = mk (P::bite, 74, 0, 0.0f, 1.0f, true);
    CHECK (convertCc (inv, 0) == 1.0f);
    CHECK (convertCc (inv, 127) == 0.0f);
    const auto ranged = mk (P::bite, 74, 0, 0.25f, 0.75f);
    CHECK (convertCc (ranged, 0) == 0.25f && convertCc (ranged, 127) == 0.75f);
    const auto rinv = mk (P::bite, 74, 0, 0.25f, 0.75f, true);
    CHECK (convertCc (rinv, 0) == 0.75f && convertCc (rinv, 127) == 0.25f);
    // monotonic, both directions, over a range
    for (const auto& mm : { m, inv, ranged, rinv })
    {
        float prev = convertCc (mm, 0);
        for (int v = 1; v <= 127; ++v)
        {
            const float x = convertCc (mm, v);
            CHECK (mm.invert ? x <= prev : x >= prev);
            CHECK (x >= mm.min && x <= mm.max);
            prev = x;
        }
    }
    // fixtures from the spec: CC values 0, 1, 64, 126, 127 (values, not CC numbers)
    for (int v : { 0, 1, 64, 126, 127 })
        CHECK (convertCc (m, v) + convertCc (inv, v) > 0.999999f && convertCc (m, v) + convertCc (inv, v) < 1.000001f);
}

void testApplyAndBool()
{
    auto t = MidiLearnTable::build ({ mk (P::bite, 74), mk (P::legato, 20, 3), mk (P::weight, 21, 0, 0.0f, 1.0f, true) });
    MidiLearnRuntime rt;
    auto a = rt.apply (*t, 74, 5, 127);
    CHECK (a.mapped && a.apply && a.target == P::bite && a.normalised == 1.0f && a.domain == 100.0f);
    a = rt.apply (*t, 74, 5, 0);
    CHECK (a.domain == 0.0f);
    CHECK (! rt.apply (*t, 75, 5, 10).mapped);
    CHECK (! rt.apply (*t, 20, 2, 10).mapped);                  // explicit channel 3 only
    CHECK (rt.apply (*t, 20, 3, 64).domain == 1.0f);            // 64/127 = 0.504 -> on
    CHECK (rt.apply (*t, 20, 3, 63).domain == 0.0f);            // 0.496 -> off
    CHECK (rt.apply (*t, 21, 16, 0).domain == 100.0f);          // inverted weight
    // same macro via UI mapping fromNormalized gives same value
    const auto& def = paramDef (P::bite);
    CHECK (rt.apply (*t, 74, 1, 40).domain == def.fromNormalized (40.0f / 127.0f));
    // reserved CCs are never in a table even if forced
    auto bad = MidiLearnTable::build ({ mk (P::bite, 64), mk (P::bite, 120), mk (P::voice_mode, 5) });
    CHECK (bad->count() == 0);
    for (int ch = 1; ch <= 16; ++ch) CHECK (bad->find (64, ch) == -1 && bad->find (123, ch) == -1);
}

void testPickup()
{
    auto t = MidiLearnTable::build ({ mk (P::bite, 74) });
    MidiLearnRuntime rt;
    rt.setMode (MidiLearnRuntime::Mode::Pickup);
    rt.notifyExternalChange (0, 0.5f);
    auto a = rt.apply (*t, 74, 1, 0);
    CHECK (a.mapped && ! a.apply);             // far below: ignored
    a = rt.apply (*t, 74, 1, 30);
    CHECK (! a.apply);
    a = rt.apply (*t, 74, 1, 100);              // crossed 0.5 between 30 and 100
    CHECK (a.apply);
    CHECK (rt.apply (*t, 74, 1, 0).apply);      // engaged: follows
    rt.notifyExternalChange (0, 0.9f);          // UI moved it: re-arm
    CHECK (! rt.apply (*t, 74, 1, 10).apply);
    CHECK (rt.apply (*t, 74, 1, 114).apply);    // lands on 0.9
    // Bool never needs pickup
    auto tb = MidiLearnTable::build ({ mk (P::legato, 20) });
    MidiLearnRuntime rb; rb.setMode (MidiLearnRuntime::Mode::Pickup); rb.notifyExternalChange (0, 1.0f);
    CHECK (rb.apply (*tb, 20, 1, 0).apply);
    // Jump mode (default) applies immediately
    MidiLearnRuntime rj; rj.notifyExternalChange (0, 0.5f);
    CHECK (rj.apply (*t, 74, 1, 0).apply);
}

void testTextRoundTrip()
{
    MidiMapData d;
    d.mappings = { mk (P::bite, 74), mk (P::weight, 1, 5, 0.1f, 0.9f, true), mk (P::legato, 20, 16) };
    const auto s = toText (d);
    const auto r = fromText (s);
    CHECK_MSG (r.ok, r.error);
    CHECK (r.data.mappings == d.mappings);
    CHECK (toText (r.data) == s);
    CHECK (fromText ("snaral-midimap 1\n").ok);

    const std::string h = "snaral-midimap 1\n";
    CHECK (! fromText ("").ok);
    CHECK (! fromText ("snaral-midimap 2\n").ok);
    CHECK (! fromText (h + "map target=nope cc=1\n").ok);
    CHECK (! fromText (h + "map target=voice_mode cc=1\n").ok);                     // not learnable
    CHECK (! fromText (h + "map target=bite cc=64\n").ok);                          // reserved
    CHECK (! fromText (h + "map target=bite cc=128\n").ok);
    CHECK (! fromText (h + "map target=bite cc=1 ch=17\n").ok);
    CHECK (! fromText (h + "map target=bite cc=1 min=0.9 max=0.1\n").ok);
    CHECK (! fromText (h + "map target=bite cc=1 min=-0.1\n").ok);
    CHECK (! fromText (h + "map target=bite cc=1 invert=2\n").ok);
    CHECK (! fromText (h + "map target=bite cc=1\nmap target=bite cc=2\n").ok);     // duplicate target
    CHECK (! fromText (h + "map target=bite cc=1\nmap target=weight cc=1\n").ok);   // omni overlap
    CHECK (! fromText (h + "map target=bite cc=1 ch=2\nmap target=weight cc=1 ch=2\n").ok);
    CHECK (fromText (h + "map target=bite cc=1 ch=2\nmap target=weight cc=1 ch=3\n").ok);
    CHECK (! fromText (h + "map target=bite cc=1 bogus=3\n").ok);
    // 17 entries
    std::string big = h;
    const auto ids = learnableParameters();
    for (int i = 0; i < 17; ++i)
        big += "map target=" + std::string (parameterTable[static_cast<std::size_t> (ids[static_cast<std::size_t> (i)])].id) + " cc=" + std::to_string (10 + i) + "\n";
    CHECK (! fromText (big).ok);
    CHECK (fromText (big.substr (0, big.rfind ("map target"))).ok);               // 16 entries
    // garbage never crashes
    Rng rng (5);
    for (int i = 0; i < 2000; ++i)
    {
        std::string g = h;
        for (int k = 0; k < 40; ++k) g.push_back (static_cast<char> (rng.below (128)));
        (void) fromText (g);
    }
}

void testLearnFlow()
{
    MidiLearnModel m;
    CHECK (m.arm (P::voice_mode) == MidiLearnModel::ArmResult::NotLearnable);
    CHECK (! m.isArmed());

    // reserved ignored while armed
    CHECK (m.arm (P::bite) == MidiLearnModel::ArmResult::Armed);
    auto r = m.onCcCaptured (64, 0);
    CHECK (r.kind == MidiLearnModel::CaptureKind::Reserved && r.status == "Reserved MIDI message" && m.isArmed());
    r = m.onCcCaptured (74, 0);
    CHECK (r.kind == MidiLearnModel::CaptureKind::Assigned);
    CHECK (r.status == "CC 74 ch 1 mapped to Bite");
    CHECK (r.assigned.min == 0.0f && r.assigned.max == 1.0f && r.assigned.channel == 0 && ! r.assigned.invert);
    CHECK (! m.isArmed() && m.mappings().size() == 1);

    // capture while not armed: ignored
    CHECK (m.onCcCaptured (10, 0).kind == MidiLearnModel::CaptureKind::NotArmed);

    // cancel leaves mappings unchanged
    m.arm (P::weight);
    m.cancel();
    CHECK (m.mappings().size() == 1 && ! m.isArmed());

    // switching target while armed
    m.arm (P::weight); m.arm (P::growl);
    CHECK (m.armedTarget() == P::growl);
    m.cancel();

    // conflict: cc already used by Bite; Cancel
    m.arm (P::growl);
    r = m.onCcCaptured (74, 2);
    CHECK (r.kind == MidiLearnModel::CaptureKind::NeedsChoice && r.conflict.ccHolders.size() == 1);
    CHECK (r.conflict.keepBothAvailable && r.conflict.suggestedExistingChannel != 0 && r.conflict.suggestedExistingChannel != 3);
    CHECK (m.hasPendingChoice());
    m.resolve (MidiLearnModel::Choice::Cancel);
    CHECK (m.mappings().size() == 1 && m.mappings()[0].target == P::bite);

    // conflict: Replace
    m.arm (P::growl); m.onCcCaptured (74, 2);
    m.resolve (MidiLearnModel::Choice::Replace);
    CHECK (m.mappings().size() == 1 && m.mappings()[0].target == P::growl && m.mappings()[0].cc == 74);

    // conflict: Keep both (existing growl omni -> pinned; bite explicit channel 3)
    m.arm (P::bite); r = m.onCcCaptured (74, 2);
    CHECK (r.kind == MidiLearnModel::CaptureKind::NeedsChoice);
    const auto st = m.resolve (MidiLearnModel::Choice::KeepBoth, 3); // pin to 3 == captured channel 3: rejected
    CHECK (m.hasPendingChoice() && ! st.empty());
    m.resolve (MidiLearnModel::Choice::KeepBoth, 7);
    CHECK (m.mappings().size() == 2 && validateMappings (m.mappings()).empty());
    {
        const auto& g = m.mappings()[static_cast<std::size_t> (m.indexOfTarget (P::growl))];
        const auto& b = m.mappings()[static_cast<std::size_t> (m.indexOfTarget (P::bite))];
        CHECK (g.channel == 7 && b.channel == 3 && g.cc == 74 && b.cc == 74);
        auto t = MidiLearnTable::build (m.mappings());
        CHECK (t->mapping (t->find (74, 3)).target == P::bite && t->mapping (t->find (74, 7)).target == P::growl && t->find (74, 1) == -1);
    }

    // keep both unavailable when holder shares the exact channel
    m.arm (P::space); r = m.onCcCaptured (74, 2);   // holders: bite ch3 (explicit, equal), growl ch7
    CHECK (r.kind == MidiLearnModel::CaptureKind::NeedsChoice && ! r.conflict.keepBothAvailable && ! r.conflict.keepBothUnavailableReason.empty());
    m.resolve (MidiLearnModel::Choice::KeepBoth);
    CHECK (m.hasPendingChoice());                    // refused, still pending
    m.resolve (MidiLearnModel::Choice::Cancel);
    CHECK (m.mappings().size() == 2);

    // target already mapped: Replace / Cancel
    m.arm (P::bite); r = m.onCcCaptured (30, 0);
    CHECK (r.kind == MidiLearnModel::CaptureKind::NeedsChoice && r.conflict.targetAlreadyMapped && r.conflict.ccHolders.empty());
    m.resolve (MidiLearnModel::Choice::Cancel);
    CHECK (m.mappings()[static_cast<std::size_t> (m.indexOfTarget (P::bite))].cc == 74);
    m.arm (P::bite); m.onCcCaptured (30, 0); m.resolve (MidiLearnModel::Choice::Replace);
    CHECK (m.mappings()[static_cast<std::size_t> (m.indexOfTarget (P::bite))].cc == 30 && m.mappings().size() == 2);

    // remove / clear
    CHECK (m.remove (P::bite) && ! m.remove (P::bite) && m.mappings().size() == 1);
    m.clearAll();
    CHECK (m.mappings().empty());

    // 16 limit
    const auto ids = learnableParameters();
    for (int i = 0; i < 16; ++i)
    {
        m.arm (ids[static_cast<std::size_t> (i)]);
        CHECK (m.onCcCaptured (10 + i, 0).kind == MidiLearnModel::CaptureKind::Assigned);
    }
    m.arm (ids[16]);
    r = m.onCcCaptured (50, 0);
    CHECK (r.kind == MidiLearnModel::CaptureKind::LimitReached && m.mappings().size() == 16 && ! m.isArmed());
    // remapping an existing target at the limit is allowed (replace flow)
    m.arm (ids[0]); r = m.onCcCaptured (60, 0);
    CHECK (r.kind == MidiLearnModel::CaptureKind::NeedsChoice);
    m.resolve (MidiLearnModel::Choice::Replace);
    CHECK (m.mappings().size() == 16);

    // load / update validation
    MidiLearnModel m2;
    MidiMapData bad; bad.mappings = { mk (P::bite, 1), mk (P::weight, 1) };
    std::string err;
    CHECK (! m2.load (bad, &err) && ! err.empty() && m2.mappings().empty());
    MidiMapData good; good.mappings = { mk (P::bite, 1), mk (P::weight, 2) };
    CHECK (m2.load (good));
    auto edit = mk (P::bite, 1, 0, 0.3f, 0.2f);
    CHECK (! m2.update (edit, &err));
    edit = mk (P::bite, 1, 4, 0.2f, 0.8f, true);
    CHECK (m2.update (edit) && m2.mappings()[0].invert);
    CHECK (! m2.update (mk (P::bite, 2)));        // would collide with weight (omni)
}

void testCaptureHandoff()
{
    MidiLearnModel m;
    m.arm (P::bite);
    auto& cap = m.capture();
    cap.offer (64, 0);                  // reserved
    cap.offer (74, 4);                  // first eligible
    cap.offer (75, 0);                  // later: ignored
    MidiLearnModel::CaptureResult r;
    CHECK (m.pollCapture (r) && r.kind == MidiLearnModel::CaptureKind::Assigned);
    CHECK (r.assigned.cc == 74 && m.mappings().size() == 1);
    CHECK (! m.pollCapture (r));
    cap.offer (10, 0);                  // not armed any more: nothing
    CHECK (! m.pollCapture (r));

    m.arm (P::weight);
    m.capture().offer (120, 0);
    CHECK (m.pollCapture (r) && r.kind == MidiLearnModel::CaptureKind::Reserved && m.isArmed());
    CHECK (! m.pollCapture (r));
    m.cancel();
    m.capture().offer (30, 0);
    CHECK (! m.pollCapture (r));
}

void testPublisherAndStorm()
{
    TablePublisher pub;
    CHECK (pub.current() && pub.current()->count() == 0);
    std::vector<MidiMapping> list = { mk (P::bite, 74), mk (P::weight, 1), mk (P::growl, 2), mk (P::space, 3) };
    pub.publish (list);
    CHECK (pub.current()->count() == 4);

    // CC storm on the "audio thread" while the message thread republishes
    std::atomic<bool> stop { false };
    std::thread ui ([&] {
        int i = 0;
        while (! stop.load())
        {
            pub.publish ((i++ & 1) ? list : std::vector<MidiMapping> { mk (P::bite, 74) });
            std::this_thread::sleep_for (std::chrono::microseconds (200));
        }
    });
    MidiLearnRuntime rt;
    Rng rng (99);
    std::array<float, 128> lastValueForBite {};
    long applied = 0;
    for (int i = 0; i < 300000; ++i)
    {
        TablePublisher::ReadGuard guard (pub); // BUG-001: the unguarded current() read raced with publish() under load
        const auto* t = guard.table;
        const int cc = rng.below (128), ch = 1 + rng.below (16), v = rng.below (128);
        const auto a = rt.apply (*t, cc, ch, v);
        if (a.mapped)
        {
            ++applied;
            CHECK (std::isfinite (a.normalised) && a.normalised >= 0.0f && a.normalised <= 1.0f);
            CHECK (std::isfinite (a.domain));
            CHECK (cc != 64 && cc < 120 && cc != 0 && cc != 6);
            if (a.target == P::bite) lastValueForBite[0] = a.normalised;
        }
    }
    stop = true;
    ui.join();
    CHECK (applied > 0);
}
} // namespace

int main()
{
    testClassifier();
    testConversion();
    testApplyAndBool();
    testPickup();
    testTextRoundTrip();
    testLearnFlow();
    testCaptureHandoff();
    testPublisherAndStorm();
    std::cout << "MidiLearnTests OK\n";
    return 0;
}
