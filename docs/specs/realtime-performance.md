# Feature Spec: Real-Time Safety and Performance

**Applies to:** all DSP and host-integration modules  
**Depends on:** parameter-state and complete engine

## Objective

Protect audio-thread deadlines and ensure stress, automation, and state changes cannot block or corrupt real-time rendering.

## Audio-thread rules

The render callback must not allocate/free memory, access files, perform logging, wait on mutexes, make network calls, or construct DSP tables. Resource creation and validation happen in prepare/load paths outside rendering. UI and state threads communicate through atomics, lock-free queues, or immutable snapshots with bounded handoff.

No callback path may depend on disk speed, UI event processing, or host tempo query latency. If host timing is unavailable, use cached valid values and report status asynchronously.

All per-voice DSP storage is allocated for the maximum supported 8 voices during prepare. Parameter event queues are bounded; overflow behavior must be deterministic, counted in diagnostics, and must not deadlock.

## Numerical safety

- Sanitize nonfinite host values before they enter DSP.
- Clamp values to registry ranges at the parameter boundary.
- Flush or avoid denormals in long release tails.
- Verify all filter and resonator coefficients remain stable for every accepted parameter combination and supported sample rate.
- Voice reset clears stale envelope/filter state and does not create a large transient.

## Performance targets

- Initial target: less than 10% of one documented modern desktop core for 8 voices at 48 kHz and 256 samples.
- Measure mono and poly, texture off/on, limiter off/on, at 44.1, 48, 96 kHz and block sizes 64, 256, 1024.
- Report median and 99th percentile callback time across at least 60 seconds after warmup. State CPU model, OS, compiler, build mode, and host/test harness.
- No callback may exceed the block deadline in the prescribed baseline test. Report worst callback separately rather than hiding it in an average.

## Acceptance criteria

1. Instrumented callback allocation counter remains zero during all test renders and automation sweeps.
2. Thread sanitizer or equivalent race checks pass for parameter automation, UI changes, and state restore where supported.
3. No callback lock wait or file access occurs in code review and runtime tracing.
4. CPU report meets or explicitly records deviation from the target; no performance result is assumed in this spec.
5. Parameter changes, note storms, preset load, and repeated prepare/release produce no deadlock, NaN/Inf, or use-after-free.
6. Event queue overflow test follows the documented bounded policy and remains recoverable.

## Evidence: engine-output blocks (Drive, Texture, Width, Limiter, OutputTrim, Oversampler)

Allocation check (acceptance 1, for these blocks): `tests/OutputStageTests.cpp` and `tests/OversamplerTests.cpp` replace the global `operator new` with a counter (`tests/EngineOutputTestUtil.h`) and arm it around `process` calls with automated parameter changes, oversampling mode switches and note-ons; both report 0 allocations (200 blocks of the full Drive + Oversampler + Texture + Width + Trim + Limiter chain; 100 blocks of mode-switching oversampler). Both executables were also run clean under AddressSanitizer + UBSan (`-O1 -fsanitize=address,undefined`). Thread-sanitizer, lock and file-access review remain with the integration worker (these blocks contain no atomics, locks or I/O).

Numerical safety (acceptance 5 in part): hostile inputs (NaN, +-Inf, +-1e30, +-3e38, denormals 1e-40) are sanitised at each block entry; tests assert finite, non-denormal output. Metal resonator poles are `r * rotation` with `r < 1` for all accepted tune/decay/sample-rate combinations (4 sample rates, 6 notes, 3 tunes, 3 decays verified finite and bounded). Known defect worth fixing in `DspUtil.h`: `Smoother` keeps float state and an absolute 1e-7 snap, which stalls short of the target (see processing-output.md); the output blocks use `SettlingSmoother` instead.

CPU measurement (acceptance 4): `tools/bench_output_stage.cpp`, `g++ 13.3.0 -O2 -DNDEBUG`, Linux 6.18 VM (shared host), Intel Xeon @ 2.80 GHz, single thread, no plug-in host (direct calls). Deviation from the protocol above: each row is **6 s of audio** (after discarding the first 10 %), not 60 s; only rates 48 and 96 kHz and blocks 64/256/1024 were run (44.1 kHz not run); texture is one voice (noise + metal), the limiter is stereo. Median / p99 / worst callback time per block, in microseconds:

| Block (48 kHz) | Drive Off | Drive 2x | Drive 4x | Texture (1 voice) | Width | Limiter on | Limiter bypassed |
|---|---|---|---|---|---|---|---|
| 64 | 0.73 / 1.0 / 497 | 1.92 / 6.4 / 6181 | 3.13 / 7.2 / 230 | 1.05 / 1.4 / 116 | 0.68 / 0.69 / 72 | 1.30 / 1.4 / 34 | 1.16 / 1.2 / 34 |
| 256 | 2.82 / 4.2 / 39 | 7.56 / 28.8 / 4064 | 12.31 / 31.8 / 4068 | 1.80 / 4.4 / 107 | 2.79 / 4.8 / 35 | 5.32 / 11.2 / 4057 | 4.69 / 26.3 / 4046 |
| 1024 | 11.14 / 32.8 / 36 | 30.16 / 71.4 / 229 | 49.76 / 80.3 / 90 | 7.11 / 44.2 / 71 | 10.41 / 32.2 / 43 | 21.51 / 55.8 / 2041 | 18.98 / 51.1 / 136 |

The worst-callback column is dominated by multi-millisecond stalls of the shared VM (the same code runs in 5 us medians), so it demonstrates nothing about the DSP's real worst case; a quiet-machine, 60 s, full-engine measurement is still required for the 10 % target. Medians show that the whole output chain with 4x oversampling costs well under 1 % of one core at 48 kHz (block 256: Drive 4x 12.3 + Width 2.8 + Limiter 5.3 us of a 5333 us budget, about 0.4 %), leaving the 10 % budget to the voices. No performance result for the complete synth is assumed.
