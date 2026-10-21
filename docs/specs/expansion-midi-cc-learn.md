# Feature Spec: MIDI CC Learn (Core CDL Requirement)

**Category:** Usability  
**Depends on:** host-integration, parameter-state, macros-ui, cdl-common-actions  
**Release scope:** Core CDL-compliant release and beta. The filename is retained for link stability; this feature is not deferred.  
**Implementation:** mapping model, lookup and value conversion are JUCE-free in `Source/Core/MidiLearn.*`; persistence in `Source/State/`; UI in the editor.

## Objective

Let hardware controls operate Snaral without the user knowing CC numbers, using the same destination mapping, smoothing and undo semantics as UI and host automation.

## Mapping model

One mapping: `{ target parameter ID, cc 0..127, channel 0..16 (0 = omni), min, max, invert }`.
- `min` and `max` are normalised target values in [0,1] with `min <= max` (default 0 and 1); `invert` flips the CC direction.
- At most 16 mappings per instance; at most one mapping per target; a (cc, channel) pair maps to at most one target, where omni overlaps every channel.
- Not needed in beta (recorded scope cut): 14-bit CC pairs (MSB/LSB), NRPN/RPN, per-mapping curves, relative encoders, MIDI output. A global template export/import file is post-beta.

## Learnable parameters

A parameter is learnable when it is a Float or Bool in the registry, is not `pfNoAutomation`, is not `pfInteger`, and is not in the exclusion list: `master_tune_hz`, `limiter_enabled`, `limiter_ceiling_db`, `bypass`, `sub_mute`, `mid_mute`. Choice parameters (including `voice_mode`, `layer_solo`, `oversampling_mode`) are not learnable in beta. Panic, Randomize and A/B are actions, not parameters, and are not learnable. This includes the five macros, `output_trim_db`, filter, contour, LFO, drive, texture, mid/sub levels and ADSR times, Furnace, Fracture, Trajectory scalars and enable switches, `legato`, `anchor_enabled`, `anchor_strength`.

## Reserved messages (cannot be learned or mapped)

Pitch bend, channel pressure, polyphonic pressure, program change, and CCs 0 and 32 (bank select), 6 and 38 (data entry), 64 (sustain), 98 to 101 (NRPN/RPN), and 120 to 127 (all channel-mode messages including 120 All Sound Off and 123 All Notes Off). Reserved messages always keep their MIDI meaning and are never consumed by a mapping. Learn ignores them and shows "Reserved MIDI message" in the status line.

## Value conversion

For CC value `v` in 0..127: `t = v / 127`; if invert, `t = 1 - t`; `n = min + (max - min) * t`; the target applies `fromNormalized(n)`. `v = 0` gives exactly `min` (or `max` inverted) and `v = 127` the opposite end. For Bool targets the value is on when `n >= 0.5`. The result enters the same snapshot path and smoothing as UI edits and automation; continuous macros keep their 10 ms smoothing.

## UI flow

1. Right-click (or keyboard menu key) a learnable control, choose MIDI Learn. The control shows "Learning..." and the status line says "Move a controller. Esc cancels."
2. The first eligible CC message (any channel) is captured, assigned with `min 0, max 1, omni on`, and reported as "CC 74 ch 1 mapped to Bite".
3. Escape, a click elsewhere, or starting Learn on another control cancels without changes. Learn mode has no timeout.
4. A mapped control shows a small CC badge. Context menu "Unlink mapping" removes it.
5. Options lists every mapping with target, CC, channel (or Omni), min, max, invert, and Remove or Clear all.
6. Conflicts: if the captured (cc, channel) is already mapped to another target, a dialog offers Replace (the old mapping is removed), Keep both (only possible when both mappings can be set to distinct explicit channels; otherwise disabled with an explanation), or Cancel. If the target already has a mapping, learning offers Replace or Cancel. At 16 mappings a new one is refused with a message. Nothing is remapped silently.

## Persistence and threading

- Mappings live in instance (project) state as a `MIDI_MAP` child with one `MAP` element per mapping (`target`, `cc`, `channel`, `min`, `max`, `invert`), versioned with the state schema. They are not part of presets, variations or A/B snapshots, and Reset and Randomize leave them alone.
- The message thread builds an immutable 128 x 17 lookup and publishes it by atomic pointer swap; the audio thread only reads it. Learn capture is signalled with a lock-free queue. The audio thread never allocates, locks or notifies the host directly; resulting value changes are applied to the next block's snapshot and mirrored to the host and UI through a bounded queue drained on the message thread.
- Host reality: some hosts do not deliver CC to instruments or remap them. CC behaviour is verified per host and recorded; the standalone target is the reference.

## Acceptance criteria

1. Learn captures the first eligible CC, assigns it, and reports channel and number; reserved messages are ignored.
2. Fixtures at CC 0, 1, 64, 126, 127 with default and inverted ranges give exact endpoints and monotonic intermediate values.
3. Conflict and limit flows require an explicit choice; cancel leaves mappings unchanged.
4. Every reserved message keeps its MIDI behaviour while a mapping on a neighbouring CC exists.
5. Mappings survive project save and reopen and standalone restart, and are absent from preset files.
6. A 60 s random CC storm (all 128 CCs, 16 channels) causes no allocation, lock, NaN or dropped notes; mapped targets follow the last value.
7. A mapped macro and the same macro moved by UI produce the same destination values.

## Test fixtures

`tests/MidiLearnTests.cpp` (JUCE-free): conversion table, conflict resolver, reserved-message filter, learnable-parameter classifier checked against the registry, and lookup build. `tests/juce/MidiMapStateTests.cpp`: state round-trip and malformed-map rejection (duplicate pair, bad target, 17th entry, `min > max`).

## Implementation decisions (Source/Core/MidiLearn.*)

- API (namespace `snaral::midi`): `MidiMapping`, `MidiLearnModel` (message thread: arm/cancel/capture/resolve/remove/clearAll/update/load), `LearnCapture` (lock-free audio-to-UI capture of the first eligible CC), `MidiLearnTable` (immutable lookup) with `TablePublisher` (atomic pointer swap, 8 retired tables kept alive), `MidiLearnRuntime::apply` (audio thread, fixed arrays, no allocation), `isLearnable`, `isReservedCc`, `convertCc`, `toText`/`fromText`.
- Lookup table is 128 x 17 `int8` slot indices (channel 0 unused at lookup time: omni mappings are expanded into channels 1 to 16 when the table is built). A message always arrives with channel 1 to 16.
- Reserved CC set is exactly the list above (0, 6, 32, 38, 64, 98 to 101, 120 to 127). Reserved CCs are rejected by validation, never enter the table and are never consumed.
- Capture assigns `min 0, max 1, omni`. Conflict test: any other target already on the same CC (any channel) is a conflict, because the new omni mapping would overlap it. Choices: Replace (removes all holders and the target's own old mapping), Keep both, Cancel. Keep both is available only when no holder is on the exact captured channel and at most one holder is omni; the new mapping then takes the captured explicit channel and an omni holder is pinned to a different channel (suggested free channel, user-changeable); otherwise it is refused with an explanation and the choice stays pending. A target that already has a mapping is a Replace or Cancel choice. A new mapping for an unmapped target at 16 mappings ends with `LimitReached`; remapping an already-mapped target at 16 is allowed.
- Value application has two modes. Default `Jump` is the spec behaviour (value applied at once; smoothing happens downstream). Optional `Pickup`: a CC is ignored until it reaches or crosses the parameter's last known normalised value, re-armed by `notifyExternalChange` (UI, automation, preset load). Bool targets always jump (on when `n >= 0.5`). The result carries the normalised target and the domain value (`fromNormalized`).
- Persistence: `MidiMapData` plus line-based text (`snaral-midimap 1`, one `map target=<id> cc= ch= min= max= invert=` line per mapping). The state code stores this text in one string property of the project-state `MIDILEARN` child, or builds `MAP` children from `MidiMapData::mappings` (verified by `tests/juce/CoreJuceTests.cpp`). `fromText` and `validateMappings` reject a malformed map as a whole (unknown or non-learnable target, reserved or out-of-range cc, `min > max`, duplicate target, overlapping (cc, channel), more than 16 entries).
- Offline export applies a map to CC events in the phrase through the same `MidiLearnRuntime` (mapped CCs are not forwarded to the engine).
- Tests: `tests/MidiLearnTests.cpp`, `tests/juce/CoreJuceTests.cpp`.
