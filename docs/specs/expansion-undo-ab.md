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

## Acceptance
1. 100 sequential edits can be undone and redone in exact reverse order.
2. Continuous drag produces one undo step.
3. A/B switch restores all serialized parameters within 1e-6 and creates no partial audio block state.
4. Undo/redo and A/B actions never block or allocate on the audio callback.
5. Host automation changes do not pollute the UI history.
