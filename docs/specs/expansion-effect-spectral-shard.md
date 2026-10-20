# Effect Spec: Spectral Shard

**Category:** Candidate effect  
**Depends on:** mid-processing, realtime-performance, host-integration

## Objective
Freeze a short midrange spectral frame and reshape it into a metallic, blurred, shifting layer.

## Controls
Capture trigger, hold, shift amount, blur, decay, wet mix, enable.

## Processing contract
- Use fixed-size preallocated analysis/synthesis buffers; maximum capture duration 250 ms.
- Capture is explicit from UI or MIDI trigger; no continuous unbounded recording.
- Frequency shift and blur are bounded. Any latency is fixed, declared, and reported to the host.
- Transport stop, Panic, preset replacement, and disable clear capture state safely.
- Mid-only, disabled by default. No disk writes or external assets.

## Acceptance
1. Memory use remains fixed and documented across all settings.
2. Reported latency matches measured latency within one sample.
3. Capture/release produces no NaN/Inf, unbounded tail, or stuck state.
4. Disable returns dry mid with latency alignment preserved.
5. Sub path is unchanged; CPU target is recorded for 8 voices and maximum buffer duration.
