# Feature Spec: Nonlinear Oversampling Modes

**Category:** Product value  
**Depends on:** mid-processing, output-stage, realtime-performance, host-integration

## Objective
Reduce aliasing from nonlinear mid processing with user-selectable CPU/quality levels.

## Behavior
- Modes: Off, 2×, 4×. Default Off until listening/CPU review selects a product default.
- Apply oversampling only around nonlinear mid stages where it materially improves aliasing. Sub oscillator remains at base rate.
- Use a documented high-quality resampling filter with bounded state and group delay.
- Report plug-in latency to hosts exactly and preserve delay alignment between sub and mid.
- Mode switching is smoothed or deferred to a safe boundary and does not reset notes.
- No hidden adaptive quality switching.

## Acceptance
1. Aliasing measurement improves monotonically from Off to 2× to 4× on prescribed nonlinear test tones.
2. Reported latency equals measured impulse-response latency within one sample.
3. Sub/mid alignment stays within one base-rate sample.
4. CPU/memory are recorded for each mode at supported rates and voice counts.
5. Mode changes during sustained notes produce no crash, stale state, or click beyond smoothing criteria.
