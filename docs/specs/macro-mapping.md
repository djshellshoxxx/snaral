# Feature Spec: Macro Mapping

**Module:** macros-ui / parameter-state  
**Depends on:** parameter-state, sub-mid-engine, motion-filter, processing-output  
**Implementation:** `Source/Parameters/MacroMappings.h` (pure functions, JUCE-free); consumed by the engine when building per-block destinations

## Objective

Define predictable, testable mappings for the five main controls. Each macro is a host parameter and one source of user intent; DSP destinations derive from its value and the saved Advanced baseline. Mapping functions are part of patch behaviour: once presets ship they change only with a version bump and migration note.

## Input convention

Macro IDs: `weight`, `growl`, `bite`, `motion`, `space` (domain 0 to 100, host-normalised 0 to 1). `x = clamp(value/100, 0, 1)`; non-finite `x` is treated as 0. Gain curves are amplitude curves, not dB percentages.

## Mapping table

| Macro | Function of x | Destination (baseline parameter it composes with) |
|---|---|---|
| Weight | `weightSubMultiplier = x^2` | Final sub gain = `sub_level/100 * x^2`, then sub amp ADSR, then Sub Anchor gain. Mid is unaffected. |
| Growl | `growlMidMultiplier = x^2`; `growlSourceBBias = 0.20 * x` | Mid and texture (noise, metal) gain = `mid_level/100 * x^2`. Effective blend = `clamp(mid_blend/100 + 0.20*x, 0, 1)`. Sub unaffected. |
| Bite | `biteFilterPeakOffsetSemitones = 18 * x`; `biteDriveOffsetDb = 12 * x` | Adds up to 18 semitones to the contour peak target (cutoff multiplied by `2^(offset/12)` at the contour peak, final cutoff clamped to 20 Hz to min(20 kHz, 0.45*fs)). Adds up to 12 dB to the drive stage pre-gain, never above the drive stage's own maximum (the pre-gain at `drive_amount = 100`). Mid-only. |
| Motion | `motionDepthMultiplier = x` | Multiplies `contour_depth` and `lfo_depth`. Beta extension: also multiplies `traj_depth`, `furnace_env_amount` and `furnace_lfo_amount`. Pitch, sub and static parameters are unaffected. |
| Space | `spaceWidthPercent = 120 * x` | Mid stereo width, 0 to 120 percent. Width 0 is dual-mono mid. The sub is never routed through width. |

Consequences stated so they are not rediscovered as bugs: Bite's filter part is a contour peak offset, so it is audible only while the contour has depth (Motion above 0); at Motion 0 Bite changes drive only. Weight and Growl are deliberately steep (x squared), leaving the lower knob travel useful for fades.

## Composition rules

1. The engine reads Advanced baselines and macros from the same `ParamSnapshot`. Destination = `compose(baseline, macro)` per the table, computed per block, then clamped to the destination's valid range, then smoothed.
2. The baseline is never rewritten when a macro moves. State stores both macros and baselines; automation of a macro cannot drift a baseline.
3. Order inside the mid chain is fixed: sources and blend, filter (contour, LFO, trajectory, Bite offset), amp ADSR, Furnace, Fracture, drive (Bite pre-gain), width (Space). The sub path is `sub osc, sub amp ADSR, anchor gain` and uses only Weight.
4. UI and host automation call the same functions. The UI shows a small macro-influence indicator on a related Advanced control while the macro contribution is non-zero.

## Smoothing

- Weight and Growl multipliers, Bite contributions, Motion depth: 10 ms one-pole or equivalent; LFO phase stays continuous.
- Space: 20 ms crossfade between mono and width path.
- A macro step at a block boundary must not create a single-sample discontinuity above -60 dBFS on a -12 dBFS steady-sine fixture.
- Preset load and A/B switch adopt all destinations together at a block boundary and are smoothed by the same rules.

## Edge cases

- `x` outside [0,1] or non-finite is clamped or zeroed before any mapping.
- Bite with `drive_amount = 100` and x = 1 stays at the drive maximum; with the limiter disabled the output stays finite.
- Growl bias saturates at blend 1.0; blend is never negative.
- Macro mapping does not depend on voice count or note.

## Acceptance criteria

1. Every function reproduces the vector table below within 1e-6.
2. Weight and Growl multipliers are monotonic and exactly 0 at x = 0.
3. Growl = 0 silences mid and texture; Weight = 0 silences sub (isolated-layer renders).
4. Bite contribution is bounded for all baseline combinations and never changes sub samples.
5. Motion = 0 removes contour, LFO, trajectory and Furnace modulation depth without changing pitch.
6. Space = 0 gives identical L/R mid; at every Space the sub is sample-identical L/R.
7. UI edit and host automation of the same normalised value give identical destination values; no baseline drift after 1,000 automation writes.

## Test vectors

| x | Weight / Growl multiplier | Growl bias | Bite peak st | Bite drive dB | Motion mult | Space width % |
|---|---|---|---|---|---|---|
| 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| 0.25 | 0.0625 | 0.05 | 4.5 | 3 | 0.25 | 30 |
| 0.5 | 0.25 | 0.10 | 9 | 6 | 0.5 | 60 |
| 0.75 | 0.5625 | 0.15 | 13.5 | 9 | 0.75 | 90 |
| 1 | 1 | 0.20 | 18 | 12 | 1 | 120 |

Fixtures: the existing macro tests (`tests/ParameterContractTests.cpp`), a clamp case (blend 0.9 plus bias), non-finite inputs, and the -60 dBFS step fixture.
