# Feature Spec: Signal Visualization and CPU Gauge

**Requirement source:** Copied approved baseline at ../standards/AUDIO_APP_BASELINE.md  
**Applies to:** Snaral plug-in and standalone

## Objective
Provide visible signal feedback and an honest estimate of Snaral’s own processing cost.

## Visualization
- Add a live output oscilloscope showing recent post-limiter stereo output. Include mono/sub view toggle and mid-only view toggle for diagnosis.
- Scope data is read-only, buffered through a bounded lock-free transfer, and refreshed at 20–30 Hz.
- Hide/show is available in Options and persists per user. Hidden visualization stops UI drawing work but does not alter audio.
- Never imply that the waveform proves sound quality.

## CPU gauge
- Measure Snaral callback processing time divided by available block deadline; display as percentage of one audio core.
- Clearly label it “Snaral DSP” and optionally show “host total” only if the host provides a real value. Do not infer host-wide CPU from plug-in callback time.
- Smooth display over 1 second and show peak/overrun indicator if callback exceeds the block deadline.
- Gauge can be disabled in Options; measurement collection then stops if it has measurable cost.

## Acceptance
1. Scope samples match post-limiter output within meter-buffer tolerance; hidden scope causes no state/audio change.
2. UI drawing is bounded and never locks or allocates on the audio thread.
3. CPU gauge calculation matches instrumented callback/block timing within 5 percentage points in the harness.
4. Overrun indicator triggers only on measured over-deadline callback.
5. Tooltips/help explain signal point, update rate, and CPU estimate scope.
