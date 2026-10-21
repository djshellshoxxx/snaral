#pragma once

// Host project-state codec (getStateInformation / setStateInformation), schema v2.
// JUCE-dependent (ValueTree + AudioProcessor binary XML) but contains NO processor wiring: every function is
// pure with respect to the live plug-in, so a failed decode cannot disturb the running state. Normative
// description: docs/specs/parameter-state.md ("Host state schema v2").
//
// Integration recipe (done by the engine-integration worker, PluginProcessor is not edited here):
//
//   getStateInformation:
//       project::ProjectModel m;
//       m.params = project::snapshotFromApvts (parameters_);           // every registry parameter, domain units
//       m.variations = variations_;  m.ab = ab_;                        // message-thread objects
//       m.presetName = ...; m.presetId = ...; m.presetModified = ...;
//       m.midiLearn = midiLearnTree_;  m.ui = uiTree_;                  // opaque children owned by their features
//       { std::lock_guard lock (stateMutex_);  project::encode (m, destination); }
//
//   setStateInformation:
//       if (size <= 0 || size > project::maxStateBytes) return;        // cheap pre-check
//       auto r = project::decode (data, size);                         // parses + validates + migrates; touches nothing
//       if (! r.ok) return;                                            // current state stays byte-for-byte intact
//       std::lock_guard lock (stateMutex_);
//       { RestoreSequenceGuard guard (stateRestoreSequence_);          // audio keeps its previous coherent snapshot
//         parameters_.replaceState (project::toValueTree (r.model)); } // ONE atomic publish of every parameter
//       variations_ = r.model.variations; ab_ = r.model.ab; ...        // message-thread state, after the publish
//       undo_.reset (r.model.params);                                  // project restore is not an undoable edit

#include <string>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "Pure/PresetFormat.h"
#include "Pure/VariationBank.h"

namespace snaral::project
{
inline constexpr int currentSchemaVersion = 2;
/// Upper bound for a state blob. v1 allowed 64 KiB; v2 holds 4 variation slots, A/B, MIDI-learn and UI data
/// (typically ~35 KiB), so the cap is 1 MiB, still tiny but safe against hostile/garbage input.
inline constexpr int maxStateBytes = 1024 * 1024;
inline constexpr std::size_t maxOpaqueChildBytes = 256 * 1024; // UI / MIDILEARN subtree (serialised XML length)
inline constexpr int maxOpaqueDepth = 8;
inline constexpr int maxOpaqueNodes = 2048;
inline constexpr std::size_t maxPresetNameChars = 128;

// Tree vocabulary
inline const juce::Identifier rootType { "SnaralState" };
inline const juce::Identifier schemaVersionProperty { "schemaVersion" };
inline const juce::Identifier pluginVersionProperty { "pluginVersion" };
inline const juce::Identifier paramType { "PARAM" };
inline const juce::Identifier presetType { "PRESET" };
inline const juce::Identifier variationsType { "VARIATIONS" };
inline const juce::Identifier slotType { "SLOT" };
inline const juce::Identifier valuesType { "VALUES" };
inline const juce::Identifier abType { "ABCOMPARE" };
inline const juce::Identifier abSideType { "SIDE" };
inline const juce::Identifier midiLearnType { "MIDILEARN" };
inline const juce::Identifier uiType { "UI" };

/// Everything a project state holds, as plain data. `params` covers every registry parameter (sound + utility).
struct ProjectModel
{
    ParamSnapshot params;
    std::string presetName;         // empty = Init
    std::string presetId;           // library id of the loaded preset (informational), may be empty
    bool presetModified = false;
    VariationBank variations;
    AbCompare ab;
    juce::ValueTree midiLearn { midiLearnType }; // opaque; owned by the MIDI-learn feature
    juce::ValueTree ui { uiType };               // opaque; owned by the editor (sizes, theme, options)
};

struct DecodeResult
{
    bool ok = false;
    ProjectModel model;                // meaningful only when ok
    int sourceSchemaVersion = 0;
    bool migrated = false;
    std::vector<Issue> errors;         // non-empty iff !ok
    std::vector<Issue> warnings;       // ignored unknown ids/children, defaulted values; never fatal

    std::string firstError() const { return errors.empty() ? std::string() : describe (errors.front()); }
};

/// Validates a candidate tree (schema 1 or 2), migrating v1 first. Pure: reads `candidate`, never modifies it.
/// Strict: wrong root, future/invalid schemaVersion, duplicate/malformed/non-finite/out-of-range values and
/// inconsistent slots reject the WHOLE candidate (no clamping).
DecodeResult validateAndMigrate (const juce::ValueTree& candidate);

/// Binary host blob (JUCE copyXmlToBinary format) -> validated model. Size-capped by maxStateBytes.
DecodeResult decode (const void* data, int sizeInBytes);

/// Model -> v2 ValueTree. The root is an APVTS-compatible state tree (type SnaralState, one PARAM child per
/// registry parameter with `id` and `value`), so it can be handed to AudioProcessorValueTreeState::replaceState().
juce::ValueTree toValueTree (const ProjectModel& model);

/// Model -> binary host blob (uses toValueTree).
void encode (const ProjectModel& model, juce::MemoryBlock& destination);

/// Factory-fresh model: registry defaults, empty slots, empty children.
ProjectModel defaultModel();

// ---- APVTS helpers (message thread) ----
/// Reads every registry parameter of `apvts` (domain units). Parameters missing from the tree use defaults.
ParamSnapshot snapshotFromApvts (juce::AudioProcessorValueTreeState& apvts);

/// Writes parameters into `apvts` with host notification. With soundOnly (default) utility parameters (bypass,
/// mutes, layer solo) are left alone. Callers wrap this in the processor's RestoreSequenceGuard so the audio
/// thread keeps its previous coherent snapshot while the values change.
void applySnapshot (juce::AudioProcessorValueTreeState& apvts, const ParamSnapshot& snapshot, bool soundOnly = true);

// ---- slot / snapshot tree codec (also used by tests) ----
/// Encodes the sound parameters of `snapshot` as a VALUES node (property per parameter id, shortest round-trip text).
juce::ValueTree snapshotToValuesTree (const ParamSnapshot& snapshot);
} // namespace snaral::project
