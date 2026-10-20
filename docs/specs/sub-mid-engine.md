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
