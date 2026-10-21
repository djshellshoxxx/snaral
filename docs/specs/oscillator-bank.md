# Feature Spec: Oscillator Bank and Spectral Sources

**Module:** mid-source  
**Depends on:** parameter-state, midi-voice  
**Used by:** motion-filter, factory presets

## Objective

Define the initial set of pitched source materials for the mid layer without relying on licensed samples or one signature wavetable. The sources should span useful harmonic character and remain stable across the supported pitch range.

## Initial source set

Use four built-in, generated oscillator families:

1. **Saw Stack:** band-limited saw with a small set of deterministic phase offsets, summed with bounded normalization.
2. **Pulse Pair:** band-limited pulse/square with pulse width selectable from 20% to 80%.
3. **Folded Triangle:** triangle with bounded wavefold amount; default fold is mild and stable.
4. **Formant Pair:** two band-limited harmonic sources with movable spectral peaks approximating vowel-like emphasis. This is a synthesis shape, not a voice sample.

Source A and Source B select from this bank. The initial blend crossfades the two source outputs. A future user wavetable import feature is out of scope.

## Pitch and aliasing

- Sources follow the shared voice pitch, glide, and pitch bend.
- Use band-limited oscillators or mipmapped wavetables that select a safe harmonic level for current pitch and sample rate.
- Built-in tables are generated offline or at initialization, never during audio rendering.
- Oscillator state is per voice and reset/reused deterministically according to the voice/retrigger contract.
- No random phase behavior in the initial implementation. A deterministic phase offset per oscillator in a source family is allowed and documented.
- No DC offset greater than −90 dBFS in a steady-state render.

## Source position

A single source-position parameter is available for the Formant Pair and Folded Triangle. It sweeps only within each family’s defined safe range. Position modulation from contour/LFO must be bounded and smoothed.

## Acceptance criteria

1. Each family produces finite, pitch-tracking output at notes 12, 24, 36, 60, 84, and 96 at all supported sample rates.
2. A 20 kHz upper-band sine sweep render at 44.1 and 48 kHz shows no unexpected foldback above the agreed anti-aliasing threshold; define and record the measurement method in implementation.
3. Source generation performs no allocation or table generation in the audio callback.
4. Repeated render from the same reset state and same MIDI/parameter input is bit-identical where the chosen math path permits, otherwise sample deviation is documented and bounded.
5. The source manifest documents oscillator family, algorithm version, table generation, and any external dependency (expected none).
6. Blend endpoints and midpoint pass the level-step requirement in the sub-mid engine spec.

## Implementation decisions (algorithm version 1)

Implemented in `Source/Dsp/Oscillators.h` (`WaveTables`, `MidOscillator`). Tests: `tests/OscillatorsTests.cpp`.

### Source manifest (acceptance 5)

| Item | Value |
|---|---|
| Families | `saw_stack`, `pulse_pair`, `folded_triangle`, `formant_pair` (choice order of `mid_source_a/b`) |
| Algorithm version | 1 (`WaveTables::algorithmVersion`); bump on any change to a formula below |
| Origin | Generated at first `WaveTables::instance()` call (call it from `prepare()`); no samples, no files, no external dependency, license N/A |
| Table format | 2048-sample single cycle, float32, 3 guard samples for cubic (Catmull-Rom) interpolation; 16 band-limit levels per slice (256, 181, 128, 90, 64, 45, 32, 22, 16, 11, 8, 5, 4, 3, 2, 1 harmonics); 1 slice for Saw Stack and the base saw, 17 position slices (p = i/16) for Folded Triangle and Formant Pair; 1 slice for the sub triangle. 1,214,192 floats (4.9 MB), shared and immutable |
| Generation | Per slice a harmonic spectrum (cos/sin coefficients, 256 harmonics) is built in double precision (Folded Triangle: 8192-point FFT of the folded waveform), truncated per level and converted with a 2048-point inverse FFT |
| Content hash | FNV-1a over all float bit patterns (`WaveTables::contentHash()`); observed `c092ba23ae3fee50` (Linux x86-64, GCC 13, Release). It is a build fingerprint and is not asserted across compilers; the test asserts it is non-zero and that renders are bit-identical |
| Build time | about 150-220 ms once per process at the first `prepare()` |

### Family definitions

All families share one phase accumulator (double). Every family is normalised to RMS 0.5 with a peak cap of 1.0 (Formant Pair: cap 2.0 because it is peaky, measured peak up to 1.75; the other families peak at or below 1.0 plus Gibbs ripple). `phi` is the phase in cycles.

- **Saw Stack:** `x(phi) = (1/3) * sum_k saw(phi + o_k)`, `o = {0, 0.083, 0.19}` cycles (fixed, deterministic), `saw(phi) = 1 - 2 phi` (falling). Harmonic `h` has weight `(2/(pi h)) * |mean_k e^{i 2 pi h o_k}|`, so the stack is a fixed comb-shaded saw (no detune, no random phase).
- **Pulse Pair:** `P(phi) = g(w) * (S(phi) - S(phi - w))` with `S` the band-limited base saw (amplitude `0.5 sqrt 3`), `w` = `mid_pulse_width` clamped to 0.2..0.8 and smoothed (5 ms), `g(w) = 0.5 / (0.5 sqrt 3 * 2 sqrt(w (1-w)))` (RMS 0.5; peak 1.0 at w = 0.2 or 0.8, 0.5 at w = 0.5). Both reads use the same level pair, so the pulse is exactly as band-limited as the saw and has zero DC at any width.
- **Folded Triangle:** `x = fold(g * tri(phi))`, `tri` a peak-1 triangle starting at 0 rising, `fold(u) = (2/pi) asin(sin(pi u / 2))` (triangle wrap, identity for |u| <= 1), `g = 1 + 3 p^2`, `p` = source position 0..1 (default 50 % gives `g = 1.75`, mild; maximum `g = 4`). Odd half-wave symmetry gives zero DC.
- **Formant Pair:** two `1/h` harmonic sources. Source 1 has a log-Gaussian peak at harmonic `2 * 4^p`, source 2 (weight 0.7, shifted by 1/8 cycle) at harmonic `5 * 6^p`; width sigma = 0.30 in ln(h); each source is `(0.10 + peak(h)) / h`. Peaks are in harmonic-number space, so the vowel-like emphasis follows the played pitch (a timbre shape, not a fixed-Hz formant filter).
- **Source position** is the only continuous control of Folded Triangle and Formant Pair. It is clamped to 0..1, non-finite values map to 0, and it is smoothed with a 5 ms one-pole before use (slices are cross-faded linearly). Saw Stack and Pulse Pair ignore it.

### Pitch, band-limiting and glide

- `process(hz, position)` takes the frequency per sample (glide, bend and tune are applied by the caller). `hz` is clamped to 0..0.49 sr; non-finite or negative gives 0 (phase held).
- Alias-safe harmonic budget: `H = 0.95 * (sr/2) / hz`. The two adjacent levels whose harmonic counts are both <= H are cross-faded (weight linear in H), so a glide never steps between tables. Above `256 * sqrt 2` harmonics table 0 is used; between 256 and 362 the output cross-fades from table 0 to table 1. Maximum weight change measured per 0.05 % pitch step: 0.005.
- `MidOscParams::from(snapshot)` applies `growlSourceBlend(mid_blend, growl)` and the tune ratio `2^(cents/1200)`.

### Blend (equal power with measured correlation compensation)

- Weights `A = cos(pi b / 2)`, `B = sin(pi b / 2)`; exact endpoints (b = 0 renders only A, b = 1 only B, bit-identical to the single-source render). Each family weight is smoothed with a 5 ms one-pole, so changing a source or the blend never clicks.
- If `source_a == source_b` the output is that source (no crossfade).
- Equal power alone is not level-flat because the sources are phase-locked and correlated (measured +2.8 dB midpoint bump for some pairs). The output is therefore multiplied by `kappa = sqrt(sum w_i^2 / (sum w_i^2 + 2 sum_{i<j} w_i w_j rho_ij))`, where `rho_ij` is the normalised correlation of the two full-band family waveforms, precomputed at table build time on a position x pulse-width grid and bilinearly interpolated (`WaveTables::correlation`). All families are polarity-aligned so rho >= 0 and kappa <= 1. A single active family costs nothing extra.

### Measurement methods and thresholds (all numbers below were measured by the tests)

- **Aliasing (acceptance 2).** Coherent sampling: N = 32768 samples, f0 = k * sr / N with odd k (every harmonic falls on bin m*k), 4-term Blackman-Harris window, FFT. Signal bins are m*k +-4 for all m*k <= N/2; every other bin up to Nyquist counts as alias/noise. Metrics: total alias energy relative to harmonic energy, and the largest single alias bin relative to the fundamental bin. Fundamentals 500, 1500, 3000, 5000, 7000, 9000 and 12000 Hz at 44.1 and 48 kHz (harmonics reach well into the 20 kHz band), all four families, positions 0 / 0.37 / 1, pulse widths 0.2 / 0.5 / 0.8. **Threshold: both metrics <= -100 dB.** Measured worst: total -115.1 dB, worst bin -117.2 dB. A naive (non-band-limited) saw measures -8.1 dB with the same method, so the method discriminates. A continuous sweep is not used for the DFT because it has no coherent bins; glide safety is covered by the level-selection continuity test.
- **Finite and pitch tracking (acceptance 1).** Notes 12, 24, 36, 48, 60, 84, 96 at 44.1, 48, 88.2, 96, 176.4 and 192 kHz, every family: all samples finite, |x| <= 2.2, RMS > 0.05, normalised autocorrelation > 0.95 at an integer multiple of the expected period (chosen within 0.02 sample of an integer) and < 0.95 at half a period (rules out an octave error).
- **DC (steady state).** Mean over a whole number of periods, every family, positions 0 / 0.5 / 1, widths 0.2 / 0.5 / 0.8, periods from 12 to 3000 samples at 48 kHz; limit -90 dBFS (3.16e-5). Measured worst -137 dBFS.
- **Level step (acceptance 6).** For every ordered pair of different families, positions 0 / 0.5 / 1, pulse widths 0.2 / 0.5 / 0.8 and notes 24 / 48 / 84 (full grid at 44.1 kHz, a reduced grid at 48 kHz) the steady-state RMS at blend 0.25 / 0.5 / 0.75 must be within 1 dB of BOTH endpoint renders. Measured worst 0.39 dB (44.1 kHz) and 0.21 dB (48 kHz).
- **Determinism (acceptance 4).** Two renders from `reset(0)` with the same parameter changes, per-sample glide and per-sample position, on the same and on a fresh instance, are bit-identical (`memcmp`).
- **No allocation (acceptance 3).** The test replaces global `operator new` and asserts zero allocations while rendering with source changes; tables are built only in `prepare()`.
- **Reset contract.** `reset(phase = 0)` sets the phase and snaps every smoother to its target. The engine calls it on voice start in the retrigger case and does not call it for legato note changes (phase continues).
