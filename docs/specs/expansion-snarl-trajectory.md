# Feature Spec: Snarl Trajectory Editor

**Category:** Distinctiveness  
**Depends on:** motion-filter, parameter-state, macros-ui

## Objective
Replace a generic envelope-only movement workflow with an editable body → peak → return trajectory while retaining the existing contour as the default mode.

## Behavior
- Provide 3–8 draggable control points, minimum 3, with time and depth axes.
- Time is in milliseconds or host-synced musical divisions; endpoints are fixed at note-on and the configured return point.
- Initial destinations are filter emphasis and mid source position only. No arbitrary matrix.
- Shapes between points: linear, smoothstep, and eased exponential.
- One-shot trajectories retrigger with note contour; legato behavior follows voice settings.
- Clamp depth to destination-specific safe ranges. The sub is excluded.
- Store points, units, sync division, shape, and trajectory version in patch/project state.

## Acceptance
1. Rendered destination curve matches exported control points within one sample in deterministic tests.
2. Dragging a point cannot cross adjacent points or create zero/negative segment duration.
3. Sync mode follows host tempo; free mode follows milliseconds.
4. Preset/state round-trip preserves point order and values.
5. Extreme curves remain finite, click-safe, and do not change sub output.
