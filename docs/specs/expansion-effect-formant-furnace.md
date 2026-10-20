# Effect Spec: Formant Furnace

**Category:** Candidate effect  
**Depends on:** motion-filter, mid-processing

## Objective
Create moving vowel-like and throat-like emphasis from a small bank of bounded resonant peaks.

## Controls
Vowel region, peak spacing, resonance, contour amount, LFO amount, wet mix, enable.

## Processing contract
- Two to four fixed resonant peaks; no user-defined resonator count in v1.
- Peaks move together through a curated range and may be offset by spacing.
- No self-oscillation or feedback.
- Mid-only; sub bypass is structural, not a parameter choice.
- Level compensation is enabled by default. Effect is off in initialized patch.

## Acceptance
1. All control combinations produce finite output with bounded peak level.
2. Sweeping vowel region produces continuous spectral movement without zipper noise.
3. Disabling effect returns dry mid within numerical tolerance.
4. Sub remains sample-identical under all effect states.
5. Mono fold-down review finds no complete cancellation on validation patches.
