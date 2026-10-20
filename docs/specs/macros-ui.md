# Feature Spec: Macros and User Interface

**Module ID:** macros-ui  
**Depends on:** parameter-state, host-integration, preset-variation  
**Consumers:** producers and sound designers

## Objective

Make the first useful bass sound reachable from five performance-oriented controls while keeping detailed editing available without crowding the main workflow.

## Main view

Show Weight, Growl, Bite, Motion, and Space with labels and numeric values. Also expose preset selection, initialize, save, modified state, Sync/Free plus rate/division, voice mode, variation slots A–D, output trim, and output activity.

Default logical window size is 900×560, minimum 720×480. At both sizes, all five macros and essential preset/transport-state controls remain visible. Resizing adds workspace without changing control hit targets or shrinking text below readable size.

## Advanced view

Use sections or tabs named Voice, Sub, Sources, Filter & Motion, Drive, Texture, Output. Content exceeding the viewport scrolls. Main macros remain available or one action away and do not disappear when Advanced opens.

## Interaction contract

- Controls have stable labels, visible values, tooltips/accessibility names, keyboard focus, and host automation support where applicable.
- Continuous controls support drag and numeric entry; discrete controls present named choices.
- A host automation gesture begins and ends correctly. UI refresh is read-only and does not generate writes.
- Preset load/save/import/export and variation writes run off the audio callback.
- Unsaved modifications are clearly indicated. Replacing a user patch requires an explicit action; failed import leaves it untouched.
- Host tempo unavailable status is visible when Sync was requested and fallback is active.
- Main and Advanced views use consistent units and value formatting.

## Macro mapping contract

Each macro mapping has a documented function from normalized input to destination values. Macro output is deterministic across UI and host automation. Mapping may be nonlinear but must be monotonic for level/depth destinations. Bite may coordinate filter emphasis and drive only within validated bounds. Motion=0 means no modulation depth. Space never affects sub. Growl=0 silences mid/texture; Weight=0 silences sub.

## Acceptance criteria

1. At 720×480 and 900×560 logical dimensions, main controls are visible, labeled, and clickable.
2. Resizing and supported DPI scaling introduce no overlap, clipping, or reduced hit target.
3. Every main control can be edited by mouse and keyboard and displays its current value.
4. Automation gesture and UI feedback do not cause a value loop or spurious parameter change.
5. Main macro automation and direct UI edits produce identical normalized values.
6. Import failure, save success, modified state, and sync fallback are communicated visibly.
7. Automated screenshot/UI checks cover default size, minimum size, Advanced scroll, and high-DPI layout.
