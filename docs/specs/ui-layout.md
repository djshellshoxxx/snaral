# Feature Spec: Main and Advanced UI Layout

**Module:** macros-ui  
**Depends on:** parameter-state, host-integration, preset-variation
**Shared standards:** [Audio Application Baseline](../../standards/AUDIO_APP_BASELINE.md), [GUI Standards](../../standards/GUI_STANDARDS.md), [Accessibility](../../standards/ACCESSIBILITY.md), [Theme and Options](cdl-theme-options.md)

## Objective

Keep Snaral's five macro controls as the primary workflow while making CDL actions, preset management, performance controls, Options, help, and signal status discoverable at common plug-in sizes. This is the UI source spec for implementation; the shared standards remain the source of truth for tokens, shared actions, accessibility, and release requirements.

## Window and visual frame

- Default client size: 900 × 560 logical px. Minimum: 720 × 480. Maximum: 1440 × 900 unless host constraints require another bound.
- Resizing reflows spacing and reveals content; it never scales control dimensions or text proportionally.
- Main header: 32 px high, product name at left, Options at right, thin separator below, and version in muted 9 px monospace text at bottom-right.
- Show the 2 px burnt-orange diagonal notch at the top-left as the CDL signature until an approved logo asset is provided. Do not draw an invented logo.
- Use the exact default CDL theme tokens and typography from the shared baseline. Do not duplicate or redefine those token values here. All additional themes preserve the semantic meaning of active, warning, success, and error states.
- Default controls follow shared sizes: knobs 48 px (small 36 px, large 64 px); buttons/toggles at least 28 px high; interactive hit target at least 32 × 32 logical px. Labels are approximately 11 px uppercase, numeric values 13–14 px, and high-DPI rendering must preserve readability.

## Main view zones

1. **Header:** product name, current preset selector, previous/next preset, modified indicator, Options, and Help.
2. **Common action row:** Stop/Panic, Reset, Randomize, Bypass, Save Preset, A/B slots, and Export WAV. Each action has a tooltip and accessible name. Hide or disable only actions that are genuinely unavailable, with an explanation.
3. **Macro area:** Weight, Growl, Bite, Motion, and Space with values, units where applicable, tooltips, reset/context menu, and host automation feedback.
4. **Performance area:** voice mode, Legato, Glide, tempo sync/free status, rate/division, and four named A–D variations. Keep detailed parameters in Advanced.
5. **Signal/status area:** post-limiter waveform/signal view, honest Snaral DSP CPU estimate, output activity/clipping, and host-tempo-unavailable status. The visualization and CPU gauge have Options switches. Do not present host-wide CPU as Snaral-only CPU.
6. **Footer:** build/version text, help link, and non-color-only status cues.

At 720 × 480, place macro controls in a 3+2 arrangement if needed. The common action row may use compact labeled buttons or a labeled overflow menu, but Stop/Panic, preset selection, Options, and the five macros remain directly visible. Export WAV and less frequent actions may move into a clearly labeled menu.

## Advanced workspace

- Use tabs named Voice, Sub, Sources, Filter & Motion, Drive, Texture, and Output.
- Each tab uses a scroll container. Scrolling must reach every control; scroll position is remembered per tab during the current session.
- Use collapsible groups when a tab contains multiple related control groups; collapsed state remains visible and keyboard-operable.
- A/B comparison and parameter context menus use shared snapshot and action semantics. A/B is distinguished from saved presets.
- Value entry validates ranges, shows units, restores the prior value on Escape, and commits one host automation gesture on Enter.
- Advanced controls must not resize the macro controls. Returning to Main preserves the prior workspace and scroll position.

## Options, help, and standalone-only controls

- Options includes theme selection, tooltips on/off, global animation on/off, show/hide signal display, show/hide CPU gauge, and rerun onboarding.
- In standalone mode only, include the supported audio/MIDI device configuration and the explicitly started/stopped 440 Hz reference tone. In plug-in mode, identify audio-device settings as host-controlled and do not expose ineffective controls.
- Help is local and synchronized with shipped controls, parameter names, units, ranges, and supported formats. The CDL website opens only after the user activates its link.
- Onboarding is skippable, repeatable from Options, and uses a disposable state that restores the user's current patch when closed or skipped.

## Interaction, accessibility, and state

- All controls provide keyboard focus, a visible focus indicator, semantic/accessibility names where JUCE supports them, and traversal order matching visual grouping.
- Do not rely on color alone for active, warning, clip, selected, or error status. Respect reduced animation settings.
- Right-click actions follow the shared parameter context menu: copy, validated paste, numerical entry, MIDI learn, unlink mapping, and reset to default when relevant.
- Preserve the current preset on cancel, invalid import, or failed save. Dirty state changes promptly after sound-affecting edits and clears only after successful save/load.
- Preset browsing, help, theme changes, and UI animation never block the audio callback or modify sound state unless the user explicitly requests a sound action.

## Acceptance criteria

1. Screenshots at 720 × 480, 900 × 560, and 1280 × 720 show no clipping, overlap, or inaccessible controls.
2. At 150% and 200% display scaling, labels, values, buttons, focus indicators, and dialogs remain readable and operable.
3. Every Advanced tab scrolls to its final control and returns to the top; collapsed sections are discoverable and keyboard-operable.
4. The default CDL theme token values match the shared baseline exactly; no placeholder accent colors remain in the production theme.
5. Every shared action is exposed in the specified location or a clearly labeled menu, with unavailable states explained.
6. Keyboard traversal, tooltips, context actions, accessible names, and reduced-animation behavior pass manual review and UI automation.
7. Unsaved edits survive canceled navigation, failed import, and canceled export without changing preset or host state.
8. Plug-in Options do not claim control of host-owned devices; standalone device loss and recovery are tested.
9. Signal view and CPU estimate can be hidden independently; their values are based on documented measurement points and ballistics.
