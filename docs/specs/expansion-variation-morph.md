# Feature Spec: Variation Morph

**Category:** Usability  
**Status:** Post-beta (not in v0.2 beta).  
**Depends on:** preset-variation, parameter-state, expansion-undo-ab, macros-ui

## Objective

Move continuously between two of the four named variation snapshots without altering any stored slot, and land exactly on a slot at the endpoints.

## Model

- Controls (post-beta registry group `morph.def`, append-only, instance state, not in presets): `morph_from` and `morph_to` (choice A to D) and `morph_amount` (0 to 100 %, automatable).
- `from` and `to` must be different, filled slots. Empty slots are not selectable and the control explains why. Fewer than two filled slots disables Morph with a message.
- Morph is a write-through operator on the live parameters: when `morph_amount` changes, the interpolated state is computed on the message thread as a complete candidate and published atomically at a block boundary, the same path as a preset load. Later individual edits are normal edits (they do not lock or fight the morph).

## Interpolation rules

For every parameter that is part of a slot (no `pfNoPreset`), with `t = morph_amount/100`, a = value in `from`, b = value in `to`:
- Float parameters: interpolate in host-normalised space, `n = na + (nb - na) * t`, then `fromNormalized(n)`. This is logarithmic for `Log` parameters (Hz, rates) and linear for linear ones (dB, percent) as the registry declares. Integer-flagged parameters round after interpolation.
- Choice parameters: value of `from` for `t < 0.5`, of `to` for `t >= 0.5`.
- Bool parameters: switch to `to`'s value when `t >= 0.55` and back to `from`'s value when `t <= 0.45`; between 0.45 and 0.55 the last state holds (hysteresis).
- Trajectory points interpolate as floats; a convex combination of two ordered point sets stays ordered with the minimum gap.
- Excluded: utility parameters (bypass, solo, mute), meter and voice state, oscillator and LFO phase, UI-only state, MIDI mappings, variation names and slots themselves.
- The sub level interpolates in normalised space between two valid levels, so it never overshoots either endpoint; the sub stays centered at every `t`.
- Endpoints are exact: `t = 0` equals slot `from` and `t = 100` equals slot `to` within 1e-6 of normalised value.

## Commit

"Commit morph" writes the current interpolated candidate into a slot chosen explicitly by the user (a confirmation names the slot to be replaced). No slot is changed otherwise. Commit is one undo transaction. Each morph gesture (drag from start to end) is one undo transaction.

## State

Project state stores `morph_from`, `morph_to`, `morph_amount`. The morph is not stored in preset files. Loading a preset resets `morph_amount` to 0 without altering slots.

## UI

A Morph strip in the Main view performance row beside the A to D buttons: from selector, to selector, a 0 to 100 % slider, and Commit. Value readout and keyboard operation follow macros-ui.

## Acceptance criteria

1. At each endpoint every parameter matches its slot within 1e-6 normalised.
2. Continuous parameters are monotonic through the travel; choices and bools follow the stated switch points, including the hysteresis band under a back-and-forth automation sweep.
3. Each interpolation publishes complete candidates only (no partial-block state) and smoothing prevents steps above the macro-mapping criteria; sub L/R stay sample-identical.
4. Slot contents are byte-identical before and after any morph that is not committed.
5. Project save and reopen restores slots and the morph position; a preset load resets the amount to 0.
6. Undo restores the exact pre-morph or pre-commit state.

## Test fixtures

Two synthetic slots covering every parameter type, endpoint equality, Log versus linear midpoint values, hysteresis sweep, empty-slot rejection, and commit isolation.
