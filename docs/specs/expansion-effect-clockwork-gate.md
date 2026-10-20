# Effect Spec: Clockwork Gate

**Category:** Candidate effect  
**Depends on:** host-integration, motion-filter, mid-processing

## Objective
Create repeatable tempo-locked cuts and accents on the mid layer without disturbing the clean sub or MIDI note lifecycle.

## Controls
Rate/division, 8-step depth pattern, swing, smoothing, wet mix, retrigger/phase behavior, enable.

## Processing contract
- Pattern has 8 editable steps; each step is a normalized gain from 0–100%.
- Sync follows host tempo; free mode uses rate in Hz and fallback status.
- Swing shifts alternating step boundaries within a bounded 0–50% of step interval.
- Gate affects mid only and is post-drive/pre-width. Sub remains unprocessed.
- Pattern and phase are saved in preset/project state.

## Acceptance
1. Step boundaries match expected timing within 1 ms or one sample, whichever is larger.
2. Pattern repeats deterministically over 100 bars.
3. Smoothing controls transient edges and prevents clicks at minimum nonzero setting.
4. Tempo changes and transport loop jumps do not create stuck gates.
5. Sub output remains sample-identical across all pattern values and states.
