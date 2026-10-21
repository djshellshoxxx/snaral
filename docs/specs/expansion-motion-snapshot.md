# Feature Spec: Motion Snapshot and Loop

**Category:** Distinctiveness  
**Status:** Post-beta (not in v0.2 beta). Requires a preset schema v2 with migration before implementation.  
**Depends on:** host-integration, parameter-state, macros-ui, expansion-undo-ab

## Objective

Record a short hand-performed macro movement and replay it as a repeatable free-running or tempo-synced loop, as internal modulation only.

## Model

- Lanes: one to three of Growl, Bite, Motion, Space. Weight is excluded by default to protect sub balance and is not offered. Each lane stores a list of points `(time, value)` with `time` in beats (synced) or seconds (free) and `value` as a normalised macro value.
- Loop length: synced 1 to 8 bars (time signature from the host, 4/4 if unknown), free 0.5 to 30 s. Quantisation of point times: off, 1/8, 1/16, 1/32 (values are never quantised). At most 2,048 points per lane; recording samples the control at 100 Hz and reduces with a deterministic tolerance of 0.002 normalised.
- States: Idle, Armed (waits for the first lane edit), Recording, Playing. Overdub is excluded. Record and Play are explicit buttons; Recording ends at the loop length or on Stop.
- Playback replaces the lane macro's effective value (an internal override feeding the same macro mapping and smoothing) and never writes host automation or parameter values. Moving the lane's control while Playing is ignored for that lane and a badge shows the lane is driven. Stop returns the macro to its parameter value.
- Interpolation per snapshot: linear or smoothstep. Position: synced = host PPQ modulo loop length while the transport plays (free-running from the last tempo while stopped); free = elapsed time modulo length. Host tempo changes keep musical length in synced mode; loop wrap blends from the last to the first value over 10 ms.
- Playback never resets voices, LFO phase, or envelopes.

## Persistence

Lane points, mode (synced or free), length, quantisation, interpolation and a `motion_snapshot_version` are stored in instance state and in presets through preset schema v2 (a new optional `motion_snapshot` object; migration v1 to v2 adds none). Presets without it behave exactly as before. MIDI mappings and slots are unaffected. No registry parameters are added in the first version.

## UI

A Motion Snapshot panel in the Filter & Motion tab: lane toggles, Record, Play, Stop, Clear, length, sync/free, quantise, interpolation, a lane preview, and a status chip. Keyboard operable and described in help.

## Edge cases

Recording with no lane selected (button disabled with a reason), host without tempo (free mode only, synced disabled with the standard message), loop shorter than the recording (truncate with notice), Clear while Playing, preset load while Playing (stops playback, loads the preset's snapshot idle).

## Acceptance criteria

1. Record then replay returns lane values and timing within 1 sample or 1 ms, whichever is larger.
2. A host tempo change in synced mode preserves musical loop length.
3. Loop wrap is click-free under the macro-mapping step criterion.
4. A saved loop survives project save and reopen and renders bit-identically for identical inputs.
5. Weight, sub pitch and sub samples are unchanged by playback; no host parameter value or automation lane changes.
6. A v1 preset loads unchanged; a v2 preset with a snapshot round-trips deterministically.

## Test fixtures

Scripted lane gesture, tempo-change render, wrap-click render, v1 and v2 preset files, and a 2,048-point stress lane.
