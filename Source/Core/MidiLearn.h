#pragma once

// MIDI CC learn: mapping model, learn state machine, conflict rules, text persistence, and the
// allocation-free audio-thread lookup. JUCE-free. Spec: docs/specs/expansion-midi-cc-learn.md.
//
// Threading
//   message thread : MidiLearnModel (edit mappings, learn flow), MidiLearnTable::build, TablePublisher::publish
//   audio thread   : TablePublisher::current(), MidiLearnTable::find, MidiLearnRuntime::apply,
//                    LearnCapture::offer. None of these allocate, lock or throw.
//
// Persistence: MidiMapData <-> text (toText / fromText). Store the text as ONE string property (e.g. "map")
// of the project-state MIDILEARN child, or build MAP children from MidiMapData::mappings (target id string,
// cc, channel, min, max, invert). Never part of presets, variations or A/B.

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "Parameters/ParameterTable.h"

namespace snaral::midi
{
inline constexpr int maxMappings = 16;
inline constexpr int channelSlots = 17; // 0 = omni, 1..16

/// True for CCs that always keep their MIDI meaning (0,32 bank; 6,38 data entry; 64 sustain; 98-101 NRPN/RPN;
/// 120-127 channel mode). Out-of-range numbers are also "reserved" (unusable).
bool isReservedCc (int cc) noexcept;

/// Float or Bool, automatable, not integer, not in the exclusion list (spec "Learnable parameters").
bool isLearnable (int paramIndex) noexcept;
std::vector<int> learnableParameters();

struct MidiMapping
{
    int target = -1;          // parameter index (P::Index)
    int cc = 0;               // 0..127 (never reserved)
    int channel = 0;          // 0 = omni, 1..16
    float min = 0.0f;         // normalised [0,1], min <= max
    float max = 1.0f;
    bool invert = false;

    bool operator== (const MidiMapping&) const = default;
};

/// Does a mapping on channel `a` overlap one on channel `b` (same cc)? Omni overlaps everything.
constexpr bool channelsOverlap (int a, int b) noexcept { return a == 0 || b == 0 || a == b; }

/// Reasons a mapping list is invalid (also used by text parsing and the state loader).
/// Empty string = valid.
std::string validateMapping (const MidiMapping& m);
std::string validateMappings (const std::vector<MidiMapping>& list);

/// CC value 0..127 -> normalised target in [min,max] (spec "Value conversion"). Exact at both ends.
float convertCc (const MidiMapping& m, int value) noexcept;

// ---------------------------------------------------------------- persistence

struct MidiMapData
{
    std::vector<MidiMapping> mappings;
};

/// Line based text, version 1:
///   snaral-midimap 1
///   map target=<param id> cc=<0..127> ch=<0..16> min=<f> max=<f> invert=<0|1>
std::string toText (const MidiMapData& data);

struct ParseResult
{
    bool ok = false;
    std::string error;  // first problem, with line number
    MidiMapData data;   // empty unless ok
};
/// Strict: a malformed map (bad header, unknown target, non-learnable target, reserved/out-of-range cc,
/// min>max, duplicate target, overlapping (cc,channel), more than 16 entries) is rejected whole.
ParseResult fromText (std::string_view text);

// ---------------------------------------------------------------- audio-thread lookup

/// Immutable 128 x 17 lookup. Built on the message thread, read on the audio thread.
class MidiLearnTable
{
public:
    static std::unique_ptr<MidiLearnTable> build (const std::vector<MidiMapping>& list); // list must be valid; invalid entries are skipped

    /// Slot (0..15) of the mapping for a message on `channel1to16`, or -1. Omni and explicit are both honoured.
    int find (int cc, int channel1to16) const noexcept
    {
        if (cc < 0 || cc > 127 || channel1to16 < 1 || channel1to16 > 16)
            return -1;
        const int explicitSlot = slotOf_[static_cast<std::size_t> (cc * channelSlots + channel1to16)];
        return explicitSlot;
    }
    int count() const noexcept { return count_; }
    const MidiMapping& mapping (int slot) const noexcept { return mappings_[static_cast<std::size_t> (slot)]; }

private:
    std::array<std::int8_t, 128 * channelSlots> slotOf_ {}; // omni already expanded into channels 1..16
    std::array<MidiMapping, maxMappings> mappings_ {};
    int count_ = 0;
};

/// Message-thread owner of the published table. The audio thread calls current(); old tables are kept alive
/// for `retired` further publishes so a block that is mid-read is never freed (publishes are UI-rate events,
/// far slower than a block; the audio thread must not hold a pointer across blocks).
class TablePublisher
{
public:
    TablePublisher();
    void publish (const std::vector<MidiMapping>& list); // message thread
    const MidiLearnTable* current() const noexcept { return current_.load (std::memory_order_acquire); }

private:
    static constexpr int retired = 8;
    std::array<std::unique_ptr<MidiLearnTable>, retired + 1> ring_ {};
    int next_ = 0;
    std::atomic<const MidiLearnTable*> current_ { nullptr };
};

/// Result of one CC message on the audio thread.
struct CcApply
{
    bool mapped = false;        // a mapping exists for this message (the message is consumed for parameter purposes)
    bool apply = false;         // value should be applied (false while waiting for pickup)
    int target = -1;            // parameter index
    float normalised = 0.0f;    // target value in [0,1] (valid when mapped)
    float domain = 0.0f;        // fromNormalized(normalised): ParamSnapshot units (Bool: 0/1)
};

/// Per-instance audio-thread state: last CC-derived value and pickup tracking per slot. Fixed size.
class MidiLearnRuntime
{
public:
    enum class Mode { Jump, Pickup };

    /// Jump (default, spec): the CC value is applied immediately; the destination smoothing prevents zipper.
    /// Pickup: a CC is ignored until it reaches (or crosses) the parameter's current normalised value, so
    /// hardware knobs cannot make the parameter jump. Bool targets always jump.
    void setMode (Mode m) noexcept { mode_ = m; }
    Mode mode() const noexcept { return mode_; }

    /// Tell the runtime the parameter was changed by UI/automation/preset (not by this class). Pickup re-arms.
    void notifyExternalChange (int slot, float normalised) noexcept;
    void resetAll() noexcept; // after the table is replaced or a preset is loaded

    CcApply apply (const MidiLearnTable& table, int cc, int channel1to16, int value) noexcept;

private:
    struct SlotState
    {
        float current = 0.0f;   // parameter's normalised value as last known
        float lastCc = -1.0f;   // previous CC-derived normalised value (-1 = none)
        bool currentKnown = false;
        bool engaged = false;
    };
    std::array<SlotState, maxMappings> slots_ {};
    Mode mode_ = Mode::Jump;
};

/// Lock-free hand-off of "first eligible CC" from the audio thread to the message thread.
class LearnCapture
{
public:
    struct Event { int cc = 0; int channel0to15 = 0; };

    void arm() noexcept { event_.store (0, std::memory_order_relaxed); reserved_.store (false, std::memory_order_relaxed); armed_.store (true, std::memory_order_release); }
    void disarm() noexcept { armed_.store (false, std::memory_order_release); event_.store (0, std::memory_order_relaxed); }
    bool armed() const noexcept { return armed_.load (std::memory_order_acquire); }

    /// Audio thread, for every CC message (channel0to15 as delivered by MIDI).
    void offer (int cc, int channel0to15) noexcept;

    /// Message thread: first eligible CC since arm(), consumed once. Disarms when an event is returned.
    bool poll (Event& out) noexcept;
    /// Message thread: true once if a reserved CC was seen while armed (status "Reserved MIDI message").
    bool pollReservedSeen() noexcept { return reserved_.exchange (false, std::memory_order_acq_rel); }

private:
    std::atomic<bool> armed_ { false };
    std::atomic<std::uint32_t> event_ { 0 };   // bit 12 valid, bits 7-10 channel, bits 0-6 cc
    std::atomic<bool> reserved_ { false };
};

// ---------------------------------------------------------------- message-thread model + learn flow

class MidiLearnModel
{
public:
    enum class ArmResult { Armed, NotLearnable };
    enum class CaptureKind
    {
        NotArmed,        // capture ignored
        Reserved,        // reserved CC: ignored, still armed ("Reserved MIDI message")
        Assigned,        // mapping created (see `assigned`)
        NeedsChoice,     // conflict: call resolve()
        LimitReached     // 16 mappings and target unmapped: refused, learn ends
    };
    enum class Choice { Replace, KeepBoth, Cancel };

    struct Conflict
    {
        bool targetAlreadyMapped = false;   // learning offers Replace or Cancel for this
        std::vector<MidiMapping> ccHolders; // existing mappings on the same cc (any channel)
        bool keepBothAvailable = false;     // see resolve()
        int suggestedExistingChannel = 0;   // when the sole holder is omni: a free channel to pin it to (1..16)
        std::string keepBothUnavailableReason;
    };

    struct CaptureResult
    {
        CaptureKind kind = CaptureKind::NotArmed;
        MidiMapping assigned;     // valid for Assigned
        Conflict conflict;        // valid for NeedsChoice
        std::string status;       // user-facing status line, e.g. "CC 74 ch 1 mapped to Bite"
    };

    MidiLearnModel() = default;
    MidiLearnModel (const MidiLearnModel&) = delete;
    MidiLearnModel& operator= (const MidiLearnModel&) = delete;

    // learn state machine
    ArmResult arm (int paramIndex);       // starts (or switches) learn; a previous pending conflict is cancelled
    void cancel();                        // Esc / click elsewhere: no changes
    bool isArmed() const noexcept { return armed_; }
    bool hasPendingChoice() const noexcept { return pending_; }
    int armedTarget() const noexcept { return armedTarget_; }
    LearnCapture& capture() noexcept { return capture_; } // audio thread offers CCs here

    /// Message thread: pull a captured event from the audio thread and feed onCcCaptured.
    bool pollCapture (CaptureResult& out);

    /// Capture of (cc, channel 0..15). New mappings are assigned min 0, max 1, omni, not inverted.
    CaptureResult onCcCaptured (int cc, int channel0to15);

    /// Resolve a NeedsChoice. Replace: removes every conflicting mapping (cc holders and the target's old one)
    /// and assigns. KeepBoth: only when keepBothAvailable; the new mapping takes the captured explicit channel,
    /// an omni holder is pinned to `existingChannel` (must be 1..16 and differ from the captured channel;
    /// 0 = use suggestedExistingChannel); the target's own old mapping (if any) is still replaced.
    /// Cancel: no change. Returns the status line; leaves learn mode.
    std::string resolve (Choice choice, int existingChannel = 0);

    // editing
    const std::vector<MidiMapping>& mappings() const noexcept { return mappings_; }
    int indexOfTarget (int paramIndex) const noexcept;
    bool remove (int paramIndex);        // "Unlink mapping"
    void clearAll();
    /// Edit min/max/invert/channel of the mapping for `paramIndex`; rejected (false, unchanged) if invalid or conflicting.
    bool update (const MidiMapping& edited, std::string* error = nullptr);

    MidiMapData data() const { return { mappings_ }; }
    /// Replaces all mappings; rejects (false) invalid data and leaves the model unchanged.
    bool load (const MidiMapData& data, std::string* error = nullptr);

    /// Bumped on every change so callers know to republish the table / persist.
    std::uint32_t revision() const noexcept { return revision_; }

private:
    std::vector<MidiMapping> mappings_;
    LearnCapture capture_;
    bool armed_ = false;
    int armedTarget_ = -1;
    bool pending_ = false;
    int pendingCc_ = 0, pendingChannel_ = 0; // channel 0..15 as captured
    Conflict pendingConflict_;
    std::uint32_t revision_ = 0;
};

std::string describeMapping (const MidiMapping& m);   // "CC 74 ch 1 -> Bite"
} // namespace snaral::midi
