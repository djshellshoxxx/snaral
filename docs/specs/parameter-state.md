# Feature Spec: Parameter and State Foundation

**Module ID:** parameter-state  
**Depends on:** none  
**Consumers:** DSP modules, UI, host automation, presets, variations and tests

## Objective

Provide one authoritative parameter model so all parts of Snaral read and write the same values, use stable identities, and restore saved state without audio glitches or partial updates.

## Contract

- Every parameter has a permanent lowercase snake_case ID, display name, unit, type, range, default, host-automation flag, and conversion between host-normalized and DSP-domain values.
- Continuous parameters expose normalized host values in [0, 1]. Discrete parameters expose stable enumerated choices and valid normalized steps.
- Initial defaults are the initialized patch. Defaults are not inferred from factory preset values.
- The initial parameter registry includes all IDs and ranges in the parent developer spec. Registry changes require a compatibility note.
- DSP receives a coherent parameter snapshot or lock-free parameter changes. UI and preset code do not mutate audio-owned DSP objects directly.
- Restore operations validate the full candidate state before publishing one atomic update at an audio-block boundary.
- Continuous values are smoothed in DSP; smoothing policy belongs to the consuming parameter and is documented. Pitch glide is not generic parameter smoothing.
- Parameter listeners cannot recursively write the same parameter as a side effect of display refresh.

## Parameter metadata requirements

For each parameter, record: ID, label, group, unit, min/max/default, host normalization mapping, discrete choices if applicable, smoothing time or event behavior, macro destinations, serialization rule, and deprecation/migration rule.

The ID is the compatibility key. Renaming a label is allowed. Reusing a retired ID for a different meaning is forbidden.

## State snapshot requirements

A snapshot contains every user-editable parameter, schema/state version, active variation index, four slot names/states, and any required preset metadata. Host-owned automation state remains represented by the host parameter values. Runtime-only values such as oscillator phase need not persist unless an explicit sync/retrigger setting requires them.

Invalid values, unknown enum choices, truncated state, and unsupported future versions must be rejected before publication. A failed restore preserves the active state byte-for-byte at the serialized parameter level.

## Host state schema v1 (legacy, read-only)

Schema v1 is no longer written. It is still accepted on load and migrated to v2 (see the migration table below).

- The JUCE host-state root is `SnaralState` with integer `schemaVersion=1`.
- Each macro is a `PARAM` child with a stable `id` attribute and a finite `value` in its declared DSP-domain range (currently 0–100 for all five macros).
- Version 1 contains exactly one child for each of `weight`, `growl`, `bite`, `motion`, and `space`. Missing, duplicate, unknown, malformed, non-finite, or out-of-range values reject the entire candidate. The current implementation accepts a maximum serialized size of 64 KiB.
- On valid restore, the audio thread retains its previous coherent macro snapshot while the APVTS tree is replaced, then adopts the validated values at the next block boundary. Invalid state does not mutate the parameter tree.
- Unknown future schema versions remain rejected. The extension for Advanced parameters and variation slots is schema v2 below.

## Host state schema v2

Implemented by `Source/State/ProjectState.h/.cpp` (namespace `snaral::project`). The codec is pure: `decode()` / `validateAndMigrate()` read a candidate and return either a complete validated `ProjectModel` or a list of errors. They never touch the live plug-in, so a failed restore leaves the active state byte-for-byte unchanged.

### Tree layout

```
SnaralState                      schemaVersion=2 (int)  pluginVersion="0.2.0" (informational)
  PARAM  id=<registry id>  value=<domain value as shortest round-trip decimal text>      x every registry parameter (sound AND utility)
  PRESET     name=""  presetId=""  modified=0|1
  VARIATIONS active=0..3
    SLOT index=0..3  name="A"  empty=0|1
      VALUES  <registry id>=<text> ...        only when empty=0; sound parameters only
  ABCOMPARE  engaged=0|1  active=0|1  matchTrim=0|1               optional, present once A/B has been used
    SIDE index=0|1 -> VALUES
  MIDILEARN  (opaque placeholder; the MIDI-learn feature defines its children)
  UI         (opaque; the editor owns it: sizes, theme, tour/help flags)
```

- The root is type `SnaralState` so the tree can be handed to `AudioProcessorValueTreeState::replaceState()`. Every `PARAM` carries `id` and `value`, the layout APVTS itself uses. Extra children are ignored by APVTS and survive `copyState()`.
- Values are DOMAIN units (Hz, %, dB, choice index, 0/1), identical to `ParamSnapshot`. Floats are written with the shortest text that parses back to the same `float`, so save then load is bit-exact (the 1e-6 criterion is met with zero error).
- `PARAM` carries every registry parameter including utility ones (bypass, layer solo, sub/mid mute). Variation slots and A/B sides carry sound parameters only (`ParamDef::inPreset()`).
- Slot names: at most 32 code points, no control characters; an empty name resets to the slot letter.
- `UI` and `MIDILEARN` are validated structurally but not semantically: depth at most 8, at most 2048 nodes, property values limited to text (4096 chars), integer, finite double and boolean (no binary, arrays or objects), serialized size at most 256 KiB each. Unknown subtrees are rejected rather than clamped.

### Validation (whole candidate first, then one publish)

Reject the entire candidate (no clamping, nothing applied) on: wrong root type; missing, non-integer, below-1 or future `schemaVersion`; duplicate `PARAM` ids; any value that is missing, non-numeric, non-finite, outside the registry range, or non-integral for a choice/bool/integer parameter; duplicate singleton nodes (PRESET, VARIATIONS, ABCOMPARE, MIDILEARN, UI); out-of-range or duplicate slot index; `empty=0` without VALUES or `empty=1` with VALUES; an A/B node without both sides; invalid names; violated opaque-subtree limits; truncated or non-XML blobs; XML nesting deeper than 64; blobs above the size cap.

Accept with a warning (the candidate is still valid): unknown `PARAM` ids and unknown child nodes (ignored, never preserved), utility ids inside slot VALUES, and parameters that are absent.

**Additive evolution rule.** A parameter appended to the registry later is NOT a schema change: states written before the parameter existed simply lack its `PARAM` child (or VALUES entry) and it takes its registry default, which is the documented default for that parameter. A schema bump is required only when an existing meaning changes (rename, retire, re-scale, re-purposed node). Renamed ids go through an explicit migration table, retired ids are dropped with a warning, and ids are never reused.

### Size cap

v1 accepted 64 KiB. v2 raises the cap to **1 MiB** (`project::maxStateBytes`). Measured sizes are logged by `tests/juce/ProjectStateTests.cpp` (a default state is roughly 8 KiB, a state with four filled slots and A/B roughly 40 KiB), so the cap leaves large headroom for MIDI-learn maps and UI data while still bounding hostile input. The `PluginProcessor` pre-check must use `project::maxStateBytes` instead of its private 64 KiB constant.

### Migration table

| From | To | Rule |
|---|---|---|
| 1 | 2 | Validate exactly like v1 (exactly the five macros, each once, finite, 0 to 100). Build a v2 tree with those five values. Every other parameter takes the registry default. PRESET is empty and unmodified, all four slots are empty and named A to D with slot A active, no A/B state, empty MIDILEARN and UI. The migrated candidate then passes through the normal v2 validation. The decode result reports `migrated=true`. |
| 2 | 3 | Reserved. A future bump adds one pure `ValueTree -> ValueTree` step here; steps run in order and an unknown version is rejected. |

The next save after a migration writes schema 2.

### Atomic publish and the RestoreSequenceGuard

`setStateInformation` stays as it is today in `PluginProcessor.cpp` apart from the three calls below (wired by the integration worker, `PluginProcessor` is not edited by the state module):

1. `size <= 0 || size > project::maxStateBytes` returns early.
2. `auto r = project::decode (data, size); if (! r.ok) return;`. Everything that can fail has happened; nothing live has been touched.
3. Under the state mutex and inside the existing `RestoreSequenceGuard`: `parameters_.replaceState (project::toValueTree (r.model));`. This is the single publish. While the sequence counter is odd the audio thread keeps its previous coherent `ParamSnapshot`, so a restore is heard as one change at a block boundary.
4. After the guard: copy `r.model.variations`, `r.model.ab`, preset name/flag, MIDI-learn and UI trees into their message-thread owners and call `UndoHistory::reset (r.model.params)` (a project restore is not an undoable edit).

`getStateInformation` builds a `ProjectModel` (`snapshotFromApvts()` plus the message-thread objects) and calls `project::encode()`. Preset load, variation recall, A/B switch and undo apply sound parameters with `project::applySnapshot()` inside the same guard, which writes only changed parameters with host notification and leaves utility parameters alone.

## Registry compatibility note

Adding parameters (append to a group `.def`) changes: the preset writer output (all sound parameters are serialized), the factory preset files (re-run `python3 tools/gen_presets.py`; `--check` exits non-zero when stale), and nothing in the project-state schema. Renaming or removing a parameter additionally requires an entry in the preset renamed/retired tables (`PresetFormat.cpp`) and a project-state migration step.

## Acceptance criteria

1. Registry test confirms IDs are unique, stable, and have valid ranges/defaults.
2. Every parameter round-trips normalized value to DSP value and back within 1e-6, excluding documented quantization for enums.
3. Restore of a complete snapshot changes all values together at a block boundary.
4. Invalid snapshot leaves all current values unchanged.
5. Parameter changes do not allocate, block, or take a mutex on the audio thread.
6. UI refresh does not generate host automation writes.
7. Deprecated IDs can be mapped through an explicit versioned migration; retired IDs are never silently repurposed.

## Test fixtures

Maintain a registry fixture for every released parameter and state fixtures for the initialized patch, a full preset, a state containing all discrete options, and malformed/future state. Update fixtures in the same change that intentionally changes a public parameter contract.

## Additional acceptance criteria (schema v2)

8. A v2 state with all parameters, four variation slots, A/B and opaque children round-trips bit-exactly through `encode`/`decode` (60 randomised states in `ProjectStateTests`).
9. A v1 five-macro state migrates with every other parameter at its registry default; each v1 rejection rule still rejects.
10. Truncated, garbage, oversized, deeply nested, future-version, duplicate-id, out-of-range, non-finite and inconsistent-slot states are rejected and leave an APVTS instance unchanged.
11. `replaceState (toValueTree (model))` on a real `AudioProcessorValueTreeState` built from `createParameterLayout()` lands every parameter within 1e-6 normalised, and the resulting `copyState()` is still a decodable candidate.
