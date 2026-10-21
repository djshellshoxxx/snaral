# Feature Spec: Sub and Mid Source Engine

**Module IDs:** sub-layer, mid-source  
**Depends on:** parameter-state, midi-voice  
**Consumers:** motion-filter, processing-output

## Objective

Generate two independently controllable pitched layers: a sine-first low foundation and a harmonically rich mid voice. They follow the same played pitch while allowing the mid to become aggressive without destabilizing the sub.

## Sub layer contract

- Default source is sine. Triangle is the only initial alternate.
- Follow the shared voice pitch and pitch bend. Fine tune spans −50 to +50 cents.
- Weight and sub level determine sub gain; Weight is the main-view control.
- Sub attack is 0–100 ms and release 5–2000 ms. Note lifecycle follows the shared MIDI voice.
- The sub path is mono at source and remains equal in left/right output samples after summing.
- Sub bypasses mid oscillator blend, filter, contour, LFO, drive, noise, metallic resonance, and Space.
- No automatic key selection or hidden pitch correction.

## Mid source contract

- Two pitched sources follow the shared MIDI pitch trajectory.
- Provide at least four curated source choices covering distinct harmonic spectra. Source provenance/license is recorded with each asset/table.
- Blend crossfades Source A and Source B without a level jump greater than 1 dB at the midpoint for steady-state test tones.
- Table position or source morph is available in Advanced and can be a bounded destination for contour/LFO.
- Mid tune is independent of sub fine tune and spans −50 to +50 cents.
- Growl controls mid nominal level and its documented bounded character mapping. Growl at 0 silences mid and optional texture paths.
- No sample playback. Wavetable data must be immutable at render time and prepared off the audio thread.

## Pitch and level behavior

All pitched sources use the same pitch unit and bend/glide input from midi-voice. Oscillator phase reset behavior is deterministic and documented per voice mode. Source level compensation prevents the blend control from creating a large loudness step; exact spectral loudness equivalence is not required, but the steady-state midpoint jump may not exceed 1 dB in the prescribed fixture.

## Acceptance criteria

1. Isolated sub render: sine and triangle selection, pitch, bend, envelope, and level tests pass.
2. With mid controls randomized, sub-only output is sample-identical left/right and does not change beyond numerical tolerance when Space, Bite, Motion, or drive changes.
3. With Weight/sub level at zero, sub contribution is zero.
4. Each source choice renders a finite, pitched output for notes 24, 36, 48, 60, and 84.
5. Mid blend endpoints select their sources; midpoint crossfade passes the 1 dB level-step check.
6. Wavetable/source changes perform no disk access or allocation in the audio callback.
7. Source asset manifest records source, license, sample rate/table size where relevant, and content hash.

## Implementation decisions (engine-synth)

Implemented in `Source/Dsp/Oscillators.h` (`SubOscillator`, `MidOscillator`). Mid families, tables, blend compensation and all measurement methods are in [oscillator-bank.md](oscillator-bank.md).

- **SubOscillator:** sine is `sin(2 pi phi)` from a double phase accumulator (error versus `std::sin` below 2e-6, float rounding); triangle is the band-limited table triangle (peak exactly 1, zero DC). Both start at phase 0 on the rising zero crossing; `reset(phase)` is the deterministic start. A waveform change cross-fades over 5 ms. `process(hz)` takes only a frequency, so Weight, Bite, Motion, Space, drive, contour and LFO cannot reach it; the caller applies `sub_tune_cents` (`SubOscParams::tuneRatio`) to the frequency it passes in. The sub is mono at source.
- **Level-step fixture:** the 1 dB limit applies to the steady-state RMS of the mid oscillator at blend 0.25 / 0.5 / 0.75 versus both endpoint renders, for every pair of different families (grid in oscillator-bank.md). Identical Source A and B bypass the crossfade. Growl's up-to-+0.20 bias on the blend is applied in `MidOscParams::from`.
- **Open inconsistency:** the sub contract says attack 0-100 ms and release 5-2000 ms, while the parameter table (`sub_amp_attack_ms`, `sub_amp_release_ms`) allows 0-500 ms and 1-5000 ms. The parameter table is authoritative for the beta; the envelope itself belongs to the voice worker.
- **Source manifest (acceptance 7):** recorded in oscillator-bank.md (generated tables, no license or external asset, algorithm version, size, content hash).
