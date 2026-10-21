# Feature Spec: Performance Foundation

**Status:** Essential addition to the initial release  
**Modules:** midi-voice, sub-layer, mid-source, macros-ui  
**Depends on:** parameter-state and the corresponding engine modules

## Objective

Add the basic instrument behaviors needed for reliable note shaping, tuning, MIDI performance, and sound isolation. The existing contour envelope controls timbre; it does not replace amplitude envelopes. These additions close that functional gap.

## 1. Independent amplitude envelopes

Add one ADSR amplitude envelope per synthesis layer. The mid amplitude envelope is separate from the filter/source contour.

Parameters:
- Sub: sub_amp_attack_ms, sub_amp_decay_ms, sub_amp_sustain, sub_amp_release_ms.
- Mid: mid_amp_attack_ms, mid_amp_decay_ms, mid_amp_sustain, mid_amp_release_ms.
- Attack: 0–500 ms. Decay: 1–5000 ms. Sustain: 0–100%. Release: 1–5000 ms.
- Default patches use short but nonzero release and a usable sustain level. Exact factory defaults are set during listening review.
- Note-off enters release unless Sustain Pedal is held. All Notes Off follows the MIDI behavior below.
- Layer gain envelopes multiply their layer outputs after source/filter/drive processing as appropriate. Sub remains outside mid processing and centered.

Acceptance:
1. Note-on reaches sustain through attack and decay within one sample of the configured envelope trajectory.
2. Note-off reaches silence within configured release plus one render block.
3. Changing the timbre contour does not change amp-envelope timing.
4. Sub amp envelope changes never affect mid output and vice versa.
5. Repeated notes and voice reuse reset envelope state without stale tails or clicks beyond documented smoothing.

## 2. Sustain pedal and MIDI panic

- Implement MIDI CC64 sustain. Values 0–63 are pedal-up; 64–127 are pedal-down.
- Note-off while pedal-down marks that note released but keeps its voice active until pedal-up.
- Pedal-up releases all notes held by the pedal, preserving notes that remain physically held.
- Implement CC123 All Notes Off as a normal release for all notes.
- Implement CC120 All Sound Off as immediate voice silence with a short 5 ms output de-click ramp.
- Add a visible Panic action that performs All Sound Off and clears sustain state. It must be accessible from the main UI.
- Host transport stop must clear pedal-held notes and perform the safe release path.

Acceptance:
1. CC64 down delays note release; pedal-up begins release for all pedal-held notes.
2. Notes physically held through pedal-up continue sounding.
3. CC123 releases and CC120 silences according to the rules.
4. Panic clears all voices and sustain state within 5 ms.
5. Randomized pedal/note event sequences do not create stuck notes.

## 3. Master tuning and transpose

- Add global tune reference A4 from 430.0 to 450.0 Hz, default 440.0 Hz, with 0.1 Hz display resolution.
- Add global semitone transpose from −24 to +24 semitones, default 0.
- Apply both to sub and mid before layer-specific fine tuning.
- Pitch bend is applied after transpose and follows the existing configured bend range.
- Global tune and transpose are automatable and serialized in presets/project state.

Acceptance:
1. At A4=440 Hz, MIDI note 69 generates 440 Hz before intentional per-layer fine tune.
2. Tune endpoints produce the calculated frequency within 0.1 cent.
3. Transpose ±12 shifts both layers exactly one octave.
4. Changing tune or transpose does not alter envelope timing or stereo invariants.

## 4. Layer audition controls

- Add Sub Solo, Mid Solo, and Mute Sub/Mute Mid controls in Advanced.
- Solo controls are mutually exclusive. Activating Sub Solo bypasses mid contribution at the final sum; it does not stop or reset mid voices/modulators. Mid Solo behaves symmetrically.
- Mute controls suppress only the selected layer at the final sum and preserve phase/envelope state.
- Solo/mute are UI utility states and persist in project state but are excluded from factory preset JSON unless explicitly added as a serialized parameter in a later schema revision.
- A clear indicator shows active solo or mute state. Panic is separate.

Acceptance:
1. Isolated layer output matches normal rendering of that layer within numerical tolerance.
2. Toggling solo/mute while notes sustain creates no stale state or unbounded click.
3. The sub remains centered under every solo/mute combination.
4. Solo states are mutually exclusive and keyboard accessible.

## Parameter and UI additions

Add the listed envelope, tuning, transpose, sustain, panic, solo, and mute controls to the parameter registry and Advanced UI. Place Panic in the main view’s performance row. Add a compact layer solo/mute control to the Advanced header. Preserve existing IDs; add new IDs only.

## Implementation boundary

These are release-scope additions. Do not add MIDI CC learn, undo/redo, preset search, or generated randomization in this feature. Those are usability candidates in the opportunity document.

## Engine decisions (engine-voice, authoritative for implementation)

**ADSR trajectory** (`Source/Dsp/Adsr.h`, sample counts `N = max(1, round(ms * sr / 1000))`, all stages linear in amplitude):
- Attack: `out[i] = s0 + (1 - s0) * (i + 1) / Na`, `s0` = current level at note-on (0 when idle). Reuse/retrigger therefore never drops or clicks; the peak 1.0 is output at index `Na - 1`. Attack 0 ms gives a 1-sample attack (first sample 1.0). `noteOn(attackScale)` multiplies attack time (velocity hook).
- Decay: `out[k] = 1 + (S - 1) * k / Nd`, `k = 1..Nd`, taking the full decay time regardless of sustain depth, ending exactly at `S`.
- Sustain: holds `S`; a sustain change while held slews at 1/(5 ms) full scale.
- Release: `out[k] = r0 * (1 - k / Nr)` from the level at note-off (any stage), exactly 0 at `k = Nr`, then Idle. Release length is always `release_ms`, latched at note-off. Therefore silence is reached within release + one render block (tested for blocks 1..1024).
- Note-off while Idle/Release is ignored. `forceRelease(n)` releases over n samples (de-click kill); `kill()` is a hard zero.
- Tests assert the exact trajectory (tighter than the "within one sample" criterion).

**Sustain / panic / tuning** behaviour is specified in the "Engine decisions" section of midi-voice.md. Master tune and transpose apply before layer cents; changing them does not touch envelope state (they only feed the pitch chain).
