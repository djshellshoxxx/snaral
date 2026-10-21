#pragma once

// Bounded undo/redo over parameter deltas (docs/specs/expansion-undo-ab.md). JUCE-free.
//
// Threading: message thread only. The audio thread never sees this class; undo()/redo() return a new
// ParamSnapshot that the caller publishes through the normal atomic parameter path.
// Time is injected (milliseconds, any monotonic origin) so behaviour is deterministic in tests.

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "SnapshotUtil.h"

namespace snaral
{
class UndoHistory
{
public:
    using TimeMs = std::int64_t;

    struct Config
    {
        std::size_t capacity = 100;     // most recent user-initiated transactions kept
        TimeMs coalesceWindowMs = 400;  // same-parameter edits without a gesture merge inside this window
    };

    struct Change
    {
        P::Index param;
        float before;
        float after;
    };

    struct Step
    {
        std::string label;
        std::vector<Change> changes;
        TimeMs time = 0;
        bool sealed = true; // sealed steps never absorb later edits
    };

    UndoHistory() : UndoHistory (Config {}) {}
    explicit UndoHistory (const Config& config);

    /// Explicit initialize / new instance: clears both stacks and sets the tracked baseline.
    void reset (const ParamSnapshot& baseline);

    /// Informs the history of a change that must NOT become an undo step (host automation, A/B switch,
    /// project restore). Parameters with an open gesture are skipped.
    void sync (const ParamSnapshot& current);

    // ---- continuous gestures (knob/slider drags) ----
    /// Marks the start of a drag; the whole gesture until endGesture() becomes at most one step.
    void beginGesture (P::Index param, TimeMs now);
    void endGesture (P::Index param, TimeMs now);
    bool gestureOpen (P::Index param) const noexcept;

    // ---- edits ----
    /// A user edit. Equal-to-tracked values are ignored (this also makes listener echoes of undo/redo harmless).
    /// Outside a gesture, consecutive edits of the same parameter within coalesceWindowMs merge into one step.
    void edit (P::Index param, float newValue, TimeMs now);

    /// One undo step for a whole-state change (preset load, initialize, variation recall). Only sound
    /// parameters that actually differ are recorded; an identical state records nothing.
    void recordTransaction (const std::string& label, const ParamSnapshot& newState, TimeMs now);

    // ---- undo / redo ----
    bool canUndo() const noexcept;
    bool canRedo() const noexcept { return ! redoStack_.empty(); }
    std::size_t undoCount() const noexcept { return undoStack_.size(); }
    std::size_t redoCount() const noexcept { return redoStack_.size(); }
    const std::string& undoLabel() const noexcept;
    const std::string& redoLabel() const noexcept;

    /// Applies the newest step in reverse to `state` (only the parameters it changed). Returns false if nothing to undo.
    bool undo (ParamSnapshot& state);
    bool redo (ParamSnapshot& state);

    const ParamSnapshot& tracked() const noexcept { return tracked_; }
    const Step* peekUndo() const noexcept { return undoStack_.empty() ? nullptr : &undoStack_.back(); }

    /// Increments whenever the visible history changes (for UI refresh).
    std::uint64_t revision() const noexcept { return revision_; }

    /// Parameters tracked by the history (sound parameters only).
    static bool isTracked (P::Index param) noexcept;

private:
    struct OpenGesture
    {
        P::Index param;
        float before;
        float after;
        bool changed;
        TimeMs start;
    };

    void pushStep (Step&& step);
    void commitGesture (std::size_t index, TimeMs now);
    void commitAllGestures (TimeMs now);
    void sealLast() noexcept;

    Config config_;
    ParamSnapshot tracked_;
    std::deque<Step> undoStack_;
    std::deque<Step> redoStack_;
    std::vector<OpenGesture> gestures_;
    std::uint64_t revision_ = 0;
};
} // namespace snaral
