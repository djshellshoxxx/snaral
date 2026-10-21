#pragma once

// Four named instance-level variation slots (A-D) and the A/B compare holder. JUCE-free model classes;
// the ValueTree codec lives in State/ProjectState.h. Message thread only.
//
// A slot stores the complete value set of every SOUND parameter (inPreset()). Recall never touches
// utility parameters (bypass, mutes, layer solo). Values are stored as raw floats, so store -> recall is
// bit-exact (the 1e-6 acceptance tolerance is trivially met) and survives project save/reopen because the
// codec writes shortest round-trip text.

#include <array>
#include <string>

#include "SnapshotUtil.h"

namespace snaral
{
inline constexpr int variationSlotCount = 4;
inline constexpr std::size_t maxVariationNameChars = 32;

struct VariationSlot
{
    std::string name;
    bool empty = true;
    ParamSnapshot snapshot; // meaningful only when !empty
};

class VariationBank
{
public:
    VariationBank();

    const VariationSlot& slot (int index) const;
    bool isEmpty (int index) const;

    /// Explicit save into one slot (all other slots untouched). Does not change the active slot.
    bool store (int index, const ParamSnapshot& current);

    /// Returns `current` with the slot's sound parameters applied (utility params kept), or false if the
    /// slot is empty/out of range. On success the slot becomes the active one. `out` is only written on success.
    bool recall (int index, const ParamSnapshot& current, ParamSnapshot& out);

    bool clear (int index); // slot becomes empty; name is kept

    /// Renames a slot. Names are trimmed, limited to maxVariationNameChars code points and may not contain
    /// control characters; an empty name resets to the letter ("A".."D"). Returns false if rejected.
    bool rename (int index, const std::string& name);

    int activeIndex() const noexcept { return active_; }
    bool setActiveIndex (int index) noexcept;

    /// Resets to four empty slots named A-D, active = 0.
    void reset();

    static std::string defaultName (int index);
    /// Validates/normalises a user-supplied name; false if it contains control chars or is too long.
    static bool normaliseName (const std::string& in, std::string& out);

    bool operator== (const VariationBank& other) const;

private:
    std::array<VariationSlot, variationSlotCount> slots_;
    int active_ = 0;
};

/// A/B compare. The live patch is always the active side; the other side is held. Switching stores the live
/// state into the active side first, so both sides are kept, then loads the other. The output trim is shared
/// by both sides by default so loudness differences do not bias the comparison.
class AbCompare
{
public:
    enum class Side { A = 0, B = 1 };

    /// Starts comparing: both sides initialised from `current` (active = A). Idempotent while engaged.
    void engage (const ParamSnapshot& current);
    /// Leaves compare mode but keeps both snapshots (re-engage with engage() to continue from the active side).
    void disengage() noexcept { engaged_ = false; }
    bool engaged() const noexcept { return engaged_; }

    Side activeSide() const noexcept { return active_; }
    bool valid() const noexcept { return engaged_ || haveBoth_; }
    const ParamSnapshot& snapshot (Side side) const noexcept { return snaps_[static_cast<std::size_t> (side)]; }

    /// Stores the live state in the active side and returns the other side as the new live state.
    /// Only sound parameters are swapped; with matchOutputTrim() the live output trim is kept.
    /// Returns false (out untouched) if not engaged. Never modifies the held side's stored snapshot.
    bool toggle (const ParamSnapshot& current, ParamSnapshot& out);

    /// Explicit "copy A to B" / "copy B to A". The live state counts as the active side's content.
    void copyToOther (const ParamSnapshot& current); // active -> other
    /// Replaces the live state by the other side's snapshot and stores it as the active side ("copy B to A" when A is live).
    bool copyFromOther (const ParamSnapshot& current, ParamSnapshot& out);

    /// Stores the live state into the active side (call before saving the project).
    void captureLive (const ParamSnapshot& current);

    void setMatchOutputTrim (bool on) noexcept { matchTrim_ = on; }
    bool matchOutputTrim() const noexcept { return matchTrim_; }

    void reset();

    /// Restores a persisted state (used by the project codec).
    void restore (bool engaged, Side active, const ParamSnapshot& a, const ParamSnapshot& b, bool matchTrim);

    bool operator== (const AbCompare& other) const;

private:
    bool engaged_ = false;
    bool haveBoth_ = false;
    bool matchTrim_ = true;
    Side active_ = Side::A;
    std::array<ParamSnapshot, 2> snaps_ {};
};
} // namespace snaral
