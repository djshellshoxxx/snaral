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

## Host state schema v1

- The JUCE host-state root is `SnaralState` with integer `schemaVersion=1`.
- Each macro is a `PARAM` child with a stable `id` attribute and a finite `value` in its declared DSP-domain range (currently 0–100 for all five macros).
- Version 1 contains exactly one child for each of `weight`, `growl`, `bite`, `motion`, and `space`. Missing, duplicate, unknown, malformed, non-finite, or out-of-range values reject the entire candidate. The current implementation accepts a maximum serialized size of 64 KiB.
- On valid restore, the audio thread retains its previous coherent macro snapshot while the APVTS tree is replaced, then adopts the validated values at the next block boundary. Invalid state does not mutate the parameter tree.
- Before release presets or project files depend on this schema, extend it through a documented migration when Advanced parameters and variation slots enter the registry. Unknown future schema versions remain rejected.

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
