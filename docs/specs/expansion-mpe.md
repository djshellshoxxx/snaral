# Feature Spec: MPE and Per-Note Expression

**Category:** Product value  
**Depends on:** midi-voice, host-integration, parameter-state

## Objective
Support expressive MPE controllers while preserving standard MIDI 1.0 channel behavior.

## Behavior
- MPE zone configuration: lower/upper zone, member-channel count, master channel, pitch-bend range, and per-note pressure/timbre assignments.
- Per-note pitch bend and pressure affect only their corresponding active voice in poly mode.
- MPE disabled by default; ordinary MIDI mapping remains unchanged.
- Channel allocation and note identity use MIDI channel plus note identity to handle repeated same-pitch notes.
- Unsupported or malformed MPE configuration falls back safely and reports status.
- MPE state is stored in project state; MIDI-learn cannot capture reserved MPE messages.

## Acceptance
1. Two simultaneous notes on separate member channels respond independently to bend and pressure.
2. Master-channel controls affect all voices only according to documented MPE zone rules.
3. Standard MIDI tests pass unchanged with MPE disabled.
4. Note-off, sustain pedal, and voice stealing preserve channel/note identity.
5. State and host automation survive project save/reopen without changing ordinary MIDI behavior.
