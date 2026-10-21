// Random edit/gesture/transaction streams against UndoHistory: undoing everything restores the baseline exactly,
// redoing everything restores the final state exactly, the stack never exceeds its capacity, tracked_ stays valid.
#include "TestSupport.h"

#include "State/Pure/UndoHistory.h"

using namespace snaral;
using namespace snaral::testing;

namespace
{
P::Index randomSoundParam (Rng& rng)
{
    for (;;)
    {
        const auto i = static_cast<P::Index> (rng.below (parameterCount));
        if (parameterTable[static_cast<std::size_t> (i)].inPreset())
            return i;
    }
}

float randomValue (P::Index i, Rng& rng)
{
    const auto& d = parameterTable[static_cast<std::size_t> (i)];
    if (d.type == ParamType::Float)
        return d.clampToDomain (d.minimum + rng.unit() * (d.maximum - d.minimum));
    return d.clampToDomain (static_cast<float> (rng.below (d.type == ParamType::Bool ? 2 : d.numChoices)));
}

void run (std::uint64_t seed, std::size_t capacity)
{
    Rng rng (seed);
    UndoHistory::Config cfg;
    cfg.capacity = capacity;
    UndoHistory h (cfg);
    ParamSnapshot live = randomSnapshot (seed);
    h.reset (live);
    const ParamSnapshot baseline = live;
    std::int64_t now = 0;
    bool trimmed = false;
    std::vector<P::Index> open;

    for (int i = 0; i < 1500; ++i)
    {
        now += rng.below (900);
        const int r = rng.below (100);
        if (r < 45)
        {
            const auto p = randomSoundParam (rng);
            const float v = randomValue (p, rng);
            h.edit (p, v, now);
            if (UndoHistory::isTracked (p)) live[p] = v;
        }
        else if (r < 60)
        {
            const auto p = randomSoundParam (rng);
            h.beginGesture (p, now);
            open.push_back (p);
        }
        else if (r < 75 && ! open.empty())
        {
            h.endGesture (open.back(), now);
            open.pop_back();
        }
        else if (r < 82)
        {
            ParamSnapshot target = live;
            for (int k = 0; k < 6; ++k) { const auto p = randomSoundParam (rng); target[p] = randomValue (p, rng); }
            h.recordTransaction ("txn", target, now);
            live = withSoundParams (live, target);
        }
        else if (r < 90)
        {
            ParamSnapshot s = live;
            if (h.undo (s)) { live = s; }
        }
        else if (r < 96)
        {
            ParamSnapshot s = live;
            if (h.redo (s)) { live = s; }
        }
        else
        {
            h.sync (live);
        }
        CHECK (h.undoCount() <= capacity);
        CHECK (snapshotIsValid (h.tracked()));
        if (h.undoCount() == capacity) trimmed = true;
    }
    // close gestures, then compare
    while (! open.empty()) { h.endGesture (open.back(), now); open.pop_back(); }
    CHECK (soundParamsEqual (h.tracked(), live, 0.0f));

    int guard = 0;
    ParamSnapshot s = live;
    while (h.redo (s) && ++guard < 100000) {}      // steps undone during the run are still on the redo stack
    const ParamSnapshot finalState = s;
    guard = 0;
    while (h.undo (s) && ++guard < 100000) {}
    if (! trimmed)
        CHECK (soundParamsEqual (s, baseline, 0.0f));
    guard = 0;
    while (h.redo (s) && ++guard < 100000) {}
    if (! soundParamsEqual (s, finalState, 0.0f))
    {
        for (int i : differingSoundParams (s, finalState))
            std::fprintf (stderr, "seed %llu cap %zu: %s after redo-all %g, expected %g\n", static_cast<unsigned long long> (seed), capacity,
                          std::string (parameterTable[static_cast<std::size_t> (i)].id).c_str(), static_cast<double> (s.v[static_cast<std::size_t> (i)]), static_cast<double> (finalState.v[static_cast<std::size_t> (i)]));
        CHECK (false);
    }
}
} // namespace

int main()
{
    for (std::uint64_t seed = 1; seed <= 150; ++seed)
        run (seed, 100);
    for (std::uint64_t seed = 200; seed <= 260; ++seed)
        run (seed, 1000000);
    std::puts ("UndoFuzzTests passed");
    return 0;
}
