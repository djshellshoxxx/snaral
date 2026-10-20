# Feature Spec: Main and Advanced UI Layout

**Module:** macros-ui  
**Depends on:** parameter-state, host-integration, preset-variation

## Objective

Set explicit layout behavior so the five macros remain the primary workflow and Advanced controls remain discoverable at common plug-in window sizes.

## Main view layout

Use a fixed logical coordinate design with responsive spacing, not proportional scaling of widgets.

- Header row: Snaral name, preset selector, previous/next, initialize, save, modified indicator.
- Main macro row: five equally sized labeled controls, each with value readout and tooltip.
- Performance row: Sync/Free and rate/division, Mono/Poly and Legato, A–D variation slots.
- Footer: output trim, stereo/peak activity, host-tempo fallback status, Advanced entry.
- Default window: 900×560 logical px. Minimum: 720×480. At 720×480, macro controls may be arranged 3+2 across two rows but may not be clipped.
- Text target: labels 12 logical px minimum, numeric readouts 11 logical px minimum. These are design minima; high-DPI scaling must preserve physical readability.
- Interactive hit target minimum 32×32 logical px.

## Advanced view

Advanced opens as a tabbed workspace with tabs Voice, Sub, Sources, Filter & Motion, Drive, Texture, Output. Each tab has a scroll container. Tabs do not resize the main macro controls. Scroll position is remembered per tab for the current session.

## Behavior

- Resizing changes available whitespace and scroll viewport; it does not scale knobs or text.
- Preset dirty marker updates immediately after any parameter change and clears after successful save/load.
- Modified-state confirmation is required before replacing unsaved user work.
- Value entry validates range and shows units. Escape reverts to prior value; Enter commits one host automation gesture.
- UI automation tests must use accessible names rather than brittle pixel-only locators wherever possible.

## Acceptance criteria

1. Screenshots at 720×480, 900×560, 1280×720 show no clipping or overlap.
2. High DPI at 150% and 200% preserves readable labels, minimum hit sizes, and no clipped dialogs.
3. Every Advanced tab scrolls to its last control and can return to top.
4. Main macros remain visible or one tab switch away while Advanced is open.
5. Unsaved edits survive canceled preset navigation and failed import.
6. All controls have accessible labels and keyboard traversal order follows visual grouping.
