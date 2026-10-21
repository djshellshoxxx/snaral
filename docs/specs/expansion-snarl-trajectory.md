# Feature Spec: Snarl Trajectory Editor

**Category:** Distinctiveness  
**Depends on:** motion-filter, parameter-state, macros-ui  
**Implemented in:** `Source/Dsp/Trajectory.h` (curve + playback), `Source/Dsp/Contour.h` (`MotionRouting`); tests in `tests/TrajectoryTests.cpp`

## Objective
Replace a generic envelope-only movement workflow with an editable body → peak → return trajectory while retaining the existing contour as the default mode.

## Behavior (beta decisions)
- **Five draggable control points** (the beta fixes the count; 3-8 points is a post-beta extension). Time axis `x` and depth axis `y`, both normalised 0..1. Point 1 is pinned at `x1 = 0` (note-on) and point 5 at `x5 = 1` (the return point); `x2..x4` and `y1..y5` are the parameters `traj_x2..x4`, `traj_y1..y5` (not host-automatable, saved in presets and project state).
- **Time** is either milliseconds (`traj_length_ms`, 50..4000, log) or host-synced (`traj_length_div`: 1/16, 1/8, 1/4, 1/2, 1/1, 2/1 of a quarter-note grid) selected by `traj_time_mode`.
- **Destinations** (`traj_dest`): `cutoff`, `source_position`, or `both`. No arbitrary matrix. The sub is never modulated (the trajectory only produces a number; the sub oscillator takes only a frequency).
- **Shape** between points: one smooth curve whose tension is `traj_curve` (0 % = straight segments, 100 % = fully smooth monotone cubic). The earlier "linear / smoothstep / eased exponential" per-segment list is replaced by this single control for the beta.
- **One-shot / loop** (`traj_loop`). One-shot holds the return value `y5` after the length; loop wraps. A note-on from idle always starts the trajectory; a retriggering note-on restarts it at `u = 0` with a 2 ms click-safe offset decay; a legato note (retrigger = false, from the voice settings) while running is ignored so the trajectory keeps playing.
- **Replaces the contour** on its destinations while enabled (see `MotionRouting` in motion-filter.md); the contour is the default movement when the trajectory is off. The trajectory is not scaled by the Motion macro; it has its own `traj_depth`.
- Note-off does not stop the trajectory (the voice's amp envelope owns the tail).

## Exact formulas

Parameter decoding (`TrajectoryParams::from(snapshot)`): `depth = traj_depth / 100`, `c = traj_curve / 100`, `x = {0, traj_x2, traj_x3, traj_x4, 1}`, `y = {traj_y1 .. traj_y5}`.

**Point sanitising** (`TrajectoryCurve::sanitizePoints`, applied before every evaluation; idempotent), `g = 0.02`:

```
y_i  = clamp(y_i, 0, 1)            non-finite -> 0.5
x_1  = 0                           x_5 = 1
x_2  = clamp(x_2, g,        1 - 3g)
x_3  = clamp(x_3, x_2 + g,  1 - 2g)
x_4  = clamp(x_4, x_3 + g,  1 - g)
```

So a dragged point is stopped against its neighbour's minimum gap, every segment has duration at least 2 % of the length (1 ms at the 50 ms minimum), and `x` is strictly increasing. Non-finite `x` counts as 0 before clamping.

**Curve** `T(u)`, `u` clamped to 0..1, segment `k` with `x_k <= u <= x_{k+1}` (the first such segment), `h = x_{k+1} - x_k`, `t = (u - x_k) / h`:

```
lin   = y_k + t (y_{k+1} - y_k)
herm  = (2t^3 - 3t^2 + 1) y_k + (t^3 - 2t^2 + t) h m_k + (-2t^3 + 3t^2) y_{k+1} + (t^3 - t^2) h m_{k+1}
T(u)  = clamp( lin + c * ( clamp(herm, min(y_k,y_{k+1}), max(y_k,y_{k+1})) - lin ), 0, 1 )
```

Tangents `m` (Fritsch-Carlson monotone, secants `d_k = (y_{k+1} - y_k) / h_k`, `h_k = x_{k+1} - x_k`):

```
m_1 = d_1,   m_5 = d_4
interior k = 2..4:  m_k = 0                                   if d_{k-1} d_k <= 0
                    m_k = (w1 + w2) / (w1/d_{k-1} + w2/d_k)   otherwise,  w1 = 2 h_k + h_{k-1},  w2 = h_k + 2 h_{k-1}
```

Properties (all tested): `T(0) = y_1`, `T(1) = y_5`, `T(x_k) = y_k`; inside every segment `T` stays between its two end values (no overshoot, hence always within 0..1) and is monotone; `c = 0` is exactly piecewise linear; `c = 1` has continuous slope at the interior points.

**Length in samples** `L` (`Trajectory::lengthSamples`): `ms = traj_length_ms` (clamped 50..4000); if `traj_time_mode = sync` and the host tempo is valid, `ms = beats(div) * 60000 / bpm` with `beats = {0.25, 0.5, 1, 2, 4, 8}` for `{1/16, 1/8, 1/4, 1/2, 1/1, 2/1}` (a quarter note is one beat, so `1/4` at 120 BPM is 500 ms); `L = max(1, round(ms * sr / 1000))`. BPM is clamped to 10..999. Free mode ignores the tempo.

**Tempo fallback:** with sync selected and no valid tempo (`bpmValid` false, or bpm not finite/positive) the length falls back to `traj_length_ms`; `Trajectory::usingFallbackLength()` is true and the UI shows "Host tempo unavailable - using ms length" (`Trajectory::fallbackMessage`). A tempo change while running only changes the per-sample increment, so the position `u` is continuous.

**Playback:** `u` is a double accumulator, `u_0 = 0` at note-on, the output for sample `n` is `T(u_n)` and then `u += 1 / L`. At `u >= 1`: one-shot holds `u = 1` (output `y_5`); loop wraps `u -= floor(u)`, and if `y_1 != y_5` the wrap jump decays linearly to zero over 2 ms. After the fade the n-th sample equals `T(n / L)` to float precision, i.e. the rendered destination curve matches the control points within one sample. Before the first note-on `process()` returns `y_1`.

**Click-safe retrigger:** `offset = last_output - T(0)`; for the next 2 ms the output is `clamp(T(u) + offset * remaining / total, 0, 1)`.

**Routing** (in `MotionRouting`): `traj` (0..1) adds to the cutoff in semitones as `traj * (traj_depth * 60 + bitePeak)` for `dest` = cutoff/both (so depth 100 % is up to +5 octaves, the same range as the contour, clamped to 20 Hz..20 kHz in the log domain), and to the source position as `traj * traj_depth * 0.5` for `dest` = source_position/both, clamped to 0..1. The depth clamp is therefore destination specific and bounded.

## Acceptance
1. Rendered destination curve matches exported control points within one sample in deterministic tests. **Test:** playback equals `T(n/L)` to 2e-5 for every sample at 44.1 / 48 / 96 kHz; each control point value lies between the two samples around `x_k * L`; one-shot holds `y_5`.
2. Dragging a point cannot cross adjacent points or create zero/negative segment duration. **Test:** exhaustive grid of extreme/non-finite `x` plus 20000 random layouts always satisfy `x_1 = 0`, `x_5 = 1`, neighbour gap >= 0.02, and sanitising is idempotent; a valid layout is never altered.
3. Sync mode follows host tempo; free mode follows milliseconds. **Test:** 6 divisions at 60 / 120 / 180 BPM give `beats * 60000 / bpm` ms within one sample; a tempo change moves the length; ms mode ignores the tempo; missing tempo falls back and raises the flag.
4. Preset/state round-trip preserves point order and values. The parameters are plain floats; the test checks that the snapshot decodes to the same ordered control points (`traj_x2..4` and `traj_y1..5` sanitised only when out of range). The preset JSON round trip itself belongs to the state worker.
5. Extreme curves remain finite, click-safe, and do not change sub output. **Test:** zig-zag `y` with all `x` at the minimum gaps, 100 % tension, 50 ms length, loop, retrigger every 777 samples at 192 kHz stays finite and within 0..1; retrigger and loop wrap are continuous (max step 0.008 in the tested case); determinism: two renders from `reset()` are bit-identical.
6. No allocation in the process path (test replaces `operator new` and counts zero).
