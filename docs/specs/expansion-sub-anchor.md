# Feature Spec: Sub Anchor Compensation

**Category:** Distinctiveness  
**Status:** Promoted into the v0.2 beta (bonus feature, see [implementation plan](../IMPLEMENTATION_PLAN.md)).  
**Depends on:** sub-mid-engine, midi-voice, performance-foundation, parameter-state  
**Parameters (registry group `sub_anchor.def`, group label "Sub"):** `anchor_enabled` (bool, default off), `anchor_note` (integer MIDI note 24 to 60, default 36), `anchor_strength` (0 to 100 %, default 50)  
**Implementation:** `Source/Engine/SubAnchor.h/.cpp` (JUCE-free), used by the voice sub path

## Objective

Optionally keep the perceived sub balance more even across a bassline's range by turning the sub down for higher notes and up for lower ones, relative to a chosen anchor note. It is a gain stage on the sub only.

## Compensation formula (normative)

Let `p` be the voice's continuous played pitch in MIDI note units, after transpose and glide and before pitch bend and the per-layer fine tune. Let `a = anchor_note`, `s = anchor_strength / 100`, `d = p - a`.

`gainDb = -12 * s * tanh(d / 12)`, `gain = 10^(gainDb / 20)`

- At `d = 0` the gain is exactly 0 dB (gain factor exactly 1.0f is used, not a computed approximation).
- Notes above the anchor are attenuated, notes below are boosted; the curve is odd, strictly monotonic for `s > 0`, smooth, and bounded: `|gainDb| < 12 * s` for every `d`, so the +-12 dB limit is never reached or exceeded.
- Reference values at `s = 1`: `d = +-12` gives `-+9.1391 dB`; `d = +-24` gives `-+11.5683 dB`; `d = +-36` gives `-+11.9407 dB` (tolerance 0.001 dB). At `s = 0.5` all values halve.
- `s = 0` or `anchor_enabled = false`: the stage is skipped entirely. Disabled output is sample-identical to a build without the stage.
- The gain is recomputed per voice at control rate (every 32 samples) from the continuous pitch and smoothed with a 10 ms one-pole before multiplying the sub signal, so glides and note changes cannot click. The simple curve selector (flat, gentle, steep) of the first draft is dropped: `anchor_strength` alone sets the slope, because the registry has no curve parameter and adding one would change the state schema.

## Signal position

`sub oscillator -> sub amp ADSR -> anchor gain -> layer sum`, with Weight applied as in [macro-mapping](macro-mapping.md). The mid layer, stereo position, pitch, macros and Advanced baselines never change. Layer solo Sub auditions the compensated sub. The final limiter still protects against boosts; the anchor never raises mid level or changes limiter settings.

## UI

Controls sit in the Sub tab group "Sub Anchor" (see [ui-layout](ui-layout.md)): an enable switch, a note selector (shown as note name and number), and a strength slider, plus a readout "gain at last note: x.x dB". The group is dimmed, not hidden, when disabled. Help states that the control compensates by note number, not by playback system loudness, and makes no claim of equal loudness on all speakers.

## Edge cases

- `p` below 12 or above 108 (extreme transpose) uses the same formula; the bound still holds.
- Poly mode compensates each voice by its own pitch; legato glide moves the gain continuously.
- Non-finite pitch or parameters produce gain 1.0.
- Preset load or A/B switch with a changed anchor moves the gain through the 10 ms smoother.

## Acceptance criteria

1. At the anchor note (with and without glide arrival) the gain factor is exactly 1.0.
2. The gain is strictly decreasing in `p` for `s > 0`, equal to the reference values above, and never exceeds `12 * s` dB in magnitude, across all integer `a` in 24..60, `p` in 0..127 and `s` in {0, 0.25, 0.5, 1}.
3. Disabled (and `s = 0`) renders are sample-identical to the baseline sub path.
4. A glide across 36 semitones with the anchor on produces finite output and no sample step above -60 dBFS relative to the steady level; L/R sub samples remain identical.
5. Enable, note and strength save and recall through presets, variations, A/B and host state exactly.
6. Mid output with the anchor on and off is bit-identical.

## Test fixtures

`tests/SubAnchorTests.cpp`: closed-form table above, monotonic and bound sweep, glide-click render, disabled-identity render, and a registry check that the three IDs exist with the stated ranges.
