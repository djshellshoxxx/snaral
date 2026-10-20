# Feature Spec: Variation Morph

**Category:** Usability  
**Depends on:** preset-variation, parameter-state, macros-ui

## Objective
Move continuously among the four named snapshots without losing exact snapshot recall.

## Behavior
- Add a Morph control with A/B/C/D target selection and normalized 0–100% travel between adjacent slots.
- All continuous normalized parameters interpolate linearly in perceptual parameter space where declared (for example, Hz logarithmically and dB linearly).
- Enum parameters switch at 50% of the segment. Boolean parameters switch at the midpoint with hysteresis to avoid flicker during automation.
- Morphing excludes meter state, current MIDI notes, oscillator phase, and UI-only dirty indicators.
- Each slot remains unchanged during morph. An explicit “Commit morph” writes the current interpolated state as a new variation.
- Empty slots are unavailable destinations and show a clear explanation. Sub gain is interpolated with a bounded curve; sub remains mono throughout.

## Acceptance
1. At each slot endpoint the state matches the saved slot within 1e-6.
2. Continuous values are monotonic through morph travel; enum/boolean behavior matches stated rules.
3. Interpolation causes no partial-block state, parameter jumps outside smoothing limits, or sub widening.
4. Morph does not mutate the source slots unless Commit is selected.
5. Project save/reopen restores slots and current morph position.
