# Feature Spec: Macro Mapping

**Module:** macros-ui / parameter-state  
**Depends on:** parameter-state, sub-layer, mid-source, motion-filter, mid-processing

## Objective

Define predictable, testable mappings for the five main controls. Each macro is a host parameter and a single source of user intent; DSP destinations derive from its value and the saved Advanced baseline.

## Mapping model

For normalized macro input x in [0,1], use these rules. Gain curves are amplitude curves, not dB percentages. Mapping functions are versioned as part of patch behavior.

| Macro | DSP mapping | User-visible result |
|---|---|---|
| Weight | Sub multiplier = x². Final sub gain = subLevel × multiplier. | 0 is silent; 0.5 is one quarter amplitude relative to the sub-level baseline; 1 reaches baseline. |
| Growl | Mid multiplier = x². Source character bias = up to +0.20 toward Source B, added to the Advanced blend and clamped to [0,1]. | 0 mutes mid and texture; increasing it raises mid level and gradually emphasizes the alternate source. |
| Bite | Adds up to +18 semitones of filter-envelope peak offset and +12 dB of drive amount to Advanced baselines, each clamped to validated limits. | Adds sharper emphasis and controlled harmonic density. |
| Motion | Multiplies contour depth and LFO depth by x. | 0 stops modulation; 1 reaches Advanced depth settings. |
| Space | Maps x to mid width percentage 0–120%. Width=0 is dual-mono. Sub is never included. | Opens mid stereo width without moving the sub. |

The Weight and Growl curves intentionally leave lower knob travel useful for fades. Their exact derivatives are tested and kept stable after presets ship.

During the current sine-only bootstrap, Weight scales the sole prototype voice as the temporary sub signal. This is a development bridge, not the final sub/mid engine. Growl, Bite, Motion, and Space stay host-automatable but do not claim audible destinations until the corresponding mid-source, processing, modulation, and stereo modules are implemented.

## Advanced interaction

Advanced controls are baseline values. Macro destinations are additive or multiplicative as shown above and clamped after composition. The UI displays baseline values in Advanced; a small macro influence indicator shows active contribution when the user changes a related control. Do not rewrite the baseline when a macro moves.

Preset and project state serialize both macro values and Advanced baselines. At render time the final destination is computed from both. Macro automation must not cause Advanced baseline values to drift.

## Smoothing

- Weight and Growl gain multipliers: 10 ms one-pole or smoother with equivalent click-free behavior.
- Bite filter/drive contribution: 10 ms continuous smoothing.
- Motion depth: 10 ms; LFO phase remains continuous.
- Space: 20 ms crossfade between mono and width path.
- Macro changes at block boundaries must not exceed a single-sample discontinuity greater than −60 dBFS on a −12 dBFS steady sine fixture.

## Acceptance criteria

1. Each table mapping is verified at 0, 0.25, 0.5, 0.75, 1.0 within 1e-6 in normalized destination space.
2. Weight and Growl are monotonic and equal zero at x=0.
3. Growl=0 silences mid and texture; Weight=0 silences sub.
4. Bite contribution is bounded at all parameter combinations and never changes sub output.
5. Motion=0 removes both contour and LFO modulation without stopping voice pitch.
6. Space=0 is mono mid; at every setting the sub output is sample-identical L/R.
7. UI and DAW automation execute the same mapping and cause no baseline parameter drift.
