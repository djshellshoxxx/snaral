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
