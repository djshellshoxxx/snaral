#include "TestSupport.h"

#include <cmath>
#include <iostream>
#include <vector>

#include "State/Pure/UndoHistory.h"

using namespace snaral;
using namespace snaral::testing;

namespace
{
void testHundredStepsReverseOrder()
{
    UndoHistory h;
    ParamSnapshot state;
    h.reset (state);

    std::vector<ParamSnapshot> states { state };
    Rng rng (11);
    for (int i = 0; i < 100; ++i)
    {
        const auto param = static_cast<P::Index> (i % 20); // macros + voice + sub parameters
        const auto& def = parameterTable[static_cast<std::size_t> (param)];
        float v = def.clampToDomain (def.minimum + rng.unit() * (def.maximum - def.minimum));
        if (v == state[param])
            v = def.clampToDomain (state[param] + 1.0f > def.maximum ? state[param] - 1.0f : state[param] + 1.0f);
        state[param] = v;
        h.edit (param, v, static_cast<UndoHistory::TimeMs> (i) * 1000); // far apart: never coalesced
        states.push_back (state);
    }
    CHECK (h.undoCount() == 100 && ! h.canRedo());

    for (int i = 100; i >= 1; --i)
    {
        CHECK (snapshotsIdentical (state, states[static_cast<std::size_t> (i)]));
        CHECK (h.undo (state));
        CHECK (snapshotsIdentical (state, states[static_cast<std::size_t> (i - 1)]));
    }
    CHECK (! h.canUndo() && ! h.undo (state));
    CHECK (h.redoCount() == 100);
    for (int i = 1; i <= 100; ++i)
    {
        CHECK (h.redo (state));
        CHECK (snapshotsIdentical (state, states[static_cast<std::size_t> (i)]));
    }
    CHECK (! h.canRedo() && ! h.redo (state));
}

void testCapacityIsBounded()
{
    UndoHistory h;
    ParamSnapshot state;
    h.reset (state);
    std::vector<ParamSnapshot> states { state };
    for (int i = 0; i < 130; ++i)
    {
        state[P::growl] = static_cast<float> (i % 100) + 0.5f + (i >= 100 ? 0.25f : 0.0f);
        h.edit (P::growl, state[P::growl], static_cast<UndoHistory::TimeMs> (i) * 1000);
        states.push_back (state);
    }
    CHECK (h.undoCount() == 100);
    int undone = 0;
    while (h.undo (state))
        ++undone;
    CHECK (undone == 100);
    CHECK (snapshotsIdentical (state, states[30])); // the oldest 30 steps fell off the front
}

void testGestureIsOneStep()
{
    UndoHistory h;
    ParamSnapshot state;
    h.reset (state);
    const float start = state[P::filter_cutoff_hz];

    h.beginGesture (P::filter_cutoff_hz, 0);
    CHECK (h.gestureOpen (P::filter_cutoff_hz) && h.undoCount() == 0);
    for (int i = 1; i <= 200; ++i)
    {
        state[P::filter_cutoff_hz] = start + static_cast<float> (i);
        h.edit (P::filter_cutoff_hz, state[P::filter_cutoff_hz], i * 50); // slower than the debounce window on purpose
        CHECK (h.undoCount() == 0);                                       // nothing is committed mid-gesture
    }
    h.endGesture (P::filter_cutoff_hz, 10000);
    CHECK (h.undoCount() == 1 && ! h.gestureOpen (P::filter_cutoff_hz));
    CHECK (h.undoLabel() == "Edit Cutoff");

    CHECK (h.undo (state));
    CHECK (state[P::filter_cutoff_hz] == start);
    CHECK (h.redo (state));
    CHECK (state[P::filter_cutoff_hz] == start + 200.0f);

    // a drag that returns to its starting value is not an edit
    UndoHistory h2;
    ParamSnapshot s2;
    h2.reset (s2);
    h2.beginGesture (P::bite, 0);
    h2.edit (P::bite, 70.0f, 10);
    h2.edit (P::bite, s2[P::bite], 20);
    h2.endGesture (P::bite, 30);
    CHECK (h2.undoCount() == 0 && ! h2.canUndo());

    // gesture followed by another gesture on the same parameter: two steps, never merged
    h2.beginGesture (P::bite, 100);
    h2.edit (P::bite, 30.0f, 110);
    h2.endGesture (P::bite, 120);
    h2.beginGesture (P::bite, 130);
    h2.edit (P::bite, 40.0f, 140);
    h2.endGesture (P::bite, 150);
    CHECK (h2.undoCount() == 2);

    // two parameters dragged at the same time
    UndoHistory h3;
    ParamSnapshot s3;
    h3.reset (s3);
    h3.beginGesture (P::weight, 0);
    h3.beginGesture (P::growl, 0);
    h3.edit (P::weight, 10.0f, 5);
    h3.edit (P::growl, 20.0f, 6);
    h3.endGesture (P::growl, 7);
    h3.endGesture (P::weight, 8);
    CHECK (h3.undoCount() == 2);

    // undo while a drag is still open commits the drag first (and undoes it)
    UndoHistory h4;
    ParamSnapshot s4;
    h4.reset (s4);
    h4.beginGesture (P::space, 0);
    h4.edit (P::space, 55.0f, 1);
    s4[P::space] = 55.0f;
    CHECK (h4.canUndo());
    CHECK (h4.undo (s4) && s4[P::space] == parameterTable[P::space].defaultValue && ! h4.gestureOpen (P::space));
}

void testSameParameterDebounce()
{
    UndoHistory h;
    ParamSnapshot state;
    h.reset (state);
    const float start = state[P::drive_amount];

    // wheel / arrow-key style: several quick edits of one parameter -> one step
    for (int i = 0; i < 6; ++i)
        h.edit (P::drive_amount, start + 1.0f + static_cast<float> (i), i * 100);
    CHECK (h.undoCount() == 1);
    state[P::drive_amount] = start + 6.0f;
    CHECK (h.undo (state) && state[P::drive_amount] == start);

    // a pause longer than the window starts a new step
    UndoHistory h2;
    h2.reset (ParamSnapshot {});
    h2.edit (P::drive_amount, 30.0f, 0);
    h2.edit (P::drive_amount, 31.0f, 399);
    h2.edit (P::drive_amount, 32.0f, 900); // 501 ms after the previous edit
    CHECK (h2.undoCount() == 2);

    // a different parameter in between breaks the run
    UndoHistory h3;
    h3.reset (ParamSnapshot {});
    h3.edit (P::drive_amount, 30.0f, 0);
    h3.edit (P::drive_tone, 60.0f, 10);
    h3.edit (P::drive_amount, 31.0f, 20);
    CHECK (h3.undoCount() == 3);

    // dragging back to the original value inside the window leaves no step
    UndoHistory h4;
    h4.reset (ParamSnapshot {});
    h4.edit (P::noise_level, 40.0f, 0);
    h4.edit (P::noise_level, 0.0f, 100);
    CHECK (h4.undoCount() == 0);

    // equal value / echo of an applied undo is ignored
    UndoHistory h5;
    ParamSnapshot s5;
    h5.reset (s5);
    h5.edit (P::bite, 50.0f, 0);
    s5[P::bite] = 50.0f;
    CHECK (h5.undo (s5));
    const auto rev = h5.revision();
    h5.edit (P::bite, s5[P::bite], 10); // the UI listener reports the value undo just wrote
    CHECK (h5.redoCount() == 1 && h5.revision() == rev);

    // custom window
    UndoHistory::Config cfg;
    cfg.coalesceWindowMs = 0;
    UndoHistory h6 (cfg);
    h6.reset (ParamSnapshot {});
    h6.edit (P::bite, 30.0f, 0);
    h6.edit (P::bite, 31.0f, 1);
    CHECK (h6.undoCount() == 2);
}

void testTransactions()
{
    UndoHistory h;
    ParamSnapshot state;
    h.reset (state);
    state[P::bypass] = 1.0f; // utility state: never part of history
    h.sync (state);

    // a preset load touches many parameters but is ONE step
    ParamSnapshot loaded = randomSnapshot (3);
    loaded[P::bypass] = 0.0f;
    h.recordTransaction ("Load Preset", loaded, 1000);
    CHECK (h.undoCount() == 1 && h.undoLabel() == "Load Preset");
    const ParamSnapshot afterLoad = withSoundParams (state, loaded);

    // edits after the load stay separate steps
    h.edit (P::weight, 12.0f, 2000);
    CHECK (h.undoCount() == 2);

    ParamSnapshot live = afterLoad;
    live[P::weight] = 12.0f;
    CHECK (h.undo (live));
    CHECK (snapshotsIdentical (live, afterLoad));
    CHECK (h.undo (live));
    CHECK (soundParamsEqual (live, ParamSnapshot {}, 0.0f));
    CHECK (live[P::bypass] == afterLoad[P::bypass]); // utility parameter untouched by undo
    CHECK (h.redo (live) && soundParamsEqual (live, loaded, 0.0f));

    // loading an identical state records nothing; utility-only differences are not tracked either
    UndoHistory h2;
    h2.reset (loaded);
    h2.recordTransaction ("Load Preset", loaded, 0);
    ParamSnapshot onlyUtility = loaded;
    onlyUtility[P::mid_mute] = 1.0f;
    h2.recordTransaction ("Mute", onlyUtility, 1);
    CHECK (h2.undoCount() == 0);

    // a transaction seals a pending quick edit: later edits do not merge into the load
    UndoHistory h3;
    h3.reset (ParamSnapshot {});
    h3.edit (P::bite, 33.0f, 0);
    ParamSnapshot target;
    target[P::bite] = 33.0f;
    target[P::growl] = 10.0f;
    h3.recordTransaction ("Recall Variation B", target, 10);
    h3.edit (P::growl, 11.0f, 20);
    CHECK (h3.undoCount() == 3);
}

void testHostAutomationDoesNotPollute()
{
    UndoHistory h;
    ParamSnapshot state;
    h.reset (state);

    // host automation moves cutoff; the history is told via sync() and records nothing
    for (int i = 0; i < 50; ++i)
    {
        state[P::filter_cutoff_hz] = 300.0f + static_cast<float> (i) * 10.0f;
        h.sync (state);
    }
    CHECK (h.undoCount() == 0 && ! h.canUndo());

    // a user edit afterwards undoes to the AUTOMATED value, not to a stale one
    h.edit (P::filter_resonance, 44.0f, 100);
    state[P::filter_resonance] = 44.0f;
    state[P::filter_cutoff_hz] = 900.0f; // automation continues
    h.sync (state);
    CHECK (h.undo (state));
    CHECK (state[P::filter_resonance] == parameterTable[P::filter_resonance].defaultValue);
    CHECK (state[P::filter_cutoff_hz] == 900.0f); // the delta only touches the edited parameter

    // sync does not clear redo, but new edits do
    CHECK (h.redoCount() == 1);
    h.edit (P::bite, 5.0f, 200);
    CHECK (h.redoCount() == 0);
}

void testExclusionsAndSanitising()
{
    UndoHistory h;
    h.reset (ParamSnapshot {});
    CHECK (! UndoHistory::isTracked (P::bypass) && ! UndoHistory::isTracked (P::layer_solo));
    CHECK (! UndoHistory::isTracked (P::sub_mute) && ! UndoHistory::isTracked (P::mid_mute));
    CHECK (UndoHistory::isTracked (P::weight) && UndoHistory::isTracked (P::traj_y3));
    CHECK (! UndoHistory::isTracked (static_cast<P::Index> (-1)) && ! UndoHistory::isTracked (P::count));

    h.edit (P::bypass, 1.0f, 0);
    h.beginGesture (P::mid_mute, 0);
    h.edit (P::mid_mute, 1.0f, 1);
    h.endGesture (P::mid_mute, 2);
    CHECK (h.undoCount() == 0);
    CHECK (h.tracked()[P::bypass] == 1.0f); // mirrored, but not undoable

    h.edit (P::weight, std::nanf (""), 0);
    h.edit (P::weight, INFINITY, 0);
    CHECK (h.undoCount() == 0);
    h.edit (P::weight, 500.0f, 10); // clamped into the registry range
    CHECK (h.tracked()[P::weight] == 100.0f && h.undoCount() == 1);
    h.edit (static_cast<P::Index> (9999), 1.0f, 20);
    CHECK (h.undoCount() == 1);

    // integer parameters quantise
    h.edit (P::transpose_semitones, 3.4f, 5000);
    CHECK (h.tracked()[P::transpose_semitones] == 3.0f);
}

void testRedoClearedAndRevision()
{
    UndoHistory h;
    ParamSnapshot s;
    h.reset (s);
    const auto r0 = h.revision();
    h.edit (P::space, 20.0f, 0);
    CHECK (h.revision() > r0);
    s[P::space] = 20.0f;
    CHECK (h.undo (s) && h.canRedo());
    h.edit (P::space, 25.0f, 5000);
    CHECK (! h.canRedo());
    CHECK (h.redoLabel().empty());
    h.reset (s);
    CHECK (h.undoCount() == 0 && h.redoCount() == 0 && h.undoLabel().empty());
}

void testDeterministicScript()
{
    auto run = []
    {
        UndoHistory h;
        ParamSnapshot s;
        h.reset (s);
        Rng rng (99);
        for (int i = 0; i < 400; ++i)
        {
            const auto param = static_cast<P::Index> (rng.below (30));
            const auto& def = parameterTable[static_cast<std::size_t> (param)];
            const float v = def.clampToDomain (def.minimum + rng.unit() * (def.maximum - def.minimum));
            switch (rng.below (5))
            {
                case 0: h.beginGesture (param, i * 30); break;
                case 1: h.endGesture (param, i * 30); break;
                case 2: if (h.canUndo()) h.undo (s); break;
                case 3: if (h.canRedo()) h.redo (s); break;
                default: s[param] = v; h.edit (param, v, i * 30); break;
            }
        }
        return std::make_pair (s, h.undoCount() * 1000 + h.redoCount());
    };
    const auto a = run();
    const auto b = run();
    CHECK (snapshotsIdentical (a.first, b.first) && a.second == b.second);
}
} // namespace

int main()
{
    testHundredStepsReverseOrder();
    testCapacityIsBounded();
    testGestureIsOneStep();
    testSameParameterDebounce();
    testTransactions();
    testHostAutomationDoesNotPollute();
    testExclusionsAndSanitising();
    testRedoClearedAndRevision();
    testDeterministicScript();
    std::cout << "UndoHistoryTests passed\n";
    return 0;
}
