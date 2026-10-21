# Feature Spec: MIDI Voice and Pitch

**Module ID:** midi-voice  
**Depends on:** parameter-state  
**Consumers:** sub-layer, mid-source, host-integration

## Objective

Convert MIDI note and performance events into stable pitched voices shared by sub and mid generation. Support straightforward monophonic bass performance by default, with optional legato, glide, and bounded polyphony.

## Behavior contract

- Accept MIDI note-on, note-off, velocity, pitch bend, and all-notes-off.
- Map MIDI notes 24–84 accurately for the guaranteed range; exercise notes 12–96 in extended tests.
- One voice exposes a continuous pitch trajectory in semitones and a normalized velocity to both synthesis layers. Both layers use the same note, bend, and glide trajectory.
- Mono is the default. A note-on replaces the current target pitch. In legato mode, overlapping note-on changes pitch without retriggering the contour; with legato off, it retriggers.
- Glide is disabled at 0 ms and spans 0–500 ms. Glide is applied only to pitch changes between notes, not to initial note-on.
- Poly mode supports up to 8 voices. Allocation steals the oldest released voice first, then the oldest active voice. Same-time event ordering follows input order.
- Pitch bend defaults to ±2 semitones and is configurable from ±1 to ±12. Bend is smooth and shared between layers.
- Velocity defaults to no level or attack effect. Optional level and attack assignments are explicit parameterized amounts from 0–100%.
- Repeated note-on for a held pitch must have deterministic behavior. Note-off releases the matching held instance using a documented stack policy.
- All-notes-off releases all voices. Transport stop must not leave stuck voices.

## Thread and event requirements

MIDI events are consumed in sample order within each render block. Events at sample offset N affect audio beginning at N. Out-of-range offsets are clamped to the block boundary and counted in a test diagnostic. No MIDI event handling performs allocation or blocking work on the audio thread.

## Acceptance criteria

1. Note-on produces both sub and mid pitch at the same MIDI note.
2. Note-off releases voices with configured release behavior; all-notes-off silences after release.
3. Mono retrigger, legato hold, and glide behavior match the rules above for overlapping and repeated notes.
4. Poly mode sustains 8 notes and applies deterministic voice stealing for a ninth.
5. Pitch bend minimum, center, and maximum values match configured semitone range within 0.01 semitone.
6. Velocity has no audible effect at default assignments and produces monotonic level/attack changes when enabled.
7. Randomized MIDI sequences of 100,000 events across block boundaries produce no stuck voices, invalid state, NaN, or crash.
8. Verify event ordering at offsets 0, block midpoint, and block end.

## Engine decisions (engine-voice, authoritative for implementation)

Implemented in `Source/Engine/VoiceAllocator.h`, `Source/Engine/NoteMath.h`, `Source/Dsp/Adsr.h`; tests in `tests/VoiceAllocatorTests.cpp`, `tests/NoteMathTests.cpp`, `tests/AdsrTests.cpp`.

**Event interface.** `VoiceAllocator::handle(EngineMidiEvent, VoiceCommandList&)` appends `VoiceCommand`s (`Start`, `Retarget`, `Release`, `Kill`; each carries voice, note, velocity). The caller clears the list and applies commands before rendering the audio at the event's sample offset. Events are applied strictly in the order given (same-offset events follow input order). Channel is ignored (omni). NoteOn with velocity 0 is NoteOff. Notes > 127 are ignored. Pitch bend is stored (`bend14()`); pressure is ignored. The engine reports finished envelopes with `voiceFinished(v)`.

**Mono (voice 0).** The held-note stack has one entry per pitch; a repeated note-on of a held pitch removes the old entry and pushes it on top (re-press). The sounding note is always the top of the stack. Note-off of a non-top note changes nothing; note-off of the top returns to the new top using its original velocity, or releases when the stack is empty. Legato off: every note-on and every stack return is `Start` (retrigger contour and envelopes). Legato on: a note-on or return while the voice is gated is `Retarget` (no retrigger); a note-on while the voice is not gated (idle or releasing) is always `Start`.

**Glide.** `glide=true` on `Start`/`Retarget` only when the mono voice was gated at the moment of the pitch change (overlap or stack return). The first note and any note after release never glide. Poly mode has no glide. Glide trajectory: linear in semitones over exactly `glide_ms` (whole samples), i.e. an exponential frequency curve; retargeting mid-glide restarts from the current pitch; `glide_ms = 0` snaps.

**Poly (8 voices).** Allocation order: (1) the voice already holding this pitch (so at most one non-free voice per pitch; re-press retriggers it), (2) lowest-index Free voice, (3) oldest Releasing voice, (4) oldest Held/PedalHeld voice. "Oldest" = smallest start serial. Cases 3 and 4 set `stolen=true`. A voice is Free only after `voiceFinished`; if the engine never reports, stealing still works. Mono-to-poly or poly-to-mono switches perform a normal release of everything.

**Sustain pedal.** CC64 >= 64 down, <= 63 up (repeated same state ignored). Note-off while down marks the note pedal-held (poly: voice state PedalHeld; mono: stack entry kept, voice keeps sounding the top). Pedal-up releases pedal-held notes only; physically held notes keep sounding. In mono, pedal-up drops key-up entries and returns to the new top (or releases). Re-pressing a pedal-held pitch makes it physically held again.

**Global events.** CC123 / AllNotesOff: normal `Release` for all gated voices (pedal-held included), stack cleared, pedal state unchanged. CC120 / AllSoundOff: `Kill` for every non-free voice (including releasing ones) in the same call, `declick=true` on the list (engine applies the 5 ms output ramp, e.g. `Adsr::forceRelease(VoiceAllocator::declickSamples(sr))`), pedal state unchanged. `panic()`: AllSoundOff plus pedal cleared; silent from the event sample, de-click budget 5 ms. TransportStop: pedal cleared, then normal release of every gated voice (no de-click).

**Pitch.** `pitch = note + transpose` (semitones); glide acts on that value; pitch bend (`bendSemitones`, -8192 -> -range, 8191 -> +range, 0 -> 0, smoothed by the engine with `bendSmoothMs`) is added after; per-layer cents last; `hz = A4 * 2^((pitch - 69) / 12)` in double precision. Both layers share the same note/glide/bend trajectory.

**Velocity.** Normalised `(v-1)/126`. Level gain `1 - a*(1-v)`; attack-time multiplier `1 - a*v` (harder = faster); `a` = parameter / 100; 0% is exactly 1.0 for all velocities.
