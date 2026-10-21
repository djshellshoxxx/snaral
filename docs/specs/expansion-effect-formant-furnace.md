# Effect Spec: Formant Furnace

**Category:** Beta effect (bus-level, mono, mid layer only)
**Depends on:** motion-filter, mid-processing
**Code:** `Source/Dsp/Fx/FormantFurnace.h` (+ `FxSvf.h`), tests `tests/FormantFurnaceTests.cpp`
**Params:** `Source/Parameters/Groups/fx_furnace.def` (IDs and defaults frozen; append-only)

## Objective
Create moving vowel-like and throat-like emphasis from a small bank of bounded resonant peaks.

## Placement and interface
Mid chain: `voices -> mid bus -> Furnace -> Fracture -> Drive -> Width`, then summed with the centred sub. Furnace never sees the sub path,
which is therefore sample-identical under all furnace states.

```cpp
FormantFurnaceParams::from (const ParamSnapshot&);          // clamps/sanitises every value, NaN -> safe
void prepare (double sampleRate, int maxBlockSize);          // no allocation afterwards
void reset();                                                // clears filter state, snaps smoothers to current params
void setParams (const FormantFurnaceParams&);                // once per block
void process (float* io, int n, const MidModulation&);       // mono, in place, any n >= 1
int  latencySamples() const;                                 // 0
```

## Controls and mappings (all in `fx_furnace.def`)
| Param | Domain | Mapping |
|---|---|---|
| `furnace_enabled` | bool, default off | target of the engage/mix smoother (see Bypass) |
| `furnace_vowel` | 0..100 % | position 0..4 along the vowel table, linear interpolation of log-frequency per formant |
| `furnace_spacing` | 0..100 % | exponent `s = 0.4 + 1.2 * spacing` applied about the geometric centre `c = sqrt(F1*F3)`: `f' = c * (f/c)^s`. 50 % = table spacing, 0 % = compressed, 100 % = stretched |
| `furnace_resonance` | 0..100 % | `Q1 = 2 + 16 * r^1.5`, hard cap 18 |
| `furnace_env_amount` | 0..100 % | peak bank shift of `+1.5 octave * amount * MidModulation.contour` |
| `furnace_lfo_amount` | 0..100 % | shift of `+-1 octave * amount * MidModulation.lfo` |
| `furnace_mix` | 0..100 % | wet/dry crossfade |

Vowel table, ordered for smooth movement (u-o-a-e-i), formants F1..F4 in Hz: u 300/870/2240/3300, o 570/840/2410/3350,
a 730/1090/2440/3400, e 530/1840/2480/3450, i 270/2290/3010/3500. Total shift is clamped to +-2 octaves and each peak to
80 Hz .. min(0.45 fs, 12 kHz).

## Algorithm
- v1 uses exactly **four** fixed peaks (inside the 2-4 range; no user-defined count). Peak weights 1.0 / 0.85 / 0.6 / 0.35 and Q scales
  1.0 / 1.0 / 0.8 / 0.7 (higher formants are broader and quieter).
- Four parallel TPT state-variable band-passes (unit gain at centre: `k * bp`, `k = 1/Q`). TPT SVF is unconditionally stable for `k > 0`;
  with `Q <= 18` and no feedback path outside the SVFs there is no self-oscillation: the impulse response of any setting decays.
- `wet = comp * (0.5 * x + sum_i w_i * BP_i(x))`; `out = x + mix * (wet - x)`.
- Level compensation (always on): `comp = clamp(1 / sqrt(0.25 + sum w_i^2 * 0.22 / Q_i), 0.7, 2.2)` (pink-noise power of constant-Q band-passes).
  Hard linear gain bound of the wet path: `(0.5 + 2.8) * 2.2 = 7.3` (`FormantFurnace::wetGainBound`); measured worst peak on full-scale random sweeps 2.28.
- Smoothing: vowel, spacing, resonance and total shift are one-pole smoothed (12 ms); mix/engage 15 ms. The incoming per-block contour/LFO
  values only set the shift target, so block-rate modulation does not zipper. Filter coefficients are recomputed every 16 samples from the
  smoothed values; the TPT structure tolerates this without clicks.
- Non-finite input never enters the filter state (`sanitize`).

## Bypass
`furnace_enabled = 0` (or `mix = 0`) ramps the mix to 0; when it falls below -80 dB it snaps to exactly 0, `process()` then returns
immediately without touching `io` (bit-identical bypass) and the filter state is cleared (no stuck energy, no CPU). Re-enabling ramps from the
clean state. The initial patch is off, so a fresh instance is an exact bypass from the first sample.

## Latency, CPU, memory
Latency 0 (IIR, no look-ahead, so dry and wet are aligned by construction). Memory: about 200 bytes of state, no heap. Measured on the CI-class
Linux dev box (Release, one instance, 48 kHz, 512-sample blocks): 0.17 % of one core (see test output; record again per platform).

## Acceptance
1. All control combinations produce finite output with bounded peak level (20 000 random blocks incl. 1-sample blocks, endpoints hit often; peak <= 7.3).
2. Sweeping vowel region is continuous: the table positions are continuous (<0.1 octave per 1 % step) and an abrupt vowel step produces no
   sample step larger than 1.6x the steady-state slew.
3. Disabling the effect returns the dry mid bit-identically once settled (and always for `enabled=0, mix=0` from reset).
4. Sub remains sample-identical: the effect only accepts the mid bus buffer.
5. Mono fold-down: the effect is mono and linear for fixed parameters, `P(L) + P(R) == P(L + R)` (max difference < 1e-4); broadband RMS ratio stays within 0.3..8 so no complete cancellation.
6. Impulse response at maximum resonance decays below -60 dB within 0.75 s for all vowels/spacings; after 2 s of silence the output is below 1e-6; no DC is added to a DC-free input.
7. Deterministic: identical input and parameter sequence give bit-identical output; `reset()` reproduces the run.
