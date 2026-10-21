# Bug hunt log

Branch `feat/bughunt`. Sanitizer build: all JUCE-free tests under `-fsanitize=address,undefined` (32/32 passed).

| ID | Bug | File | Fix | Test |
|---|---|---|---|---|
| 001 | MIDI learn table read on the audio thread raced with publish() (heap-use-after-free under ASan) | Source/Core/MidiLearn.{h,cpp} | acquire()/release() reader guard; publish waits for readers before freeing | MidiLearnTests |
| 002 | SvfFilter::prepare() kept stale coefficients after a sample-rate change | Source/Dsp/SvfFilter.h | force coefficient rebuild | BughuntRegressionTests |
| 003 | Motion macro did not scale furnace_env/lfo_amount (macro-mapping spec) | Source/Dsp/Fx/FormantFurnace.h | scale in params decoder | BughuntRegressionTests |
| 004 | Pack ids / file names could be Windows device names or end in a dot | Source/State/Pure/PresetPack.cpp | reject | BughuntRegressionTests |
| 005 | clampf passed NaN through; NaN pulse width indexed outside the correlation table (OOB read) | Source/Dsp/DspUtil.h | NaN maps to lo | BughuntRegressionTests, DspRobustnessTests |
| 006 | UI typed/pasted fractional values for integer params were rounded silently | Source/UI/ParamFormat.cpp | reject | UiKitTests |
| 007 | Copy value copied rounded display text instead of the plain domain number; choice index paste rejected | Source/UI/{ParamFormat,MacroKnob,ParamControls}.cpp | plainDomainValueText | UiKitTests |
| 008 | Trajectory editor min gap 0.01 vs DSP 0.02 and a different preview curve than the DSP | Source/UI/TrajectoryEditor.* | gap 0.02 (static_assert), preview uses TrajectoryCurve | UiKitTests |
| 009 | Offline render applied mapped CCs at block start, not at the event sample | Source/Core/OfflineRenderer.h | apply at the event | WavExportTests |
| 010 | Log-scale parameter range lambdas unbounded (NaN/inf for out-of-range input) | Source/Parameters/JuceParameterLayout.cpp | clamp | (JUCE build) |
| 011 | Truncated project-state blob decoded as a shorter valid document | Source/State/ProjectState.cpp | require full declared payload | ProjectStateTests |
| 012 | LV2 build error: no valid LV2 URI | CMakeLists.txt | LV2URI set | JUCE build |
| 013 | Lfo float-conversion warning | Source/Dsp/Lfo.h | cast | -Wconversion clean |

Also added: tools/consistency_check.py (ctest `snaral_consistencycheck`), tests/fixtures/param_ids_baseline.txt, FuzzParsersTests, DspRobustnessTests, VoiceAllocatorFuzzTests, UndoFuzzTests.

Consistency warnings (not failures): factory presets never use contour_shape=ease_in or traj_dest=source_position.

## Found, not fixed (owner)
- engine-integration: the engine must use `TablePublisher::acquire()/release()` (ReadGuard) per block instead of `current()`.
- engine-integration: confirm SnaralEngine passes `MotionRouting`/Furnace modulation with Motion already applied once (Furnace params now include Motion; do not scale again).
- engine-integration: SnaralEngine should call `Adsr`/`Smoother` with the same sample rate in prepare(); not reviewed (out of scope files).
- spec: sub attack/release ranges in sub-mid-engine.md differ from the parameter table (documented there; table authoritative).
