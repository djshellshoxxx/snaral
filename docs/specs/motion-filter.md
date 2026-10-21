# Feature Spec: Filter and Movement

**Module ID:** motion-filter  
**Depends on:** parameter-state, midi-voice, mid-source  
**Consumers:** mid-processing, macros-ui, host-integration

## Objective

Give each note a controllable beginning, peak, and return using one note-triggered contour and one optional LFO. Shape only the mid voice; preserve pitch and sub independence.

## Contour contract

- One contour has attack, decay, sustain, release, shape selection, and depth.
- Attack opens from the current baseline toward a peak; decay moves to sustain; release returns to baseline after note-off.
- At least three contour shapes are provided: linear, exponential ease-in, and exponential ease-out. Shape math is deterministic and tested.
- The contour can target filter cutoff and one spectral position destination through fixed documented routing. No matrix editor is included.
- Motion scales contour and LFO depth. Motion at 0 sets both depths to zero; changing Motion cannot change oscillator pitch or sub output.
- Retrigger follows mono/legato behavior from midi-voice.

## LFO contract

- Free rate range: 0.02–20 Hz.
- Sync choices include straight, dotted, and triplet values spanning whole note through 1/32 note.
- Sync uses valid host BPM when supplied. If unavailable, last valid free rate is used and the UI reports fallback.
- Waveforms: sine, triangle, saw, pulse. Phase is 0–360 degrees.
- Retrigger on/off determines whether note-on resets phase. With retrigger off, phase continues through note changes.
- LFO destinations are limited to filter cutoff and mid source position. No audio-rate modulation.
- Parameter changes are smoothed or transition at safe phase boundaries to avoid clicks.

## Filter contract

- Provide resonant low-pass and band-pass emphasis modes.
- Cutoff spans 20 Hz–20 kHz on a logarithmic control scale.
- Resonance is limited to a tested stable region; self-oscillation is excluded from the first implementation.
- Filter state is per voice in poly mode and is reset/released safely on voice reuse.
- Filter and contour process mid only.

## Acceptance criteria

1. Motion=0 produces no contour/LFO movement when tested with static input.
2. Each contour reaches peak and returns according to attack/decay/sustain/release within one sample of the computed control trajectory.
3. Sync divisions produce the expected cycle duration at 60, 120, and 180 BPM within 1 ms or one sample, whichever is larger.
4. Free mode cycle duration matches configured Hz within 0.1%.
5. Retrigger on resets phase; retrigger off preserves phase across note-on.
6. Filter modes, cutoff endpoints, resonance range, and parameter sweeps produce finite output without runaway feedback.
7. Sub-only render is unchanged by all contour and LFO changes.

## Implementation decisions (engine-synth)

Implemented in `Source/Dsp/SvfFilter.h`, `Lfo.h` and `Contour.h`. Tests: `tests/SvfFilterTests.cpp`, `LfoTests.cpp`, `ContourTests.cpp`.

### Filter (`SvfFilter`)

- Topology: zero-delay-feedback (trapezoidal, TPT) state-variable filter, `g = tan(pi fc / sr)`, damping `k = 1/Q`. Per-sample cutoff changes are safe (coefficients are recomputed only when the clamped cutoff changes).
- Cutoff is clamped to 20 Hz .. min(20 kHz, 0.49 sr); non-finite input uses 1200 Hz.
- Resonance 0..80 % maps exponentially to `Q = 0.5 * 20^(r/80)`: 0 % = 0.5 (no peak), 20 % (default) = 1.06, 80 % = 10 (+20 dB at the cutoff). `|H(fc)|` of the low-pass equals Q exactly. Self-oscillation is excluded because `k >= 0.1`.
- Band-pass output is `k * bp`, i.e. unity gain at the centre for every resonance.
- Safety: non-finite input is replaced by 0, integrator states are bounded to +-1e4 (a state outside that, or NaN, resets the filter and outputs 0), denormals are flushed. `reset()` clears the two states (voice reuse).
- Measured: passband gain 1.0 +-0.02, `|H(fc)| = Q` within 2 %, at least 34 dB down one decade above the cutoff, band-pass peak 1.0 +-0.02, at 44.1 / 48 / 96 kHz and resonance 0 / 20 / 50 / 80 %. White noise through every cutoff endpoint, resonance 0 / 40 / 80 %, both modes, 44.1-192 kHz: finite, worst |y| 5.0. Alternating 20 Hz / 20 kHz cutoff every sample, random per-sample cutoff and a 3 Hz log sweep at resonance 80 %: finite, worst |y| 18.6. The impulse response at Q = 10 decays below 1e-4 at every tested cutoff and rate.

### LFO (`Lfo`)

- Output is bipolar -1..1; depth and Motion are applied by `MotionRouting`. Waveforms at phase 0: sine and triangle start at 0 rising, saw ramps -1 to +1, pulse is +1 for the first half (50 % duty).
- Phase offset `lfo_phase` (0-360 deg, applied to the read phase, not accumulated).
- Free rate 0.02-20 Hz (clamped). Sync rate = `bpm / (60 * beats)` Hz with beats from the `lfo_sync_division` order: 1/1 = 4, 1/2 = 2, 1/4 = 1, 1/8 = 0.5, 1/16 = 0.25, 1/32 = 0.125 quarter notes, times 1 (straight), 1.5 (dotted, `d`) or 2/3 (triplet, `t`); the 18 choices are ordered (1/1, 1/1d, 1/1t, 1/2, ...). Synced rate is capped at 100 Hz (no audio-rate modulation); valid BPM is clamped to 10..999.
- The phase is a free-running double accumulator (it is not locked to the host PPQ); rate or division changes only change the increment, so the phase is always continuous. Retrigger on: `noteOn()` sets phase 0; off: phase continues.
- Tempo fallback: when sync is selected and `HostTiming.bpmValid` is false (or bpm is not finite/positive), the rate is the last valid free rate (`lfo_rate_hz`, a non-finite value keeps the previous one). `usingFallbackRate()` is true exactly then, and the UI shows `Lfo::fallbackMessage` = "Host tempo unavailable - using free rate". Free mode never reports fallback.
- Click safety: a slew limiter (full scale in 2 ms) acts only on discontinuities (saw reset, pulse edges, retrigger, phase or waveform jump). Sine and triangle up to 20 Hz pass bit-exactly (`rawValue()` is the unslewed value).
- Measured (the tests): cycle duration error of all 18 divisions at 60 / 120 / 180 BPM and 44.1 / 48 kHz is below 3e-8 ms (limit: 1 ms or one sample); free mode 0.02, 0.1, 0.5, 1, 2, 7.3, 20 Hz is within 5e-9 % (limit 0.1 %).

### Contour (`Contour`)

- Closed form, sample index `i` after note-on, attack length `A = round(attack_ms * sr / 1000)` samples, start value `v0` (0 from idle, the current value on retrigger), shape function `s`:
  - attack, `0 <= i < A`: `v0 + (1 - v0) * s(i / A)`;
  - decay, `j = i - A`, `0 <= j < D`: `1 + (S - 1) * s(j / D)` (so the peak value 1.0 is exactly sample `A`);
  - sustain: `S` (tracked through a 5 ms one-pole so live edits of `contour_sustain` do not click);
  - release from the value `vr` at note-off, `0 <= j < R`: `vr * (1 - s(j / R))`, then exactly 0 (sample `R`).
  - A = 0 ms skips the attack (the first sample is 1.0). D and R are at least 1 sample.
- Shapes (k = 4): linear `s(x) = x`; ease_in `s(x) = (e^{kx} - 1)/(e^k - 1)` (slow start); ease_out `s(x) = 1 - (e^{k(1-x)} - 1)/(e^k - 1)` (fast start). The shape applies to the progress of every segment.
- Hooks: `noteOn(retrigger)`: retrigger = true always restarts the attack from the current value (no jump); retrigger = false (legato) is ignored while the contour is in attack/decay/sustain and starts normally from idle/release. `noteOff()` starts the release from the current value. `reset()` is the deterministic voice-reuse state (idle, 0).
- Measured: the output equals the closed form to 2e-5 for all three shapes at 44.1 / 48 / 96 kHz, the peak is reached exactly at sample A and the contour is idle exactly after R release samples (acceptance 2 holds with zero sample error).

### Fixed routing (`MotionRouting`)

Per block `MotionRouting::from(snapshot)`, per sample `evaluate(contour01, lfoBipolar, traj01)`:

```
cutoff semitones = contour * contourSt + lfo * lfoSt + traj * trajSt
cutoffHz = clamp(base * 2^(semitones/12), 20, 20000)            (log domain, offset clamped to +-120 st)
position = clamp(basePosition + contour * contourPos + lfo * lfoPos + traj * trajPos, 0, 1)
motion   = motionDepthMultiplier(motion macro)
contourSt  = motion * (contour_depth * 60 + biteFilterPeakOffsetSemitones(bite))     (0 when the trajectory owns the cutoff)
contourPos = motion * contour_depth * 0.25                                          (0 when the trajectory owns the position)
lfoSt      = motion * lfo_depth * 24         lfoPos = motion * lfo_depth * 0.25
trajSt     = traj_depth * 60 + biteFilterPeakOffsetSemitones(bite)   (only if traj_enabled and dest is cutoff/both)
trajPos    = traj_depth * 0.5                                        (only if traj_enabled and dest is source_position/both)
```

- Contour depth 100 % is +5 octaves of cutoff at the peak, LFO depth 100 % is +-2 octaves; contour and LFO position spans are 0.25 (LFO +-0.25). Depths are fractions (0..1).
- Decision: Bite's up-to-+18 st peak offset rides on the contour peak, so Motion = 0 removes it as well (acceptance 1 requires no movement at Motion 0 with static input). With the trajectory active, Bite is added to the trajectory peak and is not Motion-scaled, because the trajectory is not Motion-scaled.
- Decision: when the Snarl Trajectory is enabled it replaces the contour on its destination(s) (the contour keeps driving the other one); the LFO is unaffected. Trajectory is scaled by its own depth only.
- Acceptance 1 is tested with the static-input fixture: with Motion = 0 and Bite = 100 %, every contour/LFO input combination returns exactly the base cutoff and position. Acceptance 7: the sub oscillator takes only a frequency, and the test renders it bit-identically with and without running contour changes.
- Position modulation is bounded to 0..1 here and smoothed by `MidOscillator` (5 ms) before use.

