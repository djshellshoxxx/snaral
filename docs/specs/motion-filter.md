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
