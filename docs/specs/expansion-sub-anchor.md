# Feature Spec: Sub Anchor Compensation

**Category:** Distinctiveness  
**Depends on:** sub-layer, midi-voice, parameter-state

## Objective
Optionally maintain a consistent perceived sub balance across a bassline’s note range.

## Behavior
- Add Sub Anchor enable, anchor note (MIDI 24–60), compensation amount 0–100%, and maximum compensation ±12 dB.
- Compensation is a smooth, bounded function of note distance from anchor. The user can select a flat, gentle, or steep curve.
- Apply compensation only to sub amplitude after note pitch is known and before final layer summing.
- Default is disabled. User can audition the compensated sub through layer solo.
- Never change pitch, stereo position, mid level, or macro values. Do not claim equal loudness across all speakers or playback systems.

## Acceptance
1. At anchor note, compensation is exactly 0 dB.
2. Gain is monotonic in either direction according to selected curve and never exceeds ±12 dB.
3. Disabled output is sample-identical to the baseline sub path.
4. Compensation does not cause NaN, clicks, or stereo imbalance while notes glide.
5. State and preset save/recall preserve anchor and curve settings.
