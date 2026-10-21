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

Version 1 uses UTF-8 JSON with format identifier snaral-preset, schema_version integer, plugin metadata, preset metadata, and a parameters object keyed by stable parameter ID. Every serialized parameter includes its canonical value; enum values use stable strings. Metadata never stores executable content or external paths. The normative field list, limits, number format, reader rules and migration chain are in [preset-file-contract.md](preset-file-contract.md); parameter values are in registry domain units.

## Four named variations

- Each plug-in instance has slots A, B, C, and D.
- A slot stores a name and complete parameter snapshot. Empty slots are allowed.
- Save-to-slot is explicit and affects only the selected slot. Recall applies the whole state atomically.
- Variation states are part of project/host instance state and are not automatically written to global user preset files.
- Active slot and names persist in project state.
- No random variation generation in this feature. If added later, store the complete generated parameter state plus seed, source preset/version, and algorithm version.

## Implementation contract (state module)

Code: `Source/State/Pure/` (JUCE-free) and `Source/State/ProjectState.*` (project-state codec). Message thread only; nothing here runs on the audio thread.

### Sound parameters versus utility parameters

A "sound parameter" is any parameter with `ParamDef::inPreset()` (all except `bypass`, `layer_solo`, `sub_mute`, `mid_mute`). Presets, variation slots, A/B snapshots and undo deltas store and restore sound parameters only. Utility parameters live in project state only and are never changed by preset load, variation recall, A/B switching or undo. Output trim, limiter enable and ceiling are sound parameters (they are part of a patch); A/B can hold output trim constant for level-matched comparison (see expansion-undo-ab.md).

### Variation bank (`VariationBank`)

- Four slots A to D: `name` (default "A".."D", at most 32 code points, no control characters), `empty` flag, full snapshot.
- `store(i, current)` writes only slot `i` and marks it used; it does not change the active slot. `recall(i, current, out)` returns the whole patch in `out` in one value (sound parameters of the slot, utility parameters of `current`), marks slot `i` active, and fails without writing `out` for an empty slot or index outside 0 to 3. `clear(i)` empties a slot but keeps its name. `rename` trims, validates and resets an empty name to the letter.
- Snapshots are stored as exact floats, so store then recall is bit-exact (tolerance criterion 1e-6 is met with zero error) and so is save/reopen (project state writes shortest round-trip text).
- Variation state, active index and names belong to project state only (schema v2 `VARIATIONS`). They are never written to preset files, and importing or loading a preset never alters a slot.
- Applying a recall is a single published snapshot through the same atomic path as a preset load (see parameter-state.md, "Atomic publish"), and is one undo transaction labelled "Recall Variation <name>".

### Init, load, save flow

1. **Initialize** = the registry-default snapshot, named "Init", applied as one undo transaction ("Initialize").
2. **Load** (browser, import, drag-drop): read file (size-limited) then `parsePreset` / `PresetLibrary::loadPreset` (full validation, no side effects) then, only on success, `UndoHistory::recordTransaction("Load Preset <name>")`, publish the snapshot atomically (sound parameters only), `PresetBrowserModel::notifyLoaded`, `PresetLibrary::markUsed`. Any failure at the first step leaves parameters, variation slots, preset name, dirty flag and undo history unchanged, and the UI shows the first error (`PresetParseResult::firstError()`), which names the offending field.
3. **Modified flag**: `PresetBrowserModel::notifyEdited(current)` recomputes it by comparing the sound parameters with the loaded preset, so editing a knob and putting it back clears the asterisk. Loading over unsaved edits requires a confirmation (`needsConfirmationBeforeLoad`), except audition, which is reversible.
4. **Save / Save As / Rename / Delete / Import / Export**: `PresetLibrary::saveUserPreset`, `renameUserPreset`, `deleteUserPreset`, `importPresetFile`, `exportPreset`. Existing name needs an explicit overwrite confirmation (`SaveStatus::OverwriteRequired` carries the existing path); writes are atomic; factory and pack presets can be exported but never overwritten or deleted.
5. Preset name, preset id and modified flag are saved in project state (`PRESET` node) so a reopened project shows the same name and asterisk.

### Acceptance mapping

| Criterion | Test |
|---|---|
| 1 factory presets validate and load within 1e-6 | `FactoryPresetTests` (27 presets: parse with `requireComplete`, byte-identical re-serialisation, embedded copy equals file) |
| 2 save then import restores values and metadata | `PresetFormatTests` round trip, `PresetLibraryTests` save/import |
| 3 invalid or future files change nothing | `PresetFormatTests` rejections, `PresetLibraryTests` import of 8 bad files, `ProjectStateTests` |
| 4 deterministic export | `PresetFormatTests` (200 random patches written twice and re-written after parsing) |
| 5 four variations exact | `VariationBankTests`, `ProjectStateTests` round trip |
| 6 project save/reopen | `ProjectStateTests` (slots, active slot, preset name, modified flag, A/B) |
| 7 migration per released schema | `PresetFormatTests` (v1 fixtures, injected chain), `ProjectStateTests` (v1 -> v2) |

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
