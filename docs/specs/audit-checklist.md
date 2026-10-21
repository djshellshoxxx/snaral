# Feature Spec: Beta Audit Checklist (Spec-by-Spec Wiring)

**Module ID:** audit-checklist  
**Depends on:** every beta spec, verification, qa-plan  
**Output:** `docs/release/AUDIT.md` (the completed checklist with evidence)

## Objective

Prove that every planned control, parameter and feature is wired end to end: registry entry, DSP consumer, UI control, preset coverage, host automation, and a test or recorded observation. This is the audit that catches "exists in the table but does nothing" before release.

## Procedure

1. **Registry walk (automated).** Iterate `snaral::parameterTable`. Every ID must match exactly one row of the parameter table below (a row may use a prefix pattern such as `furnace_*`). An ID with no row, a row with no ID, or a duplicate is a failure. The table covers 99 IDs at the time of writing; the test is authoritative, not this number.
2. **Wiring perturbation (automated, offline render).** Start from the *audit patch*: a fixture with Weight, Growl, Motion and Space above 0, and Furnace, Fracture, Trajectory, Sub Anchor, noise, metal and drive enabled, sync LFO with a supplied tempo, rendered at notes 36 and 60. For each ID, render at default and at (a) the registry midpoint and (b) the maximum (or another choice), applying the "Needs" precondition. The renders must differ by more than -90 dB (RMS of the difference relative to the reference RMS), except where the parameter is documented neutral under that condition (then the row records why). A failing row is **UNWIRED**.
3. **UI binding (automated plus screenshot).** Enumerate editor components; every non-utility ID has exactly one primary control bound through the parameter tree, reachable by keyboard, with a tooltip and an accessible name. Failures: **NO-UI**, **DUPLICATE-UI**.
4. **Preset coverage (automated).** Every factory preset serialises every parameter that is not `pfNoPreset`. Across the factory set each enable switch (`furnace_enabled`, `fracture_enabled`, `traj_enabled`, `anchor_enabled`) is on in at least one preset, every choice option is used at least once where musically sensible, and every preset round-trips at 1e-6. Failure: **NO-PRESET**.
5. **Automation (automated plus host).** Every ID without `pfNoAutomation` is in the host parameter list with matching ID, range and default, and automating it changes the render without UI focus; state-only IDs are absent from the list. Failure: **NO-AUTOMATION**.
6. **Non-parameter features:** each row of the feature table is exercised once against its spec's acceptance criteria and the evidence recorded.
7. **Cross-cutting invariants** are rerun on the final candidate.

## Parameter table

| ID(s) | DSP consumer | UI control | In presets | Host automation | Needs for perturbation |
|---|---|---|---|---|---|
| `weight` | MacroMappings -> engine sub gain | Main: Weight knob | yes | yes | sub audible |
| `growl` | MacroMappings -> engine mid/texture gain, blend bias | Main: Growl knob | yes | yes | mid audible |
| `bite` | MacroMappings -> contour peak offset, drive pre-gain | Main: Bite knob | yes | yes | Motion > 0 for the filter part |
| `motion` | MacroMappings -> contour/LFO/trajectory/Furnace depths | Main: Motion knob | yes | yes | contour_depth > 0 |
| `space` | MacroMappings -> Width | Main: Space knob | yes | yes | mid audible, stereo output |
| `voice_mode`, `legato`, `glide_ms` | VoiceAllocator | Voice tab; Main performance row | yes | yes | two overlapping notes |
| `pitch_bend_range` | VoiceAllocator bend scaling | Voice tab | yes | yes | send pitch bend |
| `velocity_to_level`, `velocity_to_attack` | Voice velocity maps | Voice tab | yes | yes | velocity below 127 |
| `master_tune_hz`, `transpose_semitones` | VoiceAllocator tuning | Voice tab | yes | yes | measure note 69 frequency |
| `sub_waveform`, `sub_tune_cents`, `sub_level`, `sub_amp_attack_ms`, `sub_amp_decay_ms`, `sub_amp_sustain`, `sub_amp_release_ms` | Oscillators (sub), Adsr | Sub tab | yes | yes | Weight > 0 |
| `mid_source_a`, `mid_source_b`, `mid_blend`, `mid_table_position`, `mid_pulse_width`, `mid_tune_cents` | Oscillators (mid sources) | Sources tab | yes | yes | pulse width needs pulse_pair; blend needs A differs from B |
| `mid_level`, `mid_amp_attack_ms`, `mid_amp_decay_ms`, `mid_amp_sustain`, `mid_amp_release_ms` | Voice mid path, Adsr | Sources tab | yes | yes | Growl > 0 |
| `filter_cutoff_hz`, `filter_resonance`, `filter_type` | SvfFilter | Filter & Motion: Filter | yes | yes | mid audible |
| `contour_attack_ms`, `contour_decay_ms`, `contour_sustain`, `contour_release_ms`, `contour_shape`, `contour_depth` | Contour -> SvfFilter | Filter & Motion: Contour | yes | yes | Motion > 0 |
| `lfo_mode`, `lfo_rate_hz`, `lfo_sync_division`, `lfo_waveform`, `lfo_retrigger`, `lfo_phase`, `lfo_depth` | Lfo -> filter / source position | Filter & Motion: LFO | yes | yes | Motion > 0 and lfo_depth > 0; sync needs host tempo; phase needs retrigger |
| `drive_type`, `drive_amount`, `drive_tone`, `drive_mix` | Drive | Drive tab | yes | yes | mid audible; mix > 0 |
| `noise_level`, `noise_tone` | Texture (noise) | Texture tab | yes | yes | noise_level > 0 |
| `metal_level`, `metal_decay_ms`, `metal_tune` | Texture (metal) | Texture tab | yes | yes | metal_level > 0 |
| `output_trim_db`, `limiter_enabled`, `limiter_ceiling_db` | Engine output stage, Limiter | Output tab | yes | yes | signal near ceiling for the limiter |
| `bypass` | Processor/engine output ramp | Action row: Bypass; host bypass | no (project state) | yes | - |
| `layer_solo`, `sub_mute`, `mid_mute` | Engine final sum | Advanced header: solo/mute | no (project state) | yes | both layers audible |
| `furnace_enabled`, `furnace_vowel`, `furnace_spacing`, `furnace_resonance`, `furnace_env_amount`, `furnace_lfo_amount`, `furnace_mix` | Fx/FormantFurnace | Effects tab: Formant Furnace | yes | yes | furnace_enabled; env/LFO amounts need Motion > 0 |
| `fracture_enabled`, `fracture_crossover_hz`, `fracture_drive_low`, `fracture_drive_mid`, `fracture_drive_high`, `fracture_symmetry`, `fracture_band_mix`, `fracture_comp_db`, `fracture_mix` | Fx/HarmonicFracture | Effects tab: Harmonic Fracture | yes | yes | fracture_enabled |
| `traj_enabled`, `traj_time_mode`, `traj_length_ms`, `traj_length_div`, `traj_dest`, `traj_depth`, `traj_curve`, `traj_loop` | Trajectory -> filter / source position | Filter & Motion: Snarl Trajectory | yes | yes | traj_enabled and Motion > 0; length_ms or division by time mode |
| `traj_x2`, `traj_x3`, `traj_x4`, `traj_y1`, `traj_y2`, `traj_y3`, `traj_y4`, `traj_y5` | Trajectory curve points | Trajectory editor points | yes | no (state only) | traj_enabled and Motion > 0 |
| `oversampling_mode` | Oversampler (latency reported) | Output tab: Quality | yes | yes | high drive; latency changes |
| `anchor_enabled`, `anchor_note`, `anchor_strength` | SubAnchor | Sub tab: Sub Anchor | yes | yes | anchor_enabled and note differs from anchor_note |

## Feature table

| Feature | Spec | Wiring to confirm | Evidence |
|---|---|---|---|
| Sustain CC64, CC120, CC123, pitch bend, velocity | performance-foundation, midi-voice | engine handles each; MIDI Learn reserves them | unit tests, fuzz run |
| Panic | performance-foundation, cdl-common-actions | both buttons call `engine.panic()`; clears pedal | test, screenshot |
| Reset, Randomize, Reproduce, locks | cdl-common-actions | action row; one undo step each; exclusions honoured | RandomizeTests |
| Bypass including host bypass | cdl-common-actions, host-integration | `bypass` is the host bypass parameter | host smoke |
| Undo, Redo, A/B, Copy A to B | expansion-undo-ab | buttons, shortcuts, 100-step history | tests, UI |
| Variation slots A to D, active slot | preset-variation | Main performance row; project state | state round-trip |
| Preset selector, browser, favourites, audition | preset-variation, expansion-preset-browser | header and browser panel | browser tests |
| Save, import, export presets | preset-file-contract | atomic write; failure leaves state | fixtures |
| MIDI CC Learn and mapping list | expansion-midi-cc-learn | context menu, Options list, state | MidiLearnTests |
| Export WAV | cdl-offline-wav-export | dialog, worker, atomic finalise | WavExportTests |
| Themes, tooltips, animation, onboarding rerun | cdl-theme-options, cdl-help-and-onboarding | Options page; preferences stay outside state | UI and restart test |
| Reference tone (standalone only) | cdl-theme-options, host-integration | absent from plug-in builds | measurement |
| Signal scope and CPU gauge | cdl-signal-visualization | status area; Options switches | harness |
| Help content | cdl-help-and-onboarding | generated from the registry; no deferred feature described as available | docs check |
| Tempo status, latency readout, status line | host-integration, macros-ui | status priority list | UI test |
| Oversampling latency report | expansion-oversampling, host-integration | `getLatencySamples` | impulse test |
| Formats and packaging | expansion-format-support, release-packaging | one artefact per matrix cell | CI log |

## Cross-cutting invariants (rerun on the final candidate)

- Sub is sample-identical L/R for every parameter combination in the stress run, every Space setting, every solo/mute/effect combination, and every oversampling mode.
- No allocation, lock or file access in `process`; the allocation counter is zero.
- Reported latency equals measured latency; sub and mid stay aligned within one sample.
- Parameter IDs are unchanged from the previous baseline (ID list diff shows additions only).
- Every deferred feature is absent from the UI and help, or labelled unavailable.

## Result record

`AUDIT.md` lists, for each table row: PASS, FAIL (class), BLOCKED or UNVERIFIED, the command or observation, and the commit. The audit is complete when no row is FAIL and every BLOCKED or UNVERIFIED row has an approved exception.

## Acceptance criteria

1. The registry walk passes with every ID matched exactly once.
2. Zero UNWIRED, NO-UI, DUPLICATE-UI, NO-PRESET and NO-AUTOMATION rows on the final candidate.
3. Every feature-table row has recorded evidence.
4. Cross-cutting invariants pass.
5. The audit is repeated after any change to the registry or to a spec listed here.

## Test fixtures

The audit patch (stored as a preset fixture), the perturbation harness in `tests/`, the registry-versus-table matcher, and the previous-release ID list (`tests/fixtures/param_ids_baseline.txt`).
