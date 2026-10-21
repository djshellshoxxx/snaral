# Feature Spec: Signal Visualization and CPU Gauge

**Requirement source:** [Copied approved baseline](../../standards/AUDIO_APP_BASELINE.md)
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

## Implementation notes (UI kit)
- Code: `Source/UI/Displays.{h,cpp}` (`ScopeView`, `LevelMeter`, `CpuGauge`), `Source/UI/UiSources.h` (`ScopeSource`, `MeterSource`, `CpuSource` and the lock-free helpers `ScopeRingBuffer`, `AtomicMeterSource`, `AtomicCpuSource`).
- The UI only calls the abstract sources on the message thread. The engine side implements them with atomics; no UI code runs on the audio thread.
- `ScopeView` refreshes at 30 Hz, or 2 Hz when animations are off (reduced motion). Its timer runs only while the component is showing and enabled; `setDisplayEnabled(false)` (Options) stops all drawing work. Views: Output, Sub, Mid (disabled when the source has no such tap) and a Mono sum. In narrow layouts the tap buttons collapse into one cycling button. The caption describes the tap and never claims sound quality.
- `LevelMeter` shows RMS bars with a peak overlay, a 1.5 s peak hold that then falls at 20 dB/s, numeric PK and RMS readouts in dBFS, and a CLIP indicator that appears only when a reading reached full scale, stays for 2 s and can be cleared by a click. No data shows dashes, never a stale clip state.
- `CpuGauge` is labelled "DSP CPU" (the task brief takes precedence over the "Snaral DSP" wording above; the tooltip says it is one audio core of Snaral's own processing). It smooths over 1 s, shows an OVERRUN marker for 3 s only when the source reports a measured over-deadline callback, shows ">100%" above full load and "--" without data. It never infers host load.
- Tests: ballistics, honest-data behaviour, clip latch, timer gating and the ring buffer are in `tests/juce/UiKitTests.cpp`; renderings are produced by `tools/ui_snapshot`.
