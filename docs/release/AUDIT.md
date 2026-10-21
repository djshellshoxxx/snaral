# Beta audit (audit-checklist.md)

Branch `feat/audit`, base `work/beta` at `78e2a48`. Method: [audit-checklist](../specs/audit-checklist.md), every spec in `docs/specs/` and `docs/IMPLEMENTATION_PLAN.md`, plus reading the code from parameter to engine to editor to state. Result key: PASS, FAIL (class), BLOCKED, UNVERIFIED.

## Result

- 99 registry IDs, 99 matched exactly once to the checklist table, 99 wired in the engine (0 UNWIRED, 0 NO-UI, 0 DUPLICATE-UI, 0 NO-PRESET, 0 NO-AUTOMATION on the final candidate).
- 8 gaps found and fixed (section 5). Remaining open items are listed in section 6 (none is a FAIL).
- Commands used (all rerunnable):
  - `tools/build_tests.sh` (38 JUCE-free tests incl. `snaral_wiringperturbationtests`, `snaral_factoryrenderqatests`, `snaral_consistencycheck`)
  - `tools/build_juce.sh` (JUCE tests: core, project state, UI kit, UI snapshots, real editor, real processor; 45 in total)
  - `build-tests/snaral_wiringperturbationtests > out.txt && tools/gen_audit.py out.txt` (regenerates the table in section 2)
  - `python3 tools/gen_wiring.py`, `python3 tools/consistency_check.py`, `python3 tools/gen_presets.py --check`

## 1. Registry walk (checklist step 1): PASS

`tests/WiringPerturbationTests.cpp` parses the parameter table of the checklist and walks `snaral::parameterTable`. Every ID matches exactly one row (prefix rows such as `traj_*` points are listed by ID), every table ID exists, and the table columns "In presets" and "Host automation" agree with the `pfNoPreset` / `pfNoAutomation` flags (95 in presets, 91 automatable). `tools/consistency_check.py` additionally compares the registry with presets, help text, UI layout, MIDI learn, randomizer and `tests/fixtures/param_ids_baseline.txt` (additions only).

## 2. Parameters: wiring, UI, presets, automation (steps 2 to 5)

Wired = the render with the parameter moved to min, midpoint or max differs from the reference by more than -90 dB (RMS of the difference relative to the reference RMS) in at least one scenario the parameter needs, starting from the audit patch (Weight 70, Growl 70, Bite 40, Motion 70, Space 60, mid sources folded triangle + formant pair blended, sync LFO at 174 BPM with retrigger, Furnace, Fracture, Trajectory (both), Sub Anchor, noise, metal and drive on; notes 36 and 60, plus two-note, bend, velocity and release scenarios where the checklist "Needs" column says so). The smallest margin is `metal_tune` at -48.9 dB, so every ID is far above the threshold. Documented neutral conditions applied in the harness: `contour_*` are measured with the Trajectory off (it replaces the contour on its destination), `lfo_rate_hz` with the LFO in free mode (sync ignores the free rate), `lfo_retrigger` in free mode with a second note, `sub_amp_decay_ms` with sustain below 100 %.

UI column: every ID has exactly one primary control bound through the parameter tree (`EditorTests::testEveryParameterHasAControl`, `primaryCount == 1`), driven by `testTweakEveryControl`. Sliders, combos, toggles and macro knobs set title, description, tooltip and keyboard focus (`Source/UI/ParamControls.cpp`, `MacroKnob.cpp`, `TrajectoryEditor.cpp`). Preset column: all 95 sound IDs are serialised in all 27 factory presets (`FactoryPresetTests`, strict `requireComplete`). Automation column: `ProcessorTests::testHostParameterList` checks ID, version hint 1, range, default, round trip within 1e-5 and the automatable flag for every ID (91 automatable). JUCE marks the 8 Trajectory point IDs as not automatable; hosts may still list them read-only (JUCE has no way to hide a parameter that is also in the state tree), see section 6.

| Feature (parameter id) | Spec | Code location (DSP consumer) | Wired | Notes |
|---|---|---|---|---|
| `drive_type` | processing-output | Dsp/Drive.h | Y | Drive tab: Drive; 27/27 serialised, 2 distinct values; host-automatable; perturbation -0.1 dB vs -90 dB limit |
| `drive_amount` | processing-output | Dsp/Drive.h | Y | Drive tab: Drive; 27/27 serialised, 16 distinct values; host-automatable; perturbation -10.7 dB vs -90 dB limit |
| `drive_tone` | processing-output | Dsp/Drive.h | Y | Drive tab: Drive; 27/27 serialised, 3 distinct values; host-automatable; perturbation -6.2 dB vs -90 dB limit |
| `drive_mix` | processing-output | Dsp/Drive.h | Y | Drive tab: Drive; 27/27 serialised, 1 distinct values; host-automatable; perturbation -7.2 dB vs -90 dB limit |
| `noise_level` | processing-output | Dsp/Texture.h | Y | Texture tab: Noise; 27/27 serialised, 3 distinct values; host-automatable; perturbation -13.7 dB vs -90 dB limit |
| `noise_tone` | processing-output | Dsp/Texture.h | Y | Texture tab: Noise; 27/27 serialised, 3 distinct values; host-automatable; perturbation -43.7 dB vs -90 dB limit |
| `metal_level` | processing-output | Dsp/Texture.h | Y | Texture tab: Metal; 27/27 serialised, 2 distinct values; host-automatable; perturbation -24.4 dB vs -90 dB limit |
| `metal_decay_ms` | processing-output | Dsp/Texture.h | Y | Texture tab: Metal; 27/27 serialised, 2 distinct values; host-automatable; perturbation -45.5 dB vs -90 dB limit |
| `metal_tune` | processing-output | Dsp/Texture.h | Y | Texture tab: Metal; 27/27 serialised, 2 distinct values; host-automatable; perturbation -48.9 dB vs -90 dB limit |
| `filter_cutoff_hz` | motion-filter | Dsp/Contour.h, Dsp/SvfFilter.h, Engine/SnaralEngine.cpp | Y | Filter & Motion tab: Filter; 27/27 serialised, 19 distinct values; host-automatable; perturbation -1.5 dB vs -90 dB limit |
| `filter_resonance` | motion-filter | Dsp/SvfFilter.h, Engine/SnaralEngine.cpp | Y | Filter & Motion tab: Filter; 27/27 serialised, 16 distinct values; host-automatable; perturbation -5.6 dB vs -90 dB limit |
| `filter_type` | motion-filter | Dsp/SvfFilter.h | Y | Filter & Motion tab: Filter; 27/27 serialised, 2 distinct values; host-automatable; perturbation -1.3 dB vs -90 dB limit |
| `contour_attack_ms` | motion-filter | Dsp/Contour.h | Y | Filter & Motion tab: Contour; 27/27 serialised, 3 distinct values; host-automatable; perturbation -10.2 dB vs -90 dB limit |
| `contour_decay_ms` | motion-filter | Dsp/Contour.h | Y | Filter & Motion tab: Contour; 27/27 serialised, 10 distinct values; host-automatable; perturbation -10.8 dB vs -90 dB limit |
| `contour_sustain` | motion-filter | Dsp/Contour.h | Y | Filter & Motion tab: Contour; 27/27 serialised, 6 distinct values; host-automatable; perturbation -10.5 dB vs -90 dB limit |
| `contour_release_ms` | motion-filter | Dsp/Contour.h | Y | Filter & Motion tab: Contour; 27/27 serialised, 2 distinct values; host-automatable; perturbation -21.6 dB vs -90 dB limit |
| `contour_shape` | motion-filter | Dsp/Contour.h | Y | Filter & Motion tab: Contour; 27/27 serialised, 3 distinct values; host-automatable; perturbation -16.7 dB vs -90 dB limit |
| `contour_depth` | motion-filter | Dsp/Contour.h, Engine/SnaralEngine.cpp | Y | Filter & Motion tab: Contour; 27/27 serialised, 15 distinct values; host-automatable; perturbation -7.6 dB vs -90 dB limit |
| `lfo_mode` | motion-filter | Dsp/Lfo.h | Y | Filter & Motion tab: LFO; 27/27 serialised, 2 distinct values; host-automatable; perturbation -8.4 dB vs -90 dB limit |
| `lfo_rate_hz` | motion-filter | Dsp/Lfo.h | Y | Filter & Motion tab: LFO; 27/27 serialised, 8 distinct values; host-automatable; perturbation -8.0 dB vs -90 dB limit |
| `lfo_sync_division` | motion-filter | Dsp/Lfo.h | Y | Filter & Motion tab: LFO; 27/27 serialised, 4 distinct values; host-automatable; perturbation -8.3 dB vs -90 dB limit |
| `lfo_waveform` | motion-filter | Dsp/Lfo.h | Y | Filter & Motion tab: LFO; 27/27 serialised, 4 distinct values; host-automatable; perturbation -7.8 dB vs -90 dB limit |
| `lfo_retrigger` | motion-filter | Dsp/Lfo.h, Engine/Voice.h | Y | Filter & Motion tab: LFO; 27/27 serialised, 2 distinct values; host-automatable; perturbation -8.9 dB vs -90 dB limit |
| `lfo_phase` | motion-filter | Dsp/Lfo.h | Y | Filter & Motion tab: LFO; 27/27 serialised, 1 distinct values; host-automatable; perturbation -6.7 dB vs -90 dB limit |
| `lfo_depth` | motion-filter | Dsp/Contour.h, Dsp/Lfo.h, Engine/SnaralEngine.cpp | Y | Filter & Motion tab: LFO; 27/27 serialised, 10 distinct values; host-automatable; perturbation -12.9 dB vs -90 dB limit |
| `fracture_enabled` | expansion-effect-harmonic-fracture | Dsp/Fx/HarmonicFracture.h | Y | Effects tab: Harmonic Fracture; 27/27 serialised, 2 distinct values; host-automatable; perturbation 0.2 dB vs -90 dB limit |
| `fracture_crossover_hz` | expansion-effect-harmonic-fracture | Dsp/Fx/HarmonicFracture.h | Y | Effects tab: Harmonic Fracture; 27/27 serialised, 4 distinct values; host-automatable; perturbation 0.6 dB vs -90 dB limit |
| `fracture_drive_low` | expansion-effect-harmonic-fracture | Dsp/Fx/HarmonicFracture.h | Y | Effects tab: Harmonic Fracture; 27/27 serialised, 4 distinct values; host-automatable; perturbation -20.6 dB vs -90 dB limit |
| `fracture_drive_mid` | expansion-effect-harmonic-fracture | Dsp/Fx/HarmonicFracture.h | Y | Effects tab: Harmonic Fracture; 27/27 serialised, 4 distinct values; host-automatable; perturbation -13.2 dB vs -90 dB limit |
| `fracture_drive_high` | expansion-effect-harmonic-fracture | Dsp/Fx/HarmonicFracture.h | Y | Effects tab: Harmonic Fracture; 27/27 serialised, 4 distinct values; host-automatable; perturbation -19.0 dB vs -90 dB limit |
| `fracture_symmetry` | expansion-effect-harmonic-fracture | Dsp/Fx/HarmonicFracture.h | Y | Effects tab: Harmonic Fracture; 27/27 serialised, 3 distinct values; host-automatable; perturbation -7.6 dB vs -90 dB limit |
| `fracture_band_mix` | expansion-effect-harmonic-fracture | Dsp/Fx/HarmonicFracture.h | Y | Effects tab: Harmonic Fracture; 27/27 serialised, 3 distinct values; host-automatable; perturbation -19.6 dB vs -90 dB limit |
| `fracture_comp_db` | expansion-effect-harmonic-fracture | Dsp/Fx/HarmonicFracture.h | Y | Effects tab: Harmonic Fracture; 27/27 serialised, 3 distinct values; host-automatable; perturbation -17.5 dB vs -90 dB limit |
| `fracture_mix` | expansion-effect-harmonic-fracture | Dsp/Fx/HarmonicFracture.h | Y | Effects tab: Harmonic Fracture; 27/27 serialised, 4 distinct values; host-automatable; perturbation 0.2 dB vs -90 dB limit |
| `furnace_enabled` | expansion-effect-formant-furnace | Dsp/Fx/FormantFurnace.h | Y | Effects tab: Formant Furnace; 27/27 serialised, 2 distinct values; host-automatable; perturbation -10.5 dB vs -90 dB limit |
| `furnace_vowel` | expansion-effect-formant-furnace | Dsp/Fx/FormantFurnace.h | Y | Effects tab: Formant Furnace; 27/27 serialised, 3 distinct values; host-automatable; perturbation -6.5 dB vs -90 dB limit |
| `furnace_spacing` | expansion-effect-formant-furnace | Dsp/Fx/FormantFurnace.h | Y | Effects tab: Formant Furnace; 27/27 serialised, 3 distinct values; host-automatable; perturbation -8.0 dB vs -90 dB limit |
| `furnace_resonance` | expansion-effect-formant-furnace | Dsp/Fx/FormantFurnace.h | Y | Effects tab: Formant Furnace; 27/27 serialised, 3 distinct values; host-automatable; perturbation -14.9 dB vs -90 dB limit |
| `furnace_env_amount` | expansion-effect-formant-furnace | Dsp/Fx/FormantFurnace.h | Y | Effects tab: Formant Furnace; 27/27 serialised, 2 distinct values; host-automatable; perturbation -9.9 dB vs -90 dB limit |
| `furnace_lfo_amount` | expansion-effect-formant-furnace | Dsp/Fx/FormantFurnace.h | Y | Effects tab: Formant Furnace; 27/27 serialised, 3 distinct values; host-automatable; perturbation -9.8 dB vs -90 dB limit |
| `furnace_mix` | expansion-effect-formant-furnace | Dsp/Fx/FormantFurnace.h | Y | Effects tab: Formant Furnace; 27/27 serialised, 3 distinct values; host-automatable; perturbation -10.5 dB vs -90 dB limit |
| `weight` | macro-mapping, macros-ui | Engine/SnaralEngine.cpp | Y | Main: macro knob; 27/27 serialised, 19 distinct values; host-automatable; perturbation -2.1 dB vs -90 dB limit |
| `growl` | macro-mapping, macros-ui | Dsp/Oscillators.h, Dsp/Texture.h, Engine/SnaralEngine.cpp | Y | Main: macro knob; 27/27 serialised, 21 distinct values; host-automatable; perturbation -2.4 dB vs -90 dB limit |
| `bite` | macro-mapping, macros-ui | Dsp/Contour.h, Dsp/Drive.h, Engine/SnaralEngine.cpp | Y | Main: macro knob; 27/27 serialised, 18 distinct values; host-automatable; perturbation -14.6 dB vs -90 dB limit |
| `motion` | macro-mapping, macros-ui | Dsp/Contour.h, Dsp/Fx/FormantFurnace.h, Engine/SnaralEngine.cpp | Y | Main: macro knob; 27/27 serialised, 16 distinct values; host-automatable; perturbation -7.0 dB vs -90 dB limit |
| `space` | macro-mapping, macros-ui | Dsp/Width.h | Y | Main: macro knob; 27/27 serialised, 6 distinct values; host-automatable; perturbation -6.7 dB vs -90 dB limit |
| `mid_source_a` | oscillator-bank | Dsp/Oscillators.h | Y | Sources tab: Mid Sources; 27/27 serialised, 4 distinct values; host-automatable; perturbation -6.1 dB vs -90 dB limit |
| `mid_source_b` | oscillator-bank | Dsp/Oscillators.h | Y | Sources tab: Mid Sources; 27/27 serialised, 4 distinct values; host-automatable; perturbation -2.7 dB vs -90 dB limit |
| `mid_blend` | oscillator-bank | Dsp/Oscillators.h | Y | Sources tab: Mid Sources; 27/27 serialised, 9 distinct values; host-automatable; perturbation -5.2 dB vs -90 dB limit |
| `mid_table_position` | oscillator-bank | Dsp/Contour.h, Dsp/Oscillators.h, Engine/SnaralEngine.cpp | Y | Sources tab: Mid Sources; 27/27 serialised, 4 distinct values; host-automatable; perturbation 1.5 dB vs -90 dB limit |
| `mid_pulse_width` | oscillator-bank | Dsp/Oscillators.h | Y | Sources tab: Mid Sources; 27/27 serialised, 5 distinct values; host-automatable; perturbation -1.1 dB vs -90 dB limit |
| `mid_tune_cents` | oscillator-bank | Dsp/Oscillators.h | Y | Sources tab: Mid Sources; 27/27 serialised, 2 distinct values; host-automatable; perturbation 0.7 dB vs -90 dB limit |
| `mid_level` | oscillator-bank | Engine/SnaralEngine.cpp | Y | Sources tab: Mid Sources; 27/27 serialised, 10 distinct values; host-automatable; perturbation -2.4 dB vs -90 dB limit |
| `mid_amp_attack_ms` | oscillator-bank | Engine/Voice.h | Y | Sources tab: Mid Envelope; 27/27 serialised, 5 distinct values; host-automatable; perturbation -10.4 dB vs -90 dB limit |
| `mid_amp_decay_ms` | oscillator-bank | Engine/Voice.h | Y | Sources tab: Mid Envelope; 27/27 serialised, 5 distinct values; host-automatable; perturbation -25.5 dB vs -90 dB limit |
| `mid_amp_sustain` | oscillator-bank | Engine/Voice.h | Y | Sources tab: Mid Envelope; 27/27 serialised, 5 distinct values; host-automatable; perturbation -6.2 dB vs -90 dB limit |
| `mid_amp_release_ms` | oscillator-bank | Engine/Voice.h, PluginProcessor.cpp | Y | Sources tab: Mid Envelope; 27/27 serialised, 5 distinct values; host-automatable; perturbation -8.8 dB vs -90 dB limit |
| `output_trim_db` | processing-output | Engine/SnaralEngine.cpp | Y | Output tab: Output; 27/27 serialised, 3 distinct values; host-automatable; perturbation -0.6 dB vs -90 dB limit |
| `limiter_enabled` | processing-output | Dsp/Limiter.h | Y | Output tab: Output; 27/27 serialised, 1 distinct values; host-automatable; perturbation 12.0 dB vs -90 dB limit |
| `limiter_ceiling_db` | processing-output | Dsp/Limiter.h | Y | Output tab: Output; 27/27 serialised, 1 distinct values; host-automatable; perturbation 9.2 dB vs -90 dB limit |
| `oversampling_mode` | expansion-oversampling | Engine/SnaralEngine.cpp | Y | Output tab: Quality; 27/27 serialised, 3 distinct values; host-automatable; perturbation 3.8 dB vs -90 dB limit |
| `sub_waveform` | sub-mid-engine | Dsp/Oscillators.h | Y | Sub tab: Sub Oscillator; 27/27 serialised, 2 distinct values; host-automatable; perturbation -14.0 dB vs -90 dB limit |
| `sub_tune_cents` | sub-mid-engine | Dsp/Oscillators.h | Y | Sub tab: Sub Oscillator; 27/27 serialised, 1 distinct values; host-automatable; perturbation 1.1 dB vs -90 dB limit |
| `sub_level` | sub-mid-engine | Engine/SnaralEngine.cpp | Y | Sub tab: Sub Oscillator; 27/27 serialised, 2 distinct values; host-automatable; perturbation -2.1 dB vs -90 dB limit |
| `sub_amp_attack_ms` | sub-mid-engine | Engine/Voice.h | Y | Sub tab: Sub Envelope; 27/27 serialised, 2 distinct values; host-automatable; perturbation -8.1 dB vs -90 dB limit |
| `sub_amp_decay_ms` | sub-mid-engine | Engine/Voice.h | Y | Sub tab: Sub Envelope; 27/27 serialised, 3 distinct values; host-automatable; perturbation -8.0 dB vs -90 dB limit |
| `sub_amp_sustain` | sub-mid-engine | Engine/Voice.h | Y | Sub tab: Sub Envelope; 27/27 serialised, 3 distinct values; host-automatable; perturbation -4.1 dB vs -90 dB limit |
| `sub_amp_release_ms` | sub-mid-engine | Engine/Voice.h, PluginProcessor.cpp | Y | Sub tab: Sub Envelope; 27/27 serialised, 5 distinct values; host-automatable; perturbation 1.0 dB vs -90 dB limit |
| `anchor_enabled` | expansion-sub-anchor | Engine/SubAnchor.h | Y | Sub tab: Sub Anchor; 27/27 serialised, 2 distinct values; host-automatable; perturbation -3.6 dB vs -90 dB limit |
| `anchor_note` | expansion-sub-anchor | Engine/SubAnchor.h | Y | Sub tab: Sub Anchor; 27/27 serialised, 1 distinct values; host-automatable; perturbation -3.6 dB vs -90 dB limit |
| `anchor_strength` | expansion-sub-anchor | Engine/SubAnchor.h | Y | Sub tab: Sub Anchor; 27/27 serialised, 2 distinct values; host-automatable; perturbation -3.6 dB vs -90 dB limit |
| `traj_enabled` | expansion-snarl-trajectory | Dsp/Contour.h, Dsp/Trajectory.h, Engine/SnaralEngine.cpp, Engine/Voice.h | Y | Filter & Motion tab: Snarl Trajectory; 27/27 serialised, 2 distinct values; host-automatable; perturbation -14.0 dB vs -90 dB limit |
| `traj_time_mode` | expansion-snarl-trajectory | Dsp/Trajectory.h, Engine/SnaralEngine.cpp | Y | Filter & Motion tab: Snarl Trajectory; 27/27 serialised, 2 distinct values; host-automatable; perturbation -24.5 dB vs -90 dB limit |
| `traj_length_ms` | expansion-snarl-trajectory | Dsp/Trajectory.h | Y | Filter & Motion tab: Snarl Trajectory; 27/27 serialised, 3 distinct values; host-automatable; perturbation -9.6 dB vs -90 dB limit |
| `traj_length_div` | expansion-snarl-trajectory | Dsp/Trajectory.h | Y | Filter & Motion tab: Snarl Trajectory; 27/27 serialised, 1 distinct values; host-automatable; perturbation -9.4 dB vs -90 dB limit |
| `traj_dest` | expansion-snarl-trajectory | Dsp/Contour.h, Dsp/Trajectory.h, Engine/SnaralEngine.cpp | Y | Filter & Motion tab: Snarl Trajectory; 27/27 serialised, 3 distinct values; host-automatable; perturbation -15.5 dB vs -90 dB limit |
| `traj_depth` | expansion-snarl-trajectory | Dsp/Contour.h, Dsp/Trajectory.h, Engine/SnaralEngine.cpp | Y | Filter & Motion tab: Snarl Trajectory; 27/27 serialised, 4 distinct values; host-automatable; perturbation -6.7 dB vs -90 dB limit |
| `traj_curve` | expansion-snarl-trajectory | Dsp/Trajectory.h | Y | Filter & Motion tab: Snarl Trajectory; 27/27 serialised, 3 distinct values; host-automatable; perturbation -36.1 dB vs -90 dB limit |
| `traj_loop` | expansion-snarl-trajectory | Dsp/Trajectory.h | Y | Filter & Motion tab: Snarl Trajectory; 27/27 serialised, 1 distinct values; host-automatable; perturbation -10.5 dB vs -90 dB limit |
| `traj_x2` | expansion-snarl-trajectory | Dsp/Trajectory.h | Y | Filter & Motion tab: trajectory editor point; 27/27 serialised, 2 distinct values; state only (not automatable); perturbation -10.1 dB vs -90 dB limit |
| `traj_x3` | expansion-snarl-trajectory | Dsp/Trajectory.h | Y | Filter & Motion tab: trajectory editor point; 27/27 serialised, 2 distinct values; state only (not automatable); perturbation -12.2 dB vs -90 dB limit |
| `traj_x4` | expansion-snarl-trajectory | Dsp/Trajectory.h | Y | Filter & Motion tab: trajectory editor point; 27/27 serialised, 2 distinct values; state only (not automatable); perturbation -19.8 dB vs -90 dB limit |
| `traj_y1` | expansion-snarl-trajectory | Dsp/Trajectory.h | Y | Filter & Motion tab: trajectory editor point; 27/27 serialised, 2 distinct values; state only (not automatable); perturbation -18.1 dB vs -90 dB limit |
| `traj_y2` | expansion-snarl-trajectory | Dsp/Trajectory.h | Y | Filter & Motion tab: trajectory editor point; 27/27 serialised, 2 distinct values; state only (not automatable); perturbation -11.3 dB vs -90 dB limit |
| `traj_y3` | expansion-snarl-trajectory | Dsp/Trajectory.h | Y | Filter & Motion tab: trajectory editor point; 27/27 serialised, 2 distinct values; state only (not automatable); perturbation -9.5 dB vs -90 dB limit |
| `traj_y4` | expansion-snarl-trajectory | Dsp/Trajectory.h | Y | Filter & Motion tab: trajectory editor point; 27/27 serialised, 2 distinct values; state only (not automatable); perturbation -12.5 dB vs -90 dB limit |
| `traj_y5` | expansion-snarl-trajectory | Dsp/Trajectory.h | Y | Filter & Motion tab: trajectory editor point; 27/27 serialised, 2 distinct values; state only (not automatable); perturbation -13.4 dB vs -90 dB limit |
| `bypass` | cdl-common-actions, parameter-state | Engine/SnaralEngine.cpp | Y | Main action row: Bypass; not in presets (project state); host-automatable; perturbation 0.0 dB vs -90 dB limit |
| `layer_solo` | cdl-common-actions, parameter-state | Engine/SnaralEngine.cpp | Y | Advanced header: solo/mute; not in presets (project state); host-automatable; perturbation -2.1 dB vs -90 dB limit |
| `sub_mute` | cdl-common-actions, parameter-state | Engine/SnaralEngine.cpp | Y | Advanced header: solo/mute; not in presets (project state); host-automatable; perturbation -2.1 dB vs -90 dB limit |
| `mid_mute` | cdl-common-actions, parameter-state | Engine/SnaralEngine.cpp | Y | Advanced header: solo/mute; not in presets (project state); host-automatable; perturbation -2.4 dB vs -90 dB limit |
| `voice_mode` | midi-voice | Engine/SnaralEngine.cpp | Y | Voice tab: Voice Mode; 27/27 serialised, 2 distinct values; host-automatable; perturbation 0.0 dB vs -90 dB limit |
| `legato` | midi-voice | Engine/SnaralEngine.cpp | Y | Voice tab: Voice Mode; 27/27 serialised, 2 distinct values; host-automatable; perturbation -16.8 dB vs -90 dB limit |
| `glide_ms` | midi-voice | Engine/Voice.h | Y | Voice tab: Voice Mode; 27/27 serialised, 4 distinct values; host-automatable; perturbation 2.4 dB vs -90 dB limit |
| `pitch_bend_range` | midi-voice | Engine/SnaralEngine.cpp | Y | Voice tab: Pitch & Tuning; 27/27 serialised, 1 distinct values; host-automatable; perturbation 2.1 dB vs -90 dB limit |
| `velocity_to_level` | midi-voice | Engine/Voice.h | Y | Voice tab: Velocity; 27/27 serialised, 4 distinct values; host-automatable; perturbation -5.8 dB vs -90 dB limit |
| `velocity_to_attack` | midi-voice | Engine/Voice.h | Y | Voice tab: Velocity; 27/27 serialised, 2 distinct values; host-automatable; perturbation -20.2 dB vs -90 dB limit |
| `master_tune_hz` | midi-voice | Engine/Voice.h | Y | Voice tab: Pitch & Tuning; 27/27 serialised, 1 distinct values; host-automatable; perturbation 3.2 dB vs -90 dB limit |
| `transpose_semitones` | midi-voice | Engine/Voice.h | Y | Voice tab: Pitch & Tuning; 27/27 serialised, 1 distinct values; host-automatable; perturbation 7.2 dB vs -90 dB limit |

## 3. Preset coverage (step 4): PASS

- All 27 factory presets serialise every non-`pfNoPreset` ID and round-trip (`FactoryPresetTests`, `PresetFormatTests`).
- Enable switches: `furnace_enabled` (Neuro Growl, Vowel Talker), `fracture_enabled` (Neuro Growl, Tearout, Splinter), `traj_enabled` (Throat Sweep, Folded Jaw, Trajectory Snarl), `anchor_enabled` (Clean Sub) are each on in at least one preset (`WiringPerturbationTests` checks all four).
- Choice options: after this audit every choice option is used by at least one preset. `contour_shape=ease_in` (Saw Rasp) and `traj_dest=source_position` (Folded Jaw) were the two unused options and the consistency check warned about them; the warnings are gone (section 5).

## 4. Non-parameter features (step 6)

| Feature | Spec | Code location | Wired | Evidence / notes |
|---|---|---|---|---|
| Sustain CC64, CC120, CC123, pitch bend, velocity | performance-foundation, midi-voice | `Engine/VoiceAllocator.h`, `Engine/Voice.h`, `PluginProcessor.cpp` (`handleMidiAndRender`), reserved list `Core/MidiLearn.cpp` (`isReservedCc`) | Y | VoiceAllocatorTests, VoiceAllocatorFuzzTests, SnaralEngineTests (`testSustainPedal`), MidiLearnTests, wiring scenarios for bend and velocity |
| Panic (both buttons, clears pedal, stops reference tone) | performance-foundation, cdl-common-actions | `EditorController::panic`, `MainView.cpp` (`panicBtn`, `panic2Btn`), `PluginProcessor.cpp` (panic flag to `engine.panic`), Ctrl/Cmd+Period in `PluginEditor::keyPressed` | Y | SnaralEngineTests `testPanic`, ProcessorTests `testBypassAndPanic`, EditorTests |
| Reset, Randomize, Reproduce, locks | cdl-common-actions | `Core/Randomizer.*`, `MainView.cpp` action row, `EditorController` | Y | RandomizeTests (100k seeds, lock independence, exclusions), EditorTests |
| Bypass including host bypass | cdl-common-actions, host-integration | `PluginProcessor::getBypassParameter` returns `bypass`; engine 10 ms ramp | Y | ProcessorTests, SnaralEngineTests `testBypass`, wiring harness (`bypass` row) |
| Undo, Redo, A/B, Copy A to B | expansion-undo-ab | `State/Pure/UndoHistory.*`, `MainView.cpp`, Ctrl/Cmd+Z/Y in `PluginEditor::keyPressed` | Y | UndoHistoryTests, UndoFuzzTests, EditorTests |
| Variation slots A to D, active slot | preset-variation | `State/Pure/VariationBank.*`, Main performance row, project state | Y | VariationBankTests, ProjectStateTests, EditorTests |
| Preset selector, browser, favourites, audition | preset-variation, expansion-preset-browser | `State/Pure/PresetBrowserModel.*`, `PresetLibrary.*`, `UI/Views/PresetBrowserPanel.cpp`, `HeaderBar.cpp` | Y | PresetBrowserTests, PresetLibraryTests, EditorTests |
| Save, import, export presets | preset-file-contract | `State/Pure/PresetFormat.*`, `AtomicFile.*`, `EditorController` | Y | PresetFormatTests, FuzzParsersTests, fixtures in `tests/fixtures` |
| MIDI CC Learn and mapping list | expansion-midi-cc-learn | `Core/MidiLearn.*`, `Engine/CcHook.h`, `UI/Views/MidiMapPanel.cpp`, context menus | Y | MidiLearnTests, CcHookTests, ProcessorTests `testMidiLearn` |
| Export WAV | cdl-offline-wav-export | `UI/Views/ExportDialog.cpp`, `Core/OfflineRenderer.h`, `Core/WavWriter.*` | Y | WavExportTests; QA render of all presets through the same renderer |
| Themes, tooltips, animation, onboarding rerun | cdl-theme-options, cdl-help-and-onboarding | `UI/Theme.*`, `UI/OptionsPanel.*`, `UI/UiOptions.*`, `UI/OnboardingOverlay.*` | Y | UiKitTests (token values, contrast, persistence outside state) |
| Reference tone (standalone only) | cdl-theme-options, host-integration | `Core/ReferenceTone.h` (`Mode::Plugin` never emits), `OptionsPanel` button injected only when standalone | Y | ReferenceToneTests; plug-in builds cannot start it |
| Signal scope and CPU gauge | cdl-signal-visualization | `Engine/EngineDiagnostics.h`, `UI/Views/ProcessorSources.h`, `UI/Displays.*`, Options switches | Y (fixed) | Sub and Mid scope taps and the measured overrun marker were missing, fixed (section 5); SnaralEngineTests `testDiagnostics`, UiKitTests |
| Help content | cdl-help-and-onboarding | `UI/HelpContent.cpp` | Y | consistency check (help text per registry ID); the deferred features (MPE, Variation Morph, Motion Snapshot, Readability Monitor, Preset Exchange, Spectral Shard, Clockwork Gate, Inharmonic Striker) appear only under "Not available yet" |
| Tempo status, latency readout, status line | host-integration, macros-ui | `PluginEditor::updateStatusChips`, `StatusBar.*`, `AdvancedView.cpp` (latency) | Y (fixed) | status line ignored the `tempoUnavailable` flag and had no overrun message, fixed (section 5) |
| Oversampling latency report | expansion-oversampling, host-integration | `PluginProcessor::prepareToPlay`, `applyPendingLatency`, `Dsp/Oversampler.h` | Y | SnaralEngineTests `testOversamplingLatencyAndAlignment`, OversamplerTests, ProcessorTests `testLatencyAndTail` |
| Formats and packaging | expansion-format-support, release-packaging | `CMakeLists.txt` (`FORMATS` VST3, Standalone, AU on macOS, LV2 on Linux, CLAP via clap extensions), `.github/workflows/build.yml`, `tools/package.sh`, `tools/smoke.sh` | Y (Linux), UNVERIFIED (Windows, macOS) | Linux VST3/CLAP/LV2/Standalone built and validated here (see QA_REPORT); Windows and macOS cells are built by CI only |
| Added effect: Formant Furnace | expansion-effect-formant-furnace | `Dsp/Fx/FormantFurnace.h`, `Engine/SnaralEngine.cpp` mid chain (Furnace, Fracture, Drive, Width), Effects tab | Y | reachable in the Effects tab, audible (`furnace_enabled` and all 6 knobs wired, section 2), FormantFurnaceTests, presets Neuro Growl and Vowel Talker |
| Added effect: Harmonic Fracture | expansion-effect-harmonic-fracture | `Dsp/Fx/HarmonicFracture.h`, `SnaralEngine.cpp` (oversampled per band shaping), Effects tab | Y | reachable, audible (all 9 IDs wired), HarmonicFractureTests, presets Neuro Growl, Tearout, Splinter |
| Snarl Trajectory editor | expansion-snarl-trajectory | `Dsp/Trajectory.h`, `UI/TrajectoryEditor.*`, Filter & Motion tab | Y | TrajectoryTests, 8 point IDs wired, UiKitTests; presets use ms and sync modes, both and cutoff and source-position destinations |
| Undo history (100 steps), extended A/B | expansion-undo-ab | see above | Y | UndoHistoryTests |
| Sub Anchor | expansion-sub-anchor | `Engine/SubAnchor.h`, Sub tab | Y | SubAnchorTests, 3 IDs wired |
| Nonlinear oversampling Off/2x/4x with latency | expansion-oversampling | `Dsp/Oversampler.h`, engine `applyOversampling`, Output tab Quality | Y | OversamplerTests, wiring harness |
| 24+ factory presets | factory-presets | `Resources/presets/*.json` (27), `tools/gen_presets.py` | Y | FactoryPresetTests, QA render of all 27 |
| Macro mapping (Weight, Growl, Bite, Motion, Space) | macro-mapping | `Parameters/MacroMappings.h`, `SnaralEngine.cpp`, `Dsp/Contour.h`, `Dsp/Fx/FormantFurnace.h` | Y | SnaralEngineTests (`testWeightGrowl`, `testMotionBite`), BughuntRegressionTests |
| Project state save and restore (parameters, slots, A/B, mappings, UI tree) | parameter-state, host-integration | `State/ProjectState.*`, `PluginProcessor::getStateInformation / setStateInformation` | Y | ProjectStateTests, ProcessorTests `testStateRoundTrip`, EditorTests `testProjectStateRoundTrip` |
| Post-beta features (MPE, Variation Morph, Motion Snapshot, Readability Monitor, Preset Exchange, Spectral Shard, Clockwork Gate, Inharmonic Striker, Performance Gesture Map) | IMPLEMENTATION_PLAN scope | none | N/A by design | Absent from the UI; the help page lists them as not available yet. MPE messages (channel pressure, poly pressure) are accepted by the processor and ignored by the engine |

## 5. Gaps found and fixed

| # | Class | Gap | Fix | Test |
|---|---|---|---|---|
| 1 | spec gap | Scope offered the Output tap only, Sub and Mid views (cdl-signal-visualization) were disabled | `EngineDiagnostics` has two more lock-free rings (sub after delay alignment, mid after Width, both before trim and limiter), `DiagnosticsSources` serves them | SnaralEngineTests `testDiagnostics` (sub-only render: mid tap silent) |
| 2 | spec gap | CPU gauge overrun marker fired on the smoothed load above 100 %, not on a measured over-deadline call | engine counts render calls (32 samples or more) slower than their audio; the gauge and a new status chip use that counter | none automated (a real over-deadline call cannot be forced deterministically); code reviewed, counter is a plain atomic increment |
| 3 | spec gap | Status line ignored the `tempoUnavailable` flag (only the "never seen a tempo" case was shown) and had no engine overrun message | `PluginEditor::updateStatusChips` shows tempo chip whenever Sync is requested and the host tempo is missing, plus an overrun chip for 5 s | EditorTests `testStatusChips` (tempo chip appears with Sync and no tempo, clears in free mode); the overrun chip is not automated |
| 4 | spec deviation | `isBusesLayoutSupported` accepted a mono output; host-integration requires one stereo output and no input, refused not downmixed | stereo output with no input only (processBlock still tolerates a mono buffer defensively) | ProcessorTests `testMonoBusAndBlockSizes` now asserts the refusal |
| 5 | NO-PRESET (choice) | `contour_shape=ease_in` and `traj_dest=source_position` unused by the factory set | Saw Rasp uses ease_in, Folded Jaw gets a short source-position trajectory | consistency check warnings gone, FactoryPresetTests |
| 6 | missing harness | The checklist names a perturbation harness, registry-vs-table matcher and host parameter list check, none existed | `tests/WiringPerturbationTests.cpp` (registry walk, 99-ID perturbation, sub-centred invariant under every parameter move) and `ProcessorTests::testHostParameterList` | new tests |
| 8 | wrong latency in export | `OfflineRenderer` read the engine latency before applying the patch: Oversampling x2 / x4 presets exported 46 to 56 samples late (QA bug Q6) | patch applied before the latency is read | `FactoryRenderQaTests` latency-trim check |
| 7 | stale doc | `docs/release/WIRING.md` was out of date (Motion consumer list) | regenerated by `tools/gen_wiring.py` | consistency check |

## 6. Open items, exceptions and UNVERIFIED rows

- UNVERIFIED: Windows and macOS builds and host matrix (Reaper, Ableton, FL Studio, Logic, Bitwig): no such machines here, CI builds them. Linux standalone and VST3 are covered in QA_REPORT.
- Decision recorded: the 8 Trajectory point IDs are `pfNoAutomation`. JUCE exposes them to the host with the "not automatable" flag, the checklist wording "absent from the list" cannot be achieved with the APVTS (they must live in the same state tree). Not a FAIL, needs an approved exception from Sam or Jurgens only if a host lists them badly.
- Decision recorded: channel and poly pressure are forwarded to the engine and ignored (MPE is post-beta).
- Factory presets are hot: 19 of 27 sit at an active RMS between -5.6 and -9 dBFS with peaks within 3.6 dB of full scale (crest 3.6 to 6 dB). They pass the QA thresholds, but a quieter default level (output_trim_db around -3) is worth a decision before the beta; see QA_REPORT.
- `AtomicCpuSource` in `UI/UiSources.h` still derives overrun from a per-block ratio (used by the UI kit tests only); the product path uses the engine counter.

## 7. Cross-cutting invariants (final candidate)

| Invariant | Result | Evidence |
|---|---|---|
| Sub sample-identical L/R for every parameter combination, Space, solo/mute/effects, every oversampling mode | PASS | `WiringPerturbationTests` (sub-solo render, notes 36 and 60, min / mid / max of all 99 IDs on the audit patch), SnaralEngineTests `testSubCentered`, SnaralEngineStressTests, QA render (all 27 presets, sub solo L == R bit exact) |
| No allocation, lock or file access in `process` | PASS | allocation counter in SnaralEngineTests `testNoAllocationAndDeterminism`, OversamplerTests, ContourTests and others; the new engine code adds only atomics, a clock read and ring writes |
| Reported latency equals measured latency; sub and mid aligned within one sample | PASS | SnaralEngineTests `testOversamplingLatencyAndAlignment`, OutputStageTests (limiter), QA render trims 50 samples at 48 kHz |
| Parameter IDs unchanged from the baseline | PASS | consistency check against `tests/fixtures/param_ids_baseline.txt` |
| Deferred features absent from UI and help or labelled unavailable | PASS | `HelpContent.cpp` "Not available yet" paragraph; no UI control for them |
