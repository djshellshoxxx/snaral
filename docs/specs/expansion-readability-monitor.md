# Feature Spec: Bass Readability Monitor

**Category:** Distinctiveness  
**Status:** Post-beta (not in v0.2 beta). It only reads existing engine taps; it adds no parameters to the registry.  
**Depends on:** processing-output, cdl-signal-visualization, macros-ui

## Objective

Show non-prescriptive diagnostics about sub stability, mid and sub overlap, stereo width and output peak, computed from the instrument's own signals.

## Measurements

Taps (engine diagnostics, lock-free): sub bus L/R before the sum, mid bus L/R before the sum, output L/R after the limiter. The audio thread accumulates per block only sums of squares, cross products and peaks into a fixed-size record pushed to a single-producer ring buffer; no FFT or allocation runs on the audio thread.

| Reading | Definition |
|---|---|
| Sub Peak / RMS | Peak and RMS of the sub bus (mono-summed), dBFS |
| Mid Peak / RMS | Peak and RMS of the mid bus, dBFS |
| Low-band L/R difference | Output low band (fourth-order Linkwitz-Riley low-pass at the chosen crossover) side energy relative to mid energy, `20 log10(RMS(L-R)/RMS(L+R))` dB, floored at -120 dB |
| Mid correlation | Pearson correlation of the output high band over the window, -1 to +1 |
| Output peak | Maximum post-limiter sample, dBFS, with Hold |

Selectable, from a fixed small set: crossover 90, 120 or 150 Hz (default 120); averaging window 100, 300 or 1000 ms (default 300); refresh 10, 20 or 30 Hz (default 20). The band filter runs in the measurement accumulator only while the panel is visible; hiding the panel stops measurement and releases the CPU cost.

## Behaviour and UI

- A read-only panel in the Output tab (and an optional compact strip in the Signal area) with Reset and Hold buttons. Reset clears windows and holds; Hold freezes the displayed peak values until released. Both are deterministic given the same input.
- No automatic parameter change, score, genre judgement or network access. Each reading shows its unit, averaging window and band. A note states that results depend on note, patch, sample rate and monitoring system.
- Updates are decoupled from the audio callback: the message thread drains the ring, computes display values, and drops stale records.

## Edge cases

Silence reads as `-inf` shown as "-120 dB"; sample rate changes recompute filters; 192 kHz uses the same Hz-defined bands; parameters changing during the window are simply averaged.

## Acceptance criteria

1. Readings match an offline reference computation within 0.5 dB over steady-tone, noise and transient fixtures at 44.1 and 96 kHz.
2. With the sub only on, the low-band L/R difference reads at or below -100 dB at every Space setting (centered sub).
3. The panel never blocks or allocates on the audio thread; hidden panel adds no per-sample work (checked by timing).
4. Reset and Hold behave deterministically over a scripted sequence.
5. Every label states unit, window and band; refresh stays within 10 to 30 Hz.

## Test fixtures

Generated stereo fixtures with known RMS, peak, side energy and correlation; the offline reference calculator lives in the test.
