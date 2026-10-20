# Feature Spec: Mid Processing and Output

**Module IDs:** mid-processing, output-stage  
**Depends on:** sub-layer, mid-source, motion-filter, parameter-state  
**Consumers:** macros-ui, verification

## Objective

Add controlled aggression, optional texture, mid-only width, and output protection while maintaining a clear independent sub path.

## Bite and drive

- Bite is a macro mapping to bounded filter emphasis and drive amount. Its mapping is explicit, preset recallable, and host-automatable.
- Provide at least two drive characters with distinct harmonic responses and no unbounded feedback.
- Drive amount, tone, and mix are Advanced parameters.
- Level compensation reduces loudness bias as drive increases. At fixed input and output trim, loudness variation across the normal drive range should remain within 3 dB unless the selected preset intentionally documents a larger change.
- Drive is mid-only; changing drive cannot alter sub samples.

## Optional texture

- Noise is a synthesized noise source with level and tone controls, off at default.
- Metal is one bounded resonant/inharmonic synthesis layer with level, MIDI-following tune, and decay envelope, off at default.
- Texture paths are mid-only and are disabled by Growl=0.
- Do not add user samples, unbounded resonance, or inter-resonator feedback.

## Width and summing

- Space=0 is mono mid. Increasing Space widens only mid up to the validated maximum.
- Sub is summed identically to left and right. Width processing cannot modify the sub contribution.
- Mid width implementation must avoid polarity inversion that causes complete mono cancellation on the acceptance fixtures.
- Output stage sums sub and mid, then applies output trim and optional limiter.

## Output trim and safety limiter

- Output trim range is −24 to +12 dB.
- Limiter enable and ceiling are stateful parameters. Ceiling range is −12 to 0 dBFS.
- Limiter must have bounded latency, report its latency to supported hosts, and avoid denormal/NaN output.
- Limiter is a peak safety stage, not a loudness maximizer. Default ceiling is −1 dBFS and limiter is enabled in initialized state.
- Output meter, if provided, reports peak and/or RMS with units and defined ballistics; it must not block DSP.

## Acceptance criteria

1. Isolated sub test shows no change from Bite, drive, texture, Space, or mid filter changes.
2. Noise and metal are silent when off/default and finite at maximum level.
3. Space=0 produces mono mid; Space=100 changes mid width while sub remains sample-identical in both channels.
4. Mono fold-down retains at least −20 dBFS mid energy for designated factory validation patches at normal gain.
5. Drive sweep remains finite and passes the 3 dB compensation target for defined steady-state fixtures.
6. Limiter-enabled test signals within the stated operating envelope remain below ceiling +0.1 dB.
7. Output trim and limiter changes do not create stuck state or callback allocation.
