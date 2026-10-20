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
