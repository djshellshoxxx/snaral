# Feature Spec: Host Integration and Automation

**Module ID:** host-integration  
**Depends on:** parameter-state, midi-voice, motion-filter  
**Consumers:** VST3 and standalone targets; macros-ui

## Objective

Make Snaral behave as a MIDI instrument in a DAW, follow transport tempo when requested, and expose stable, useful automation parameters.

## VST3 contract

- Report an instrument that accepts MIDI and produces stereo audio.
- Expose all five macros and meaningful Advanced parameters as host automatable parameters.
- Parameter IDs remain stable across builds and label changes.
- Support host state save/restore, sample-rate and block-size prepare/release, tempo/play-state queries, and latency reporting if the limiter introduces latency.
- No network access or external asset download.
- Host state restore validates before applying. Host-provided parameter automation remains authoritative during playback.

## Tempo and transport

- Sync mode uses host BPM and division. BPM values must be finite and within the host-reported valid range.
- Missing or invalid tempo uses last valid free rate and exposes a status state to the UI.
- Transport stop releases all held notes. Loop jumps and repositioning do not leave stuck notes or generate invalid LFO phase.
- Free mode continues at configured rate independent of host transport.
- Retrigger-on resets LFO phase at note-on; retrigger-off preserves running phase.

## Standalone target

- Standalone exposes the same DSP and parameter behavior as VST3.
- Provide MIDI input device selection and output device/audio device configuration using JUCE facilities.
- Device changes prepare/release DSP safely and do not lose the saved patch.
- Standalone preset and variation behavior matches VST3.

## Acceptance criteria

1. VST3 is discovered and instantiated in the documented host test matrix as a MIDI instrument.
2. Host piano roll and physical MIDI input trigger notes; audio stops after note release.
3. Automating each main macro changes its value and sound without UI focus.
4. Host save/reopen restores patch and variation state.
5. Tempo sync follows host BPM at 60, 120, and 180 BPM; missing tempo invokes fallback status.
6. Transport stop and loop repositioning produce no stuck notes or audio NaN/Inf.
7. Sample-rate/block-size changes preserve parameter state and produce no crash or unbounded spike.
8. Standalone device changes do not alter preset values or variation slots.
