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

## Filter design and latency (engine-output, implemented in `Source/Dsp/Oversampler.h`)

Cascaded linear-phase polyphase halfband FIR stages. Each stage is a Kaiser-windowed (beta = 8.0, half length c + 1) sinc halfband: length N = 4K + 3, centre tap c = 2K + 1 = 0.5, every other even-offset tap exactly zero, non-centre taps normalised to sum 0.5 (unity DC gain, up-sampler gain 2 applied in the taps). Only the non-zero polyphase branch is computed, with the symmetric taps folded (K + 1 multiply-adds per output sample).

| Stage | Runs at | K | Taps | Measured passband (own rate) | Measured stopband |
|---|---|---|---|---|---|
| 1 (base <-> 2x) | 2 fs | 11 | 47 | 0..0.200 (0.4 fs): ripple -0.004..+0.001 dB | 0.300..0.5 (>= 0.6 fs): <= -63.4 dB |
| 2 (2x <-> 4x) | 4 fs | 4 | 19 | 0..0.125 (0.5 fs): ripple -0.0005..+0.0016 dB | 0.375..0.5 (>= 1.5 fs): <= -74.8 dB |

Stage 2 carries `pad = 2` extra samples of delay at 4x so that the 4x total is an exact integer number of base-rate samples. Up-sampling = zero-stuff + filter; down-sampling = filter + decimate by 2. Odd-phase output of an up-sampler is a pure delayed copy of the input (the centre tap), so the cost is one short FIR per two output samples.

Exact latency in base-rate samples (identity nonlinearity), independent of sample rate in samples:

| Mode | Formula (high-rate samples / factor) | Latency | At 44.1 / 48 / 96 kHz |
|---|---|---|---|
| Off | 0 | 0 | 0 |
| 2x | 2 c1 / 2 = 46 / 2 | 23 | 0.52 / 0.48 / 0.24 ms |
| 4x | 23 + (2 c2 + pad) / 4 = 23 + 20 / 4 | 28 | 0.63 / 0.58 / 0.29 ms |

`Oversampler::latencyFor(mode)` is a `constexpr` function and `latencySamples()` returns it for the active mode. The host-reported plug-in latency is `oversampler latency + limiter latency` (Limiter: 50 samples at 48 kHz, see processing-output.md); the engine must delay the sub layer (and the dry sub path) by the oversampler latency so that sub and mid stay aligned within one base-rate sample, using `DelayAlign` (preallocated integer delay line, also used inside `Drive` for its dry/wet mix).

API: `os.setMode(mode)` (any thread-safe-by-caller point, applied at the next block boundary), `os.process(io, n, fn)` where `fn(float* buf, int count)` runs in place on `count = n * factor` samples, `latencySamples()`, `reset()`. All scratch is sized in `prepare(sr, maxBlock)`; `process` chunks internally if `n > maxBlock`. Results are bit-identical for any block partition (tested at block sizes 1, 37, 1000).

Mode switching: the filters are cleared (voices and envelopes are not touched) and the output holds its last sample while the new path warms up (2 x latency + 4 samples), then crossfades to it over 128 samples (2.7 ms at 48 kHz). The stress test switches mode every 256-sample block on a sustained 110 Hz tone through a tanh shaper: max sample-to-sample step 0.021 (natural step of the tone 0.007), no step above 0.05, all finite. The reported latency changes with the mode, so the host should be told when the user switches; an automatic host latency-change notification is the integration worker's job.

### Measured results (all from `tests/OversamplerTests.cpp`, 48 kHz, run on the machine below)
- Latency: impulse-response peak index equals the reported latency exactly (Off 0, 2x 23, 4x 28); Off is bit-exact pass-through with zero latency.
- Passband: identity-nonlinearity sine gain error <= 0.029 dB (2x) and <= 0.036 dB (4x) from 60 Hz to 19.2 kHz.
- Aliasing: hard clip (gain 8, ceiling +-1) of a coherent 3.0 kHz sine at -6 dBFS peak, power of all non-harmonic spectral bins relative to the fundamental: Off -26.7 dB, 2x -42.1 dB, 4x -51.9 dB (monotonic). With the Drive wave folder at 36 dB drive (alias power relative to total): Off -8.9 dB, 2x -23.5 dB, 4x -34.4 dB.
- Dry/wet alignment: DelayAlign(latency) of the dry vs the identity-processed wet differs by <= 1.2e-6 (220 Hz sine) in both modes; an oversampled Drive with unity-gain folder and mix 50 % equals the delayed input within 0.005.
- Acceptance 1, 2, 5 (smoothing part) are covered by those tests. Acceptance 3 (sub/mid alignment in the full engine) and 4 at all supported rates and voice counts are open until engine-integration exists.

### CPU estimate
Measured with `tools/bench_output_stage.cpp` (Release, `g++ 13.3.0 -O2 -DNDEBUG`, Linux 6.18 VM, Intel Xeon @ 2.80 GHz, single thread, 6 s of audio per row after 10 % warm-up, shaper = warm saturation, mono mid bus, i.e. ONE bus-level Drive instance, not per voice). Median per block:

| Rate / block | Drive Off | Drive 2x | Drive 4x |
|---|---|---|---|
| 48 kHz / 64 | 0.73 us (0.055 %) | 1.92 us (0.14 %) | 3.13 us (0.23 %) |
| 48 kHz / 256 | 2.82 us (0.053 %) | 7.56 us (0.14 %) | 12.31 us (0.23 %) |
| 48 kHz / 1024 | 11.14 us (0.052 %) | 30.16 us (0.14 %) | 49.76 us (0.23 %) |
| 96 kHz / 256 | 2.82 us (0.11 %) | 7.54 us (0.28 %) | 12.29 us (0.46 %) |

Percentages are of the block duration (one core). Memory: 2x and 4x scratch = 6 x maxBlock floats plus about 10 KB of filter history/extension buffers per instance at maxBlock 256 (about 16 KB total) (no per-voice allocation). 99th percentile and worst-case figures are in realtime-performance.md; the worst values on this shared VM include multi-millisecond scheduler spikes that are not caused by the DSP. These numbers cover only the oversampled drive path, not the whole synth; full-engine CPU per mode (acceptance 4) is to be measured after integration.
