# Feature Spec: Host Integration and Automation

**Module ID:** host-integration  
**Depends on:** parameter-state, midi-voice, motion-filter, expansion-oversampling  
**Consumers:** every plug-in format and the standalone target; macros-ui, expansion-midi-cc-learn, expansion-format-support

## Objective

Make Snaral behave as a well-formed MIDI instrument in every shipped format (see [format support](expansion-format-support.md)), follow host tempo and transport safely, expose stable automation, and report latency exactly.

## Plug-in contract (all formats)

- One stereo output bus, no audio input, one MIDI input, no MIDI output. `isBusesLayoutSupported` accepts only that layout. IS_SYNTH and NEEDS_MIDI_INPUT stay TRUE in CMake.
- Parameters come only from `snaral::parameterTable`. Every entry without `pfNoAutomation` is a host-automatable parameter (currently 91 of 99; the eight Trajectory point coordinates are state-only). JUCE `ParameterID` is `{id, 1}`. IDs never change; a retired ID is never reused.
- The host-bypass parameter is `bypass`. Bypass ramps the instrument output to silence over 10 ms (and back); voices keep running so un-bypass resumes mid-note.
- Persisted instance state is defined by parameter-state: every parameter, variation slots A to D plus active slot, A/B snapshots, MIDI CC mappings, and layer solo/mute (project state only). Theme, tooltip, animation, onboarding-seen, and device settings are per-user preferences and never enter host state or presets.
- Host state restore validates the whole candidate first and applies it at a block boundary (see parameter-state). Host automation stays authoritative during playback. UI refresh never writes parameters; UI edits use begin/end change gestures.
- No network access, no asset download, no file I/O from `processBlock`.

## Block, rate and layout handling

- Supported sample rates 44.1, 48, 88.2, 96, 192 kHz. Other rates must not crash (filter coefficients clamp below Nyquist) but are unsupported.
- `processBlock` accepts any `numSamples` from 0 up to the host maximum. If a host passes more than the prepared maximum, the processor renders in sub-blocks of the prepared size without allocating. Zero-length blocks are a no-op that still advances nothing.
- MIDI is sorted by sample offset, offsets clamped to the block, and split at event boundaries by the caller (the engine receives pre-sorted events).
- `prepareToPlay` may be called repeatedly with different rate and block size. It preserves all parameter and variation state, clears voices, and never allocates after it returns.

## Latency

`getLatencySamples()` returns `engine.latencySamples()` (limiter look-ahead plus oversampler delay). The value is set in `prepareToPlay` and re-published to the host whenever `oversampling_mode` or `limiter_enabled` changes. The change is requested through a lock-free flag and applied on the message thread. Sub and mid are delay-aligned inside the engine, so no host compensation beyond the reported value is needed.

## Tempo and transport

- Read `AudioPlayHead::getPosition()` once per block: BPM, play state, PPQ position, loop state. A tempo is valid when finite and in 20 to 999 BPM.
- Sync LFO rate: `Hz = (bpm / 60) / cycleBeats`, `cycleBeats = 4 * (1/denominator) * f` with `f` = 1 plain, 1.5 dotted, 2/3 triplet (so 1/4 is one beat). The Trajectory sync length uses the same beat table.
- Invalid or missing tempo: keep the last valid tempo for sync timing, set the status flag `tempoUnavailable` (read by the UI status line within 250 ms), and never produce a NaN phase. If no valid tempo has ever been seen, use the free rate (`lfo_rate_hz`).
- Free mode ignores the transport.
- `lfo_retrigger` on: phase resets to `lfo_phase` at each (non-legato) note-on. Off: phase is a running phase; in sync mode with a valid playing transport it is derived from PPQ so loops and repositions realign it without a discontinuity larger than the control smoothing.
- Transport transition playing to stopped: clear pedal-held state and release every held note through the normal release path (same as CC123). No transition is generated when the host exposes no play head (standalone).
- Loop jump or reposition while playing: no notes are cut by Snaral; stuck-note protection relies on the host note-offs and CC123 handling. LFO/Trajectory phase realigns without NaN.
- Offline or non-realtime rendering (`isNonRealtime()`): identical audio for identical input; UI timers and meters may be idle.

## Standalone

- Same processor, parameters, state and presets as the plug-ins. Audio and MIDI device selection use JUCE device management through Options; device loss keeps the last valid selection and shows an error, and re-prepares DSP without touching parameters, presets, or variation slots.
- The 440 Hz, -12 dBFS reference tone is generated in the standalone wrapper path only (after the processor), starts and stops only by explicit user action, and cannot exist in plug-in builds.
- Standalone has no play head; sync mode behaves as "tempo unavailable" with the free-rate fallback unless a standalone tempo control is later added by a recorded decision.

## Edge cases

- NaN or out-of-range host parameter values are sanitized at the parameter boundary.
- Host asking for a non-stereo layout is refused, not downmixed.
- State saved with a newer schema is rejected and leaves the active state untouched.
- Plug-in instantiated twice in one process shares no mutable global state.

## Acceptance criteria

1. Each shipped format is discovered and instantiated as an instrument that accepts MIDI and outputs stereo (pluginval strictness 5 plus the host matrix in [verification](verification.md)).
2. Every automatable registry parameter round-trips a host normalised value to the same DSP value within 1e-6 and changes the sound or UI without UI focus.
3. Host save and reopen restores parameters, variations, A/B, MIDI mappings, and solo/mute; two consecutive saves of an unchanged instance are byte-identical.
4. Tempo sync at 60, 120, 180 BPM produces cycle lengths within 1 sample of the formula; missing tempo yields the fallback and the status flag.
5. Transport stop, loop jump, and reposition leave no stuck note and no NaN/Inf over a 60 s randomized transport test.
6. Prepare/release cycles across the rate and block-size matrix preserve state and produce no spike above the smoothing criteria.
7. Reported latency equals measured impulse latency within 1 sample for each oversampling mode and limiter setting.
8. Standalone device change, loss, and recovery leave all parameter values and variation slots unchanged; the reference tone measures -12 dBFS within 0.1 dB and never appears in plug-in renders.

## Test fixtures

JUCE-free: tempo/beat-table conversion, transport-transition state machine, sub-block slicing. JUCE-dependent (`tests/juce/`): state round-trip, parameter-layout versus registry. Host-level: pluginval log and the host matrix table in the QA report; hosts not actually run are recorded as UNVERIFIED.
