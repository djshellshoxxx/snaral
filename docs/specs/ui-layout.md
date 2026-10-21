# Feature Spec: Main and Advanced UI Layout

**Module:** macros-ui  
**Depends on:** parameter-state, host-integration, preset-variation, expansion-preset-browser, expansion-undo-ab, expansion-snarl-trajectory, expansion-oversampling  
**Shared standards:** [Audio Application Baseline](../../standards/AUDIO_APP_BASELINE.md), [GUI Standards](../../standards/GUI_STANDARDS.md), [Accessibility](../../standards/ACCESSIBILITY.md), [Theme and Options](cdl-theme-options.md)  
**Behaviour of controls:** [macros-ui](macros-ui.md)

## Objective

Keep the five macros the primary workflow while making CDL actions, history and A/B, preset browsing, performance controls, effects, the Trajectory editor, oversampling, Options and status discoverable at common plug-in sizes. This is the placement source for implementation; shared standards own tokens, accessibility and release rules.

## Window and frame

- Default 900 x 560 logical px, minimum 720 x 480, maximum 1440 x 900 unless the host bounds differ. Resizing reflows and reveals; it never scales controls or text.
- Header 32 px: 2 px burnt-orange diagonal notch at top-left (CDL signature until an approved logo exists), product name, thin separator, version in muted 9 px monospace at bottom-right of the window.
- Default CDL tokens and typography come from the shared baseline and are not redefined here. Knobs 48 px (small 36, large 64), buttons at least 28 px high, hit targets at least 32 x 32.

## Main view zones

1. **Header:** preset selector with previous and next, Browser toggle, modified indicator, Help, Options.
2. **Common action row:** Panic, Reset, Randomize (seed popover), Bypass, Undo, Redo, A, B, Copy A to B, Save, Export WAV. At 720 px Panic, Bypass, Undo, Redo, A and B stay visible; Reset, Randomize, Save and Export WAV move to a labeled "More" menu.
3. **Macro area:** Weight, Growl, Bite, Motion, Space (large knobs). At 720 x 480 they may wrap 3 + 2.
4. **Performance row:** voice mode, Legato, Glide, Sync/Free with rate or division, variations A to D, and a second Panic button.
5. **Signal and status area:** post-limiter scope with Stereo, Sub and Mid view toggles, Snaral DSP CPU gauge with overrun marker, output peak and clip indicator, and a read-only quality readout such as "OS 2x, 64 smp". Scope and gauge hide independently from Options. Never label host-wide CPU as Snaral CPU.
6. **Footer status line:** one line of text (priority list in macros-ui), help link, non-color-only status icons.

**Preset browser panel:** a 320 px slide-over from the right edge (at most half the window), opened from the header, closed with Escape or the toggle; it overlays rather than resizing the main view. Contents: search field, filter chips (layer balance, motion, texture, factory, user, favourite), sort menu, result list with favourite star, and Apply and Cancel for the audition state. Empty-result and invalid-file messages appear inside the list.

## Advanced workspace

Eight tabs: Voice, Sub, Sources, Filter & Motion, Drive, Texture, Effects, Output. Each tab has its own scroll container; scroll position is remembered per tab for the session; collapsible groups are keyboard-operable and their collapsed state is a user preference. The Advanced header holds a compact layer control: Solo Sub, Solo Mid (mutually exclusive) and Mute Sub, Mute Mid, with a "SOLO SUB" style chip whenever one is active.

| Tab | Registry groups and controls |
|---|---|
| Voice | `voice_mode`, `legato`, `glide_ms`, `pitch_bend_range`, `velocity_to_level`, `velocity_to_attack`, `master_tune_hz`, `transpose_semitones` |
| Sub | `sub_waveform`, `sub_tune_cents`, `sub_level`, `sub_amp_*` ADSR; group **Sub Anchor**: `anchor_enabled`, `anchor_note`, `anchor_strength` with a readout of the gain at the last played note |
| Sources | `mid_source_a`, `mid_source_b`, `mid_blend`, `mid_table_position`, `mid_pulse_width`, `mid_tune_cents`, `mid_level`, `mid_amp_*` ADSR |
| Filter & Motion | groups **Filter** (`filter_*`), **Contour** (`contour_*`), **LFO** (`lfo_*`), **Snarl Trajectory** (enable, time mode, length or division, destination, depth, curve, loop, and the editor) |
| Drive | `drive_type`, `drive_amount`, `drive_tone`, `drive_mix` |
| Texture | `noise_level`, `noise_tone`, `metal_level`, `metal_decay_ms`, `metal_tune` |
| Effects | groups **Formant Furnace** (`furnace_*`) then **Harmonic Fracture** (`fracture_*`), each with an enable switch, in signal order with a "mid only, before Drive" caption |
| Output | `output_trim_db`, `limiter_enabled`, `limiter_ceiling_db`; group **Quality**: `oversampling_mode` selector (Off, 2x, 4x) with a live "Latency: N samples (x ms)" readout; engine diagnostics |

`bypass`, `layer_solo`, `sub_mute`, `mid_mute` are controlled from the action row and the Advanced header. The five macros are on the Main view. Every non-hidden registry parameter has exactly one primary control; duplicates (performance row) bind to the same parameter.

**Trajectory editor panel (inside Filter & Motion):** minimum 360 x 160 px canvas, five points; point 1 is fixed at time 0 and point 5 at time 1; points 2 to 4 move horizontally (`traj_x2..x4`) and all five vertically (`traj_y1..y5`). A dragged point cannot cross its neighbours (minimum gap 0.02) and values stay in [0,1]. Each point is keyboard focusable (arrow 1%, Shift+arrow 10%) with the accessible name "Point N, time T percent, depth D percent". A drag is one gesture and one undo step. A read-only playhead is drawn only when animation is enabled. The panel greys out (not hides) when `traj_enabled` is off.

## Options, help, standalone

- Options: theme, tooltips, animation, show or hide signal view and CPU gauge, rerun onboarding, MIDI mappings list (target, CC, channel, min, max, invert, remove, clear all), and in standalone only the audio and MIDI device setup and the 440 Hz reference tone. In plug-ins audio settings are labelled host-controlled.
- Help is local and generated from the registry so parameter names, units and ranges match. The CDL website opens only on user activation.

## Interaction, accessibility, state

- Every control has keyboard focus, a visible focus indicator, an accessible name, and traversal order matching the visual grouping. State is never colour-only; reduced animation is honoured.
- Value entry validates range, shows units, restores on Escape, and commits one gesture on Enter.
- Right-click menu: copy, validated paste, numeric entry, MIDI Learn, unlink, reset.
- Preset browsing, help, themes and animation never block audio or change sound state unless the user requests a sound action. Cancel, failed import and failed save preserve the current patch.

## Acceptance criteria

1. Screenshots at 720 x 480, 900 x 560 and 1280 x 720 (every theme) show no clipping, overlap or unreachable control, with the browser panel open and closed.
2. At 150% and 200% scaling labels, values, focus rings and dialogs stay readable and operable.
3. Every Advanced tab scrolls to its last control and back; collapsed groups work by keyboard.
4. A UI-to-registry test walks `parameterTable` and fails if any non-utility parameter has no control, or two primary controls (see [audit-checklist](audit-checklist.md)).
5. Effects tab, Trajectory panel, Quality selector, Anchor group, Undo/Redo/A/B and Panic exist at the stated locations; the 720 x 480 compact action row exposes Panic, Bypass, Undo, Redo, A, B.
6. Trajectory points cannot cross, exports match parameter values, and keyboard editing works.
7. Default CDL theme tokens match the baseline exactly; no placeholder accents remain.
8. Plug-in Options do not claim host-owned device control; standalone device loss and recovery are tested.
9. Signal view and CPU gauge hide independently; values use documented measurement points.
10. Edits survive canceled navigation, failed import and canceled export unchanged.
