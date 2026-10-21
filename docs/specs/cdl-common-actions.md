# Feature Spec: Shared CDL Actions and Bounded Randomize

**Requirement source:** [Copied approved baseline](../../standards/AUDIO_APP_BASELINE.md)  
**Applies to:** Snaral plug-in and standalone  
**Depends on:** parameter-state, performance-foundation, expansion-undo-ab, expansion-midi-cc-learn  
**Implementation:** JUCE-free logic in `Source/Core/` (randomize, reset); wiring in the editor and processor

## Objective

Provide consistent Stop/Panic, Reset, Randomize, Bypass, preset selection and parameter context actions without affecting device, safety, host or user-file state.

## Actions

- **Stop/Panic:** calls `engine.panic()` (All Sound Off with a 5 ms de-click ramp, voices cleared, sustain state cleared). It never alters parameters, stops host transport, or touches other tracks. In standalone it also stops the reference tone. Two buttons (action row and performance row) call the same function. Keyboard: focusable button; shortcut Ctrl/Cmd+Period when the editor has focus.
- **Reset:** loads the initialized patch: every parameter without `pfNoPreset` is set to its registry default, preset name becomes "Initialized", dirty state clears. One undo transaction. Preserved: variation slots, A/B buffers, MIDI mappings, utility state (bypass, solo, mute), theme and all other preferences, device selection, user files.
- **Bypass:** toggles the `bypass` parameter (also the host bypass). The instrument output ramps to silence in 10 ms and back; voices keep running. It is not part of presets, A/B snapshots, or Randomize.
- **Randomize:** see below. **Export WAV** sits in the preset row and opens [Offline WAV Export](cdl-offline-wav-export.md); the dialog states that host effects, automation and transport are not included.
- **Context menu on a parameter:** copy value (plain domain number), validated paste, numerical entry, MIDI Learn, unlink mapping (only if mapped), reset to default. Paste accepts a finite number with an optional matching unit suffix (`k` for kHz); out-of-range or malformed text is rejected with a status message and never clamped or applied silently. Unavailable items are omitted. Nothing here remaps MIDI without the conflict flow of the MIDI Learn spec.

## Randomize: algorithm version 1

- Seed: unsigned 32-bit, shown as 8 hex digits next to the button; a new seed is drawn from the system clock for each Randomize press. **Reproduce** reapplies the displayed seed to the current state with the current locks.
- PRNG: splitmix64 seeded from the 32-bit seed, then one `uniform [0,1)` draw per eligible parameter in registry order. A draw is consumed even for locked parameters (and discarded), so the same seed gives the same values for unlocked parameters regardless of locks.
- Eligible parameters and bounds (domain units; ranges are intersected with the registry range):

| Lock group | Parameters and bounds |
|---|---|
| Macros (never locked) | `weight` 30 to 100, `growl` 20 to 100, `bite` 0 to 70, `motion` 0 to 100, `space` 0 to 80 |
| Voice | `legato`, `glide_ms` 0 to 200 |
| Sub | `sub_waveform`, `sub_level` 60 to 100, `sub_amp_attack_ms` 0 to 20, `sub_amp_decay_ms`, `sub_amp_sustain` 60 to 100, `sub_amp_release_ms` 20 to 600 |
| Mid Sources | `mid_source_a`, `mid_source_b`, `mid_blend`, `mid_table_position`, `mid_pulse_width`, `mid_tune_cents` -15 to 15, `mid_level` 50 to 100, mid ADSR (attack 0 to 40 ms, sustain 40 to 100, release 20 to 800) |
| Motion | `filter_cutoff_hz` 150 to 6000 (log draw), `filter_resonance` 0 to 60, `filter_type`, all `contour_*`, all `lfo_*`, Trajectory enable (25% chance on), `traj_*` shape and valid ordered points |
| Drive | `drive_type`, `drive_amount` 0 to 70, `drive_tone`, `drive_mix` 30 to 100 |
| Texture | `noise_*` and `metal_*`: each level is nonzero with 25% chance and at most 40 |
| Effects | `furnace_*`, `fracture_*`: each effect on with 30% chance, mix at most 70 |

- Never randomized: `output_trim_db`, `limiter_*`, `master_tune_hz`, `transpose_semitones`, `pitch_bend_range`, `voice_mode`, `velocity_*`, `sub_tune_cents`, `anchor_*`, `oversampling_mode`, all utility parameters, MIDI mappings, host and device state, theme.
- Locks for Voice, Sub, Sources, Motion, Drive, Texture and Effects default to unlocked and are a per-user preference, not preset or host state.
- Validate the whole candidate against the registry, then publish as one transaction at a block boundary. If the current patch fails validation, do nothing and report it. The seed and algorithm version are stored in project state (not in the preset file, whose v1 schema has no such field).
- Sub invariant: Randomize cannot change anything that routes the sub through mid processing or width; sub L/R stay identical for every output.

## Edge cases

Randomize while notes sound is click-safe through normal parameter smoothing. Randomize with every group locked changes only macros. Undo returns the exact previous state.

## Acceptance criteria

1. Reset restores registry defaults for all preset parameters and leaves preferences, mappings, slots, utility state and files untouched.
2. 10,000 seeded randomizations produce only in-range parameter states, finite 1 s renders at notes 36 and 60, no stuck notes, and sample-identical L/R sub.
3. Same seed, source state, locks and algorithm version give an identical parameter state; different locks do not change unlocked results.
4. Excluded parameters are bit-identical before and after; locked groups are unchanged.
5. Panic clears voices and pedal state within 5 ms and changes no parameter or transport.
6. Bypass and Panic are keyboard-operable and labeled; bypass transition has no discontinuity above the smoothing criteria.
7. The preset row exposes Export WAV with the scope disclosure.
8. Context actions reject invalid paste, never clamp silently, and never remap MIDI on their own.
9. Randomize, Reset and Reproduce are each exactly one undo step.

## Test fixtures

`tests/RandomizeTests.cpp` (JUCE-free): golden vectors for seeds 0, 1, 0xDEADBEEF; 10,000-seed range sweep; lock-independence; exclusion check against the registry. Engine render checks reuse the offline render driver.
