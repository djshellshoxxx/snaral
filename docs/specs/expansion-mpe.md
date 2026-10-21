# Feature Spec: MPE and Per-Note Expression

**Category:** Product value  
**Status:** Post-beta (not in v0.2 beta). Do not implement before the beta ships; the voice allocator must first carry channel and note identity.  
**Depends on:** midi-voice, host-integration, parameter-state, expansion-midi-cc-learn

## Objective

Support MPE controllers (per-note pitch bend, pressure and timbre) while leaving ordinary MIDI 1.0 behaviour byte-for-byte unchanged when MPE is off.

## Protocol facts used

MPE zones follow the MIDI Manufacturers Association MPE specification: lower zone manager channel 1 with member channels 2 upward, upper zone manager channel 16 with members 15 downward; each sounding note uses its own member channel; members default to a +-48 semitone per-note bend range and the manager channel to +-2; per-note pressure is channel pressure and per-note timbre is CC 74; a zone is configured with an RPN 6 message on the manager channel. The default bend ranges (member 48, manager 2 semitones) were checked against the MIDI Association's MPE pitch-bend article; channel layout, CC 74, channel pressure and RPN 6 must be confirmed against the specification text before coding.

## Behaviour

- Off by default. When off, all channels behave exactly as today: bend on any channel applies to all voices, channel pressure and CC 74 are not interpreted.
- Configuration (instance state, post-beta registry group `mpe.def`, append-only): `mpe_enabled` (bool), `mpe_zone` (lower, upper), `mpe_member_channels` (integer 1 to 15), `mpe_member_bend_range` (integer 1 to 96 st, default 48), `mpe_pressure_dest` (off, mid level, cutoff, drive; default cutoff) and `mpe_timbre_dest` (off, source position, cutoff, drive; default source position), each with a depth parameter `mpe_pressure_depth` and `mpe_timbre_depth` (0 to 100 %). Destinations are mid-only and bounded; the sub receives pitch bend only.
- An incoming RPN 6 on a manager channel reconfigures the zone only if `mpe_enabled` is on; malformed or out-of-range configuration is ignored, the previous valid zone is kept, and the status line says "MPE configuration ignored".
- Note identity is (channel, note number). Repeated same-pitch notes on different channels are separate voices. Voice stealing follows [midi-voice](midi-voice.md) and preserves identity for the surviving voices.
- Per-note bend = member bend (scaled by `mpe_member_bend_range`) plus manager bend (scaled by the manager range `pitch_bend_range`). Per-note pressure and timbre are smoothed (10 ms) and clamped.
- Manager-channel controllers (sustain CC 64, CC 120 and 123, manager bend, manager pressure) apply to every voice in the zone per the specification. Sustain on a member channel is ignored while MPE is on.
- MPE mode forces poly voice mode with at most 8 voices; the status line warns if mono was selected and restores it when MPE is turned off.
- MIDI Learn: while MPE is on, only manager-channel CCs can be learned; member-channel messages are reserved.
- Project state stores the configuration; presets store only the destination and depth parameters (not the zone), so a preset never silently enables MPE.

## Edge cases

Zone overlap with unused channels, a note-on on a non-member channel (treated as ordinary MIDI), note-off ordering across channels, channel pressure arriving before note-on (ignored), pitch bend and pressure at the release stage, host transport stop (CC 123 on all channels releases all).

## Acceptance criteria

1. Two simultaneous notes on separate member channels respond independently to bend, pressure and timbre.
2. Manager-channel controls affect all zone voices exactly as the specification states.
3. The complete standard-MIDI test suite passes unchanged with MPE off, and renders are bit-identical to the pre-MPE build.
4. Note-off, sustain and voice stealing preserve (channel, note) identity; a 100,000-event random MPE stream leaves no stuck note and no NaN.
5. Zone configuration (valid, invalid, overlapping) behaves as stated and survives project save and reopen without affecting ordinary MIDI behaviour.
6. Pressure and timbre destinations never change the sub samples.

## Test fixtures

Scripted MPE event streams (two-note divergence, stealing, malformed RPN), a 100,000-event fuzz stream, and a golden render with MPE off.
