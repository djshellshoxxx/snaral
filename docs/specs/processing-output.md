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

## Implementation contract (engine-output, beta)

Code: `Source/Dsp/Drive.h`, `Texture.h`, `Width.h`, `Limiter.h` (+ `OutputTrim`), `Oversampler.h`, `SettlingSmoother.h`. All JUCE-free, header-only, allocation-free after `prepare()`. Tests: `tests/OutputStageTests.cpp`, `tests/OversamplerTests.cpp`.

### Drive
- Params: `DriveParams::from(ParamSnapshot)` reads `drive_type`, `drive_amount`, `drive_tone`, `drive_mix` and the Bite macro (`biteDriveOffsetDb(bite)`, 0..12 dB). Pre-gain = `amount% * 0.24 dB + biteOffsetDb`, clamped to 36 dB total.
- Characters: `warm_saturation` = odd tanh-style rational shaper `x(27+x^2)/(27+9x^2)` on `clamp(x, +-3)` (exactly +-1 beyond; no asymmetry, so no generated DC); `wave_fold` = triangle folder (identity for |x| <= 1, always within [-1, 1], input clamped to +-1000). Neither has feedback.
- Tone: one-pole low-pass after the shaper, 500 Hz (0 %) .. 20 kHz (100 %, bypass), log-spaced, capped at 0.45 fs. DC blocker: one-pole high-pass at 8 Hz after the tone filter. Mix: dry/wet crossfade (dry is delayed by the oversampler latency when oversampling is active so the blend never combs).
- Level compensation: static gain recomputed in `setParams()` only when gain/type/tone change. It matches the RMS of the shaped-and-toned **reference fixtures** (110 Hz sine and 110 Hz saw, each at -9 dBFS peak, power-averaged) to the unshaped RMS. Clamped to [-18 dB, +18 dB]. Fixture definition for acceptance item 5: 110 Hz sine or saw, peak 0.35 (-9 dBFS), tone 50 %, mix 100 %, amount swept 0..100 % in 10 % steps, Bite offset 0 or 12 dB. Loudness at other input levels is not compensated (drive is a level-dependent effect by nature).
- Hostile input: non-finite values become 0, input clamped to +-64; ramps (gain, compensation, mix) are 10 ms one-pole per block with linear interpolation inside the block, so results do not depend on block size except through the ramp grid.
- Oversampling: `process(io, n, &oversampler)` runs the shaper in the oversampled domain and the tone/DC/compensation/mix stages at base rate. Latency is `os.latencySamples()` (0 when Off) and is not compensated inside Drive; the engine must delay the sub by the same amount (see expansion-oversampling.md).

### Texture
- One `TextureLayer` per voice (`prepare(sr, seed)`, `setParams`, `noteOn(note, velocity)`, `addTo(buf, n)`), added to the voice source before the filter. A single bus-level instance also works (noise is note-agnostic, metal follows the latest note-on). `TextureParams::from(snapshot)` supplies the **growl gain input** (`growlMidMultiplier(growl)`, i.e. x^2); 0 silences both layers exactly. If the engine already scales the whole mid by that multiplier after the voice, call `setGrowlGain(1)` for non-zero Growl but keep a hard 0 at Growl = 0 to avoid double application.
- Noise: xorshift32 with a per-instance seed (same seed -> same samples), uniform white scaled to +-0.5 at level 1 (`gain = 0.5 * level^2`), tone = one-pole low-pass 500 Hz .. 18 kHz (white at 100 %). At level 0 (after the 5 ms smoother settles, which is exact) nothing is added and the PRNG is not advanced.
- Metal: 6 partials at ratios 1, 1.593, 2.136, 2.296, 2.653, 2.918 (circular-membrane modes), amplitude weights 0.30/0.24/0.19/0.14/0.08/0.05 (sum 1). Each partial is a damped rotation `(s, c) -> r * R(w) (s, c)` with `|pole| = r < 1`, so every parameter combination is stable; partials are independent (no coupling). Base pitch = MIDI note + 24 semitones + `(metal_tune - 50) / 50 * 12` semitones (captured at note-on, with `metal_decay_ms`); partials above 0.45 fs are muted rather than aliased. Partial k decays 60 dB in `metal_decay_ms / (1 + 0.4 k)`. Output bound: `|y| <= 0.5 * level^2 * growlGain`. The bank is excited by the note-on state (sine starts at zero, no click) and goes inactive below -100 dB.

### Width
- `Width::process(mid, left, right, n)` (left/right may alias mid). `side = Decorrelate(HP200(mid))`: one-pole high-pass at 200 Hz (low end stays mono), four first-order allpass sections (a = 0.62, -0.47, 0.83, -0.71) and a 0.4 ms delay. `L = c (mid + w side)`, `R = c (mid - w side)`, `w = widthPercent / 100` (0..1.2, smoothed 20 ms), `c = 1 / sqrt(1 + 0.5 w^2)`.
- Mono fold-down `(L + R) / 2 = c mid` exactly: the side cancels, the mid cannot. Worst case (w = 1.2) retains c^2 = 0.58, i.e. -2.4 dB. Width 0 gives `L == R == mid` bit-exactly. The sub never enters this block; the engine adds the same sub sample to L and R afterwards.

### Limiter and trim
- Stereo-linked lookahead peak limiter, lookahead L = ceil(1 ms * fs), reported latency D = L + 2 (the +2 is the detector delay that lets the cubic inter-sample estimate look one sample ahead). 50 samples at 48 kHz (1.04 ms), 47 at 44.1 kHz, 98 at 96 kHz. Details in the header comment; guarantee: applied gain never exceeds the gain required for the sample being output.
- Detector: max(|x|, Catmull-Rom mid-sample estimates of the two adjacent segments) over both channels. Release 80 ms, tracked on the gain deficit so it returns to exactly 1.0. Enable/bypass moves an `engage` factor over 5 ms; delay and tracker keep running when bypassed, so latency never changes and bypassed output is the bit-exact delayed input. A final safety clamp at ceiling + 0.05 dB exists only as a backstop and is counted (`safetyClampHits()`); the tests require 0 hits.
- Operating envelope for acceptance item 6: content band-limited to <= 12 kHz at 48 kHz (<= 0.25 fs), input peak up to +12 dBFS, ceiling -12..0 dBFS, any block size. Out-of-envelope signals (full-band content near Nyquist) can exceed the ceiling by the cubic-estimate error.
- `OutputTrim`: -24..+12 dB, 20 ms smoothing, settles exactly on the target gain (`isSettled()`).
- `SettlingSmoother` (double state) replaces `Smoother` from DspUtil.h inside these blocks: the foundation `Smoother` snaps only when `|cur - tgt| < 1e-7`, so a 20 ms glide to gain 0.063 froze at 0.0630036 and any target >= 1 can never snap (found while testing output-trim stuck state). Recommendation: foundation owner should fix `Smoother` (double state or relative snap threshold) for the other modules.

### Acceptance status (all asserted by tests that were run)
| # | Result |
|---|---|
| 1 | Structural: Drive/Texture/Width/Limiter only ever receive the mid buffer; `testNoAllocationAndSubIsolation` shows L-R is unchanged by an added centred sub. Full isolated-sub test belongs to engine-integration. |
| 2 | Pass: exact zeros at level 0 and at Growl 0 (44.1/96 kHz); max level finite and bounded at 4 sample rates x 6 notes x 3 tunes x 3 decays (worst peak 0.418, bound 0.5). |
| 3 | Pass for Width (L == R == mid at 0; L/R correlation 0.02 on noise at 100 %). Sub identity is by construction at integration. |
| 4 | Pass at block level: worst fold-down -2.36 dB vs -20 dB (noise, saw, sines 55 Hz..12 kHz, widths 25..120 %). Factory patch validation remains with verification. |
| 5 | Pass: worst loudness spread 1.55 dB (limit 3 dB) over both characters, sine and saw fixtures, Bite 0 and 12 dB; all outputs finite for NaN/Inf/1e38 input. |
| 6 | Pass: worst output over ceiling +0.000004 dB (limit +0.1) over sines 40 Hz..12 kHz, saws, square bursts, amplitudes 0.5..4, ceilings -12..0; 0 clamp hits. |
| 7 | Pass: trim settles exactly across the range, limiter gain returns to exactly 1.0 after a burst and after random ceiling/enable sweeps, zero heap allocations in 200 blocks of the full chain. |
