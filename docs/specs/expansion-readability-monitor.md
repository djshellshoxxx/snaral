# Feature Spec: Bass Readability Monitor

**Category:** Distinctiveness  
**Depends on:** output-stage, macros-ui

## Objective
Show useful, non-prescriptive diagnostics for sub stability, mid/sub overlap, stereo width, and output peak.

## Behavior
- Read-only panel with Sub Peak/RMS, Mid Peak/RMS, low-band L/R difference, mid stereo correlation, and output peak.
- Frequency bands and averaging windows are visible and configurable only within a small documented set.
- Display updates at a bounded UI rate (10–30 Hz) from lock-free measurement snapshots.
- No automatic parameter changes, quality score, genre judgment, or network telemetry.
- Explain that readings depend on note, patch, sample rate, and monitoring system.

## Acceptance
1. Meter values match offline reference calculations within 0.5 dB over defined steady and transient fixtures.
2. Low-band L/R difference indicates centered sub at all Space settings.
3. UI refresh never blocks the audio callback and does not allocate from the audio thread.
4. Reset/hold functions behave deterministically.
5. Labels state units, averaging window, and measured frequency band.
