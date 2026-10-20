# Feature Spec: Presets and Variations

**Module ID:** preset-variation  
**Depends on:** parameter-state  
**Consumers:** macros-ui, host-integration, producers

## Objective

Provide safe, repeatable patch storage and four exact instance-level snapshots without making random generation part of the instrument’s core workflow.

## Factory and user preset contract

- Ship at least 10 varied factory presets before release candidate.
- Factory presets are version-controlled JSON fixtures with names, descriptions, tags, and provenance/license metadata for any source asset.
- User can initialize, load, save, rename, import, and export presets.
- Import parses and validates the complete candidate before changing the active state.
- Reject wrong format, unsupported future schema, malformed JSON, duplicate IDs if the format contains a list, nonfinite numeric values, invalid enum values, and unreasonable file sizes.
- Unknown parameter IDs may be ignored with a warning. Missing parameters use only documented schema-version defaults.
- Export uses deterministic key order and numeric representation.

## JSON schema

Version 1 uses UTF-8 JSON with format identifier snaral-preset, schema_version integer, plugin metadata, preset metadata, and a parameters object keyed by stable parameter ID. Every serialized parameter includes its canonical value; enum values use stable strings. Metadata never stores executable content or external paths.

## Four named variations

- Each plug-in instance has slots A, B, C, and D.
- A slot stores a name and complete parameter snapshot. Empty slots are allowed.
- Save-to-slot is explicit and affects only the selected slot. Recall applies the whole state atomically.
- Variation states are part of project/host instance state and are not automatically written to global user preset files.
- Active slot and names persist in project state.
- No random variation generation in this feature. If added later, store the complete generated parameter state plus seed, source preset/version, and algorithm version.

## Failure and recovery behavior

A failed file read or validation leaves active parameters, variation slots, and preset name unchanged. A save uses atomic file replacement where supported so interruption cannot leave a truncated valid-looking preset. The UI reports concise failure reason and retains unsaved edits.

## Acceptance criteria

1. All factory presets pass schema validation and load each parameter within normalized tolerance 1e-6.
2. Saving then importing a user preset restores all serialized values and metadata.
3. Invalid/future files do not alter current state or any slot.
4. Export of an unchanged state is byte-for-byte deterministic.
5. Four variations round-trip names and all parameter values within 1e-6.
6. Project save/reopen restores all slots, active slot, active patch, and unsaved state as defined by host state behavior.
7. Migration tests cover each released schema version; no released ID is repurposed.
