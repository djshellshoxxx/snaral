# Effect Spec: Harmonic Fracture

**Category:** Beta effect (bus-level, mono, mid layer only)
**Depends on:** mid-processing, oversampling
**Code:** `Source/Dsp/Fx/HarmonicFracture.h` (+ `FxSvf.h`), tests `tests/HarmonicFractureTests.cpp`
**Params:** `Source/Parameters/Groups/fx_fracture.def` (IDs and defaults frozen; append-only)

## Objective
Create a sharply segmented, aggressive mid tone by applying distinct nonlinear curves to a few frequency bands.

## Interface
```cpp
HarmonicFractureParams::from (const ParamSnapshot&);                 // clamps/sanitises
void prepare (double sampleRate, int maxBlockSize);                   // allocates all buffers (13 x maxBlockSize floats + 4 x 1024 delay)
void reset();   void setParams (const HarmonicFractureParams&);
void process (float* io, int n, const MidModulation&);                // built-in shapers, mono in place, any n >= 1
template <class Shaper> void processWith (float* io, int n, const MidModulation&, Shaper&&);   // external nonlinear stage
static float shapeSample (int band, float x, float gain, float amount, float bias);            // the pure nonlinearity
float shape (float x, int band) const;                                // same at the current smoothed controls
void setShaperLatency (int samples);  int latencySamples() const;      // latency of an external shaper (0 built-in)
```

## Controls and mappings
| Param | Mapping |
|---|---|
| `fracture_crossover_hz` 120..3000 (log) | low-mid / mid split `f1`; mid / upper-mid split `f2 = min(3*f1, 0.42 fs)`; smoothed 40 ms |
| `fracture_drive_low/mid/high` 0..100 % | `d = drive/100`; pre-gain `1 + (Gmax-1)*d` with Gmax 5 / 9 / 13; shaped amount `min(1, 6d)` (drive 0 = exactly linear band) |
| `fracture_symmetry` 0..100 % | static pre-shaper bias `(sym-0.5)*1.2`; 50 % = symmetric (odd curve) |
| `fracture_band_mix` (Band Tilt) 0..100 % | weights on the *added* distortion: low `-6 dB*t`, high `+6 dB*t`, `t = (tilt-0.5)*2` (50 % neutral) |
| `fracture_comp_db` -12..+6 | user trim `dbToGain`, multiplied by auto compensation `1/sqrt(1 + 1.2*mean(d))` (about -3.4 dB at full drive; "perceived level", not loudness-exact) |
| `fracture_mix` 0..100 % | scales the distortion (`lin + mix*delta`) and the compensation (`1 + mix*(comp-1)`) |
| `fracture_enabled` | engage smoother target: `enabled && mix > 0` |

## Algorithm
1. **Split** (3 bands, no feedback): two TPT SVF Butterworth stages cascaded per branch give Linkwitz-Riley 4th-order LP/HP
   (3 SVF sections per split, `LP+HP` is an exact all-pass). Input -> split at `f1` -> {low, high1}; high1 -> split at `f2` -> {mid, upper-mid};
   the low band is passed through an all-pass equal to the `f2` split (`LP2+HP2`, 3 sections) so that all three bands share the same phase response and
   **sum flat in magnitude** (`low_ap + mid + high` = all-pass of x). Crossover frequency is smoothed and coefficients recomputed every 16 samples;
   the SVF is stable under modulation, so a sweep produces no persistent notch.
2. **Shape** each band with a bounded curve (`shapeSample`): `u = gain*x + bias`; band 0 (low-mid) `tanh(u)`, band 1 (mid) `sin(pi/2*u)` (smooth fold),
   band 2 (upper-mid) triangle fold `4|t - round(t)| - 1`, `t = u/4 + 1/4` (sharp fold); `y = (curve(u) - curve(bias)) / sqrt(gain)` so the response at
   x = 0 is exactly 0 for every bias; result `x + amount*(y - x)`. Bound: `|result| <= |x| + amount*(2/sqrt(gain) + |x|)`.
3. **Recombine as deltas**: `delta_b = shaped_b - band_b`. `lowDelta` goes through a 4th-order 120 Hz high-pass (this is the fixed *fracture minimum frequency*,
   equal to the lower bound of the crossover control): distortion products and gain change below 120 Hz are removed so the sub-adjacent
   region stays clean (measured: 60 Hz fundamental within 0.01 dB of the linear path at full drive, net of compensation). `midDelta + tilt*highDelta`
   go through a 15 Hz DC blocker. With drive 0 all deltas are 0 and the effect is a flat all-pass.
4. `active = (lin + mix*(lowDelta + restDelta)) * (1 + mix*(comp-1))`; `out = dry + engage*(active - dry)` (engage smoothed 15 ms). Dry is never mixed against
   the phase-rotated band sum at partial wet, so no comb/notch appears at the crossover.
5. Fold symmetry: a bias creates even harmonics and a DC component that the delta DC blocker removes; steady-state DC measured below -90 dBFS.

## Bypass
When `engage` has decayed to 0 (it snaps below -80 dB) `process()` leaves `io` untouched (bit-identical to the input), filter state is cleared
(no stuck energy), and no band work is done.

## Oversampling hook (expansion-oversampling)
The nonlinear stage is separable so the engine can run it inside the shared `Oversampler` when `oversampling_mode != off`:
- The engine calls `processWith(io, n, mod, shaper)`. `shaper(int band, const float* in, float* out, int n, const FractureShapeControl& c)` must write
  `out[0..n)` from the **base-rate** band signal `in`. An oversampling wrapper upsamples `in`, evaluates `HarmonicFracture::shapeSample(band, x, gain, amount, bias)`
  at the higher rate using the base-rate arrays `c.gain[i], c.amount[i], c.bias[i]` (held or linearly interpolated), and decimates back.
- The linear crossover split, deltas, DC/sub filters, smoothing and mix stay at the base rate (they are linear/benign); only the folding creates aliases.
- If the wrapper has group delay `L` (< 512), call `setShaperLatency(L)` (outside `process`): the dry path, the linear band path and the shaper-input
  band signals are delayed by `L` so wet and dry stay aligned and the bypass path is also delayed by `L` (the output is the input delayed by `L`, matching the
  engine's reported latency). Fracture's own latency is otherwise 0. `process()` is `processWith` with the built-in per-sample shaper (bit-identical for an equivalent callback).
- The callback must be real-time safe and may use the scratch it owns; Fracture hands it only pointers into buffers allocated in `prepare()`.

## CPU / memory
Nine SVF sections + two high-pass sections + three shapers (tanh, sin, triangle) per sample at base rate; measured 0.53 % of one core at 48 kHz (Release, 512-sample
blocks, Linux dev box, built-in shapers, Off oversampling). Memory: 13 x maxBlockSize floats + 4 KiB of delay, allocated in `prepare()`. Oversampled cost is
dominated by the wrapper and is recorded by the engine worker.

## Acceptance
1. Crossover settings 120..3000 Hz: sum of bands (drive 0) is flat within 0.1 dB from 100 Hz to 11 kHz; a continuous crossover sweep over noise keeps short-term level within 2.5 dB of input (measured 0.28 dB), output finite.
2. DC offset of the steady-state output is below -90 dBFS for all symmetry settings and input levels tested.
3. 20 000 random control/input blocks (incl. 1-sample blocks, endpoint values) stay finite with peak < 20 for a full-scale input (measured 2.15). The limiter downstream still defines the product ceiling.
4. `mix = 0` or `enabled = 0` returns the dry path bit-identically; with an external shaper of latency `L` the dry path and wet path remain aligned (verified with a delay shaper).
5. Mono and sub: the effect only accepts the mid bus (mono); identical L/R copies stay identical; the sub path is never routed through it.
6. Impulse response decays (< 1e-4 after 0.75 s at extreme settings); no stuck energy after silence; deterministic and block-size independent (1-sample vs 256-sample blocks within 1e-4).
