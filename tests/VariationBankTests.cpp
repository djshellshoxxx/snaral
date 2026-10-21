#include "TestSupport.h"

#include <iostream>

#include "State/Pure/VariationBank.h"

using namespace snaral;
using namespace snaral::testing;

namespace
{
void testSlotsStoreAndRecall()
{
    VariationBank bank;
    for (int i = 0; i < variationSlotCount; ++i)
    {
        CHECK (bank.isEmpty (i));
        CHECK (bank.slot (i).name == std::string (1, static_cast<char> ('A' + i)));
    }
    CHECK (bank.activeIndex() == 0);

    ParamSnapshot live = randomSnapshot (1);
    live[P::bypass] = 1.0f; // utility state must survive recall
    ParamSnapshot out;
    CHECK (! bank.recall (2, live, out)); // empty slot

    // store four different snapshots, recall each exactly
    std::array<ParamSnapshot, 4> stored;
    for (int i = 0; i < 4; ++i)
    {
        stored[static_cast<std::size_t> (i)] = randomSnapshot (100 + static_cast<std::uint64_t> (i));
        CHECK (bank.store (i, stored[static_cast<std::size_t> (i)]));
    }
    CHECK (bank.activeIndex() == 0); // store does not select

    for (int round = 0; round < 3; ++round)
        for (int i = 3; i >= 0; --i)
        {
            CHECK (bank.recall (i, live, out));
            CHECK (bank.activeIndex() == i);
            for (int p = 0; p < parameterCount; ++p)
            {
                const auto idx = static_cast<std::size_t> (p);
                if (parameterTable[idx].inPreset())
                    CHECK (out.v[idx] == stored[static_cast<std::size_t> (i)].v[idx]); // bit-exact, so well within 1e-6
                else
                    CHECK (out.v[idx] == live.v[idx]);
            }
            CHECK (soundParamsEqual (out, stored[static_cast<std::size_t> (i)], snapshotTolerance));
        }

    // overwrite one slot only
    const auto before = bank;
    const auto other = randomSnapshot(555);
    CHECK (bank.store (1, other));
    for (int i : { 0, 2, 3 })
        CHECK (soundParamsEqual (bank.slot (i).snapshot, before.slot (i).snapshot, 0.0f));
    CHECK (soundParamsEqual (bank.slot (1).snapshot, other, 0.0f));

    // clear keeps the name, empties the slot
    CHECK (bank.rename (3, "Heavy"));
    CHECK (bank.clear (3) && bank.isEmpty (3) && bank.slot (3).name == "Heavy");

    // out of range
    CHECK (! bank.store (4, live) && ! bank.store (-1, live) && ! bank.clear (7) && ! bank.setActiveIndex (4));
    CHECK (bank.isEmpty (9) && ! bank.recall (-1, live, out));
}

void testNames()
{
    VariationBank bank;
    CHECK (bank.rename (0, "  Dark Rasp  ") && bank.slot (0).name == "Dark Rasp");
    CHECK (bank.rename (1, "") && bank.slot (1).name == "B"); // empty resets
    CHECK (bank.rename (2, "Caf\xC3\xA9 \xE2\x82\xAC"));
    CHECK (! bank.rename (2, "bad\nname") && bank.slot (2).name == "Caf\xC3\xA9 \xE2\x82\xAC");
    CHECK (! bank.rename (2, std::string (33, 'x')) && bank.rename (2, std::string (32, 'x')));
    CHECK (! bank.rename (2, "\xff\xfe"));
    CHECK (! bank.rename (4, "x"));
    std::string out;
    CHECK (VariationBank::normaliseName ("\tx ", out) && out == "x");
}

void testEquality()
{
    VariationBank a, b;
    CHECK (a == b);
    a.store (0, randomSnapshot (4));
    CHECK (! (a == b));
    b.store (0, randomSnapshot (4));
    CHECK (a == b);
    b.rename (1, "x");
    CHECK (! (a == b));
}

void testAbCompare()
{
    AbCompare ab;
    ParamSnapshot live = randomSnapshot (21);
    ParamSnapshot out;
    CHECK (! ab.engaged() && ! ab.toggle (live, out));

    ab.engage (live);
    CHECK (ab.engaged() && ab.activeSide() == AbCompare::Side::A);
    CHECK (soundParamsEqual (ab.snapshot (AbCompare::Side::A), live, 0.0f));
    CHECK (soundParamsEqual (ab.snapshot (AbCompare::Side::B), live, 0.0f));

    // edit on side A, then explicit copy A -> B, then edit B differently
    live[P::growl] = 12.0f;
    ab.copyToOther (live);
    CHECK (soundParamsEqual (ab.snapshot (AbCompare::Side::B), live, 0.0f));

    ab.captureLive (live);
    ParamSnapshot afterSwitch;
    // switch to B: B was a copy of A, so make B different first by toggling twice with edits
    CHECK (ab.toggle (live, afterSwitch));
    CHECK (ab.activeSide() == AbCompare::Side::B);
    afterSwitch[P::growl] = 88.0f;
    afterSwitch[P::filter_cutoff_hz] = 4000.0f;
    // switch back to A: A's stored snapshot returns exactly
    ParamSnapshot backToA;
    CHECK (ab.toggle (afterSwitch, backToA));
    CHECK (ab.activeSide() == AbCompare::Side::A);
    CHECK (soundParamsEqual (backToA, live, snapshotTolerance));
    CHECK (backToA[P::growl] == 12.0f);
    // and forward again to B
    ParamSnapshot toB;
    CHECK (ab.toggle (backToA, toB));
    CHECK (toB[P::growl] == 88.0f && toB[P::filter_cutoff_hz] == 4000.0f);

    // the held side's stored snapshot is not modified by switching (A is held now)
    const ParamSnapshot heldA = ab.snapshot (AbCompare::Side::A);
    for (int i = 0; i < 10; ++i)
    {
        ParamSnapshot tmp;
        CHECK (ab.toggle (toB, tmp));
        CHECK (ab.toggle (tmp, toB));
    }
    CHECK (soundParamsEqual (ab.snapshot (AbCompare::Side::A), heldA, 0.0f));
    CHECK (toB[P::growl] == 88.0f);
}

void testAbMatchedTrimAndUtility()
{
    AbCompare ab;
    ParamSnapshot a = randomSnapshot (31);
    a[P::output_trim_db] = -6.0f;
    a[P::bypass] = 1.0f;
    ab.engage (a);
    ParamSnapshot b = a;
    b[P::output_trim_db] = 3.0f;
    b[P::weight] = 20.0f;
    ab.copyToOther (b);      // other := b (trim +3)
    CHECK (ab.matchOutputTrim());

    ParamSnapshot out;
    CHECK (ab.toggle (a, out)); // switch to B
    CHECK (out[P::weight] == 20.0f);
    CHECK (out[P::output_trim_db] == -6.0f); // trim shared by default
    CHECK (out[P::bypass] == 1.0f);          // utility untouched
    CHECK (ab.snapshot (AbCompare::Side::B)[P::output_trim_db] == 3.0f); // stored snapshot not rewritten

    ab.setMatchOutputTrim (false);
    ParamSnapshot out2;
    CHECK (ab.toggle (out, out2)); // back to A with its own trim
    ParamSnapshot out3;
    CHECK (ab.toggle (out2, out3));
    CHECK (out3[P::output_trim_db] == -6.0f); // stored from the live state at the last toggle

    // copy other -> live
    {
        AbCompare ab2;
        const ParamSnapshot x = randomSnapshot (41);
        ParamSnapshot y = randomSnapshot (42);
        ab2.engage (x);
        ab2.copyToOther (y);                         // B := y (the live state counts as the active side)
        ab2.captureLive (x);                         // A := x again
        ParamSnapshot copied;
        CHECK (ab2.copyFromOther (x, copied));       // live := B
        CHECK (soundParamsEqual (copied, withSoundParams (x, y), 0.0f) || copied[P::output_trim_db] == x[P::output_trim_db]);
        for (int i = 0; i < parameterCount; ++i)
        {
            const auto idx = static_cast<std::size_t> (i);
            if (! parameterTable[idx].inPreset() || i == P::output_trim_db)
                continue;
            CHECK (copied.v[idx] == y.v[idx]);
        }
        CHECK (copied[P::output_trim_db] == x[P::output_trim_db]);
        CHECK (soundParamsEqual (ab2.snapshot (AbCompare::Side::A), copied, 0.0f));
    }

    // restore round trip
    AbCompare restored;
    restored.restore (ab.engaged(), ab.activeSide(), ab.snapshot (AbCompare::Side::A), ab.snapshot (AbCompare::Side::B),
                      ab.matchOutputTrim());
    CHECK (restored == ab);
    ab.disengage();
    CHECK (! ab.engaged() && ab.valid());
    ab.reset();
    CHECK (! ab.valid());
}
} // namespace

int main()
{
    testSlotsStoreAndRecall();
    testNames();
    testEquality();
    testAbCompare();
    testAbMatchedTrimAndUtility();
    std::cout << "VariationBankTests passed\n";
    return 0;
}
