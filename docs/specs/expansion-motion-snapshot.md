# Feature Spec: Motion Snapshot and Loop

**Category:** Distinctiveness  
**Depends on:** host-integration, parameter-state, macros-ui

## Objective
Capture a short hand-performed macro movement and replay it as a repeatable free or tempo-synced loop.

## Behavior
- Record one to three selected lanes from Growl, Bite, Motion, and Space. Weight is excluded by default to protect sub balance.
- Maximum loop length: 8 bars synced or 30 seconds free. Quantization is off, 1/8, 1/16, or 1/32.
- Record and playback state is explicit; overdub is excluded from first version.
- Interpolate using linear or smooth curves. Playback is deterministic and follows host loop/tempo changes without resetting unrelated voices.
- Save lane points, sync/free setting, length, quantization, and version with patch state.
- Motion Snapshot cannot write automation back to the DAW; it generates internal parameter values only.

## Acceptance
1. Record/replay returns to the same control values and timing within one sample or 1 ms, whichever is larger.
2. Host tempo change preserves musical loop length in synced mode.
3. Loop wrap is click-free under the control smoothing criteria.
4. Saved loop survives project reopen and renders deterministically.
5. Weight and sub pitch remain unchanged unless a future explicit option is approved.
