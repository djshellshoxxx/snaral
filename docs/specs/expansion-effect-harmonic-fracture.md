# Effect Spec: Harmonic Fracture

**Category:** Candidate effect  
**Depends on:** mid-processing, oversampling

## Objective
Create a sharply segmented, aggressive mid tone by applying distinct nonlinear curves to a few frequency bands.

## Controls
Crossover frequency, low/mid/high drive, fold symmetry, band blend, output compensation, oversampling mode, wet mix.

## Processing contract
- Three bands maximum, with stable crossover filters and smoothed frequency changes.
- Minimum processing frequency is configurable but cannot include the protected sub layer.
- Each band uses bounded waveshaping; no feedback.
- Default compensation attempts constant perceived level over normal drive range; does not claim exact loudness.
- Optional oversampling follows the oversampling feature specification.

## Acceptance
1. Crossover sweeps remain finite and do not create persistent notches or excessive peaks on fixtures.
2. DC offset remains below −90 dBFS in steady-state tests.
3. Full control sweeps produce no NaN/Inf and stay below the defined ceiling with limiter enabled.
4. Dry/wet at zero returns dry path; dry and wet remain latency aligned.
5. Sub path is unchanged and mono.
