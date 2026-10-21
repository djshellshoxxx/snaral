# Feature Spec: Undo, Redo, and A/B Compare

**Category:** Usability  
**Depends on:** parameter-state, macros-ui

## Objective
Let users explore patch changes safely and compare two complete sound states at matched output trim.

## Behavior
- Keep a per-instance history of the latest 100 user-initiated edit transactions.
- A continuous drag is one transaction from gesture begin to gesture end; host automation writes are not recorded as UI edits.
- Undo/redo restore complete parameter snapshots atomically. Preset load, initialize, and variation recall create one transaction each.
- A/B stores two complete parameter snapshots. Copy A↔B is explicit. Switching is click-safe and does not modify the stored snapshots.
- Compare mode uses the same output trim for both snapshots by default; provide a “compare original level” option only if level-matched comparison proves confusing.
- History is cleared only by explicit initialize/new-instance action; successful preset load remains undoable.

## Implementation rules (`UndoHistory`, `AbCompare`)

Code: `Source/State/Pure/UndoHistory.*` and `Source/State/Pure/VariationBank.*` (A/B lives with the variation bank). Message thread only; the audio thread never sees either class.

**Scope.** Only sound parameters (`ParamDef::inPreset()`) are tracked. Utility parameters (bypass, solo, mutes) are never undone, never copied by A/B and never changed by preset load.

**Steps and capacity.** A step is a list of `{param, before, after}` deltas with a label and time. At most 100 undo steps are kept and the oldest is dropped. A new edit clears the redo stack. Undo and redo touch only the parameters in the step, applied to a copy of the live snapshot that the caller publishes atomically.

**Coalescing rules.**
1. A gesture (`beginGesture` to `endGesture`) is exactly one step, however many value changes it holds. A gesture that ends where it began records nothing.
2. Outside a gesture (typed values, wheel, hosts that send no gesture), consecutive edits of the same parameter within 400 ms merge into one step. A different parameter, a longer gap, or an intervening transaction starts a new step. Undo and redo seal the top step so later edits never merge into it.
3. An edit equal to the tracked value is ignored, which makes the listener echo after an undo or redo harmless.
4. `recordTransaction` (preset load, initialize, variation recall) is always its own sealed step with a menu label such as "Load Preset X". An identical state records nothing.
5. `sync` re-baselines the tracked snapshot without a step. Use it for host automation, A/B switches and project restore. A parameter with an open gesture is skipped so automation cannot corrupt a drag.
6. `reset` (explicit initialize or new instance) clears both stacks. A successful preset load does not.

**A/B semantics (`AbCompare`).**
- `engage(live)` copies the live sound state into both sides and selects A. Toggling before that is refused.
- `toggle(live, out)` stores the live sound state into the active side, then returns the other side's stored snapshot (with the live utility state) for the caller to publish. Switching alone never rewrites the stored snapshot of the side being left except to capture live edits.
- `copyToOther` and `copyFromOther` are explicit. `copyFromOther` changes live parameters, so the caller records it as an undoable transaction.
- Matched trim (default on): `output_trim_db` comes from the live state on both sides so loudness stays matched. Switch it off to hear each side's own trim.
- A/B state (engaged, side, both snapshots, matched flag) is saved in project state (`ABCOMPARE` node) and restored with `AbCompare::restore`.
- A/B switches are not undo steps (the caller calls `sync`).

**Processor wiring (integration worker).** Own one `UndoHistory` and one `AbCompare` on the message thread. Forward UI gestures through `beginGesture`, `edit` and `endGesture`; call `sync` from the parameter listener for changes that did not come from the UI; publish snapshots returned by `undo`, `redo` and `toggle` through the existing restore-sequence guard so they land as one block-boundary update. Pass a monotonic millisecond clock as `now`.

## Acceptance
1. 100 sequential edits can be undone and redone in exact reverse order.
2. Continuous drag produces one undo step.
3. A/B switch restores all serialized parameters within 1e-6 and creates no partial audio block state.
4. Undo/redo and A/B actions never block or allocate on the audio callback.
5. Host automation changes do not pollute the UI history.
