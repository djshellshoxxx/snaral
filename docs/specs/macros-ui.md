# Feature Spec: Macros and User Interface Behaviour

**Module ID:** macros-ui  
**Depends on:** parameter-state, host-integration, preset-variation, macro-mapping  
**Consumers:** producers and sound designers  
**Geometry and zones:** [ui-layout](ui-layout.md). This spec owns control behaviour; ui-layout owns placement.

## Objective

Make the first useful bass sound reachable from five performance-oriented controls while keeping detailed editing, history, comparison, preset browsing and MIDI learn available without crowding the main workflow.

## Macro controls

- Five large rotary controls (64 px) bound to `weight`, `growl`, `bite`, `motion`, `space` through the parameter tree. Each shows its label, a value readout (whole percent, `Space` also shows width as percent in its tooltip), and a tooltip from the help text (purpose, unit, range, default).
- Mouse: drag changes the value; Shift-drag is fine adjustment (10x finer); double-click resets to default; mouse wheel steps 1%. Click on the readout opens numeric entry (Enter commits one automation gesture, Escape restores the prior value and ends the gesture).
- Keyboard: focusable in the visual order Weight, Growl, Bite, Motion, Space; Left/Down and Right/Up change by 1%, Page Down/Up by 10%, Home/End jump to min/max, Enter opens numeric entry.
- Context menu (right-click or the keyboard menu key): copy value, validated paste, enter value, MIDI Learn, Unlink mapping (only when mapped), Reset to default.
- Host automation animates the control without writing back. Gestures: `beginChangeGesture` on press, `endChangeGesture` on release; programmatic refresh never starts a gesture.
- A macro-influence indicator (small marker on the related Advanced control) is shown while the macro contribution is non-zero; the Advanced value itself never moves when a macro moves (see [macro-mapping](macro-mapping.md)).
- Macro mapping is the same function for UI, automation, MIDI CC and presets; the UI contains no mapping math.

## Main view content

Preset selector and browser button, modified indicator, common action row (Panic, Reset, Randomize, Bypass, Save, Undo, Redo, A/B, Export WAV), five macros, performance row (voice mode, Legato, Glide, Sync/Free with rate or division, variations A to D, Panic), output trim and output activity, scope and CPU gauge, one-line status. Exact zones and sizes are in ui-layout.

## Advanced view behaviour

- Tabs: Voice, Sub, Sources, Filter & Motion, Drive, Texture, Effects, Output. Main macros stay visible or one click away; opening Advanced never moves the macros.
- Tab scroll position is remembered for the session. Collapsible groups remember their state in user preferences, not in presets.
- All controls follow the same drag, numeric entry, context menu and keyboard rules as macros. Choice controls are menus or segmented buttons with named options. Units and formatting match everywhere a value is shown (Hz, ms, %, dB, ct, st).

## Status line and feedback

A single status line (see ui-layout) reports, in priority order: engine overrun, import or save failure, MIDI Learn waiting, tempo unavailable (when Sync requested), oversampling latency note, undo or A/B action result, save success. Each message names the cause in plain language; transient messages clear after 5 s except errors, which stay until dismissed or replaced. Status text is exposed as an accessibility live region and never relies on color alone.

## Edit history and dirty state

- A drag, numeric entry, preset load, initialize, variation recall, Randomize, or Reset is one undo transaction ([undo and A/B](expansion-undo-ab.md)). Host automation writes are not UI edits.
- Dirty state turns on within one UI refresh of any sound-affecting edit and clears only after a successful save or preset load. Replacing a modified user patch asks for confirmation; failed import or canceled dialogs leave the patch unchanged.

## Non-blocking rule

Preset, browser, import, export, help and theme actions run off the audio thread. Opening, resizing and animating the editor must not allocate on the audio thread or change sound state.

## Acceptance criteria

1. At 720x480 and 900x560 all five macros, Panic, preset selector and Options are visible, labeled and operable.
2. Resizing and 150% or 200% scaling cause no overlap, clipping, or hit targets below 32x32 px.
3. Every main control is operable by mouse and keyboard with the keys above and displays its current value.
4. A drag produces exactly one begin and one end gesture and one undo step; refresh from automation produces zero parameter writes.
5. UI edits and automation of the same normalised value yield identical parameter values and identical renders.
6. Import failure, save success, dirty state, sync fallback and engine overrun are each visible in the status line in a screenshot test.
7. Macro-influence indicators appear and disappear with the macro contribution; Advanced baselines are unchanged after macro sweeps.
8. Screenshot and UI-automation checks cover default size, minimum size, Advanced scroll on every tab, and high DPI.

## Test fixtures

Parameter-gesture counter on a mock parameter, numeric-entry validator, status-priority table test (JUCE-free), and screenshot baselines recorded per theme at the three reference sizes.
