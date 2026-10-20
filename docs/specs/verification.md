# Feature Spec: Verification and Release Gates

**Module ID:** verification  
**Depends on:** all feature modules  
**Consumers:** developers, QA, release owner

## Objective

Create repeatable evidence that each feature works in isolation and that the integrated instrument meets its audio, state, host, UI, and performance requirements.

## Test layers

1. **Unit:** parameter conversion and mapping, voice allocation, oscillator pitch, envelopes, LFO timing, filter stability, drive limits, JSON validation/migration.
2. **Integration:** sub/mid isolation, MIDI-to-audio, full state restore, macros to DSP destinations, host-tempo fallback, limiter/width invariants.
3. **Stress/fuzz:** valid endpoint and midpoint parameter combinations; malformed preset files; randomized MIDI event order; repeated prepare/release and buffer-size changes.
4. **Audio render:** deterministic renders for pitch, aliasing review, clicks, stereo/sub invariants, drive compensation, output ceiling, and preset comparisons.
5. **UI/host:** window sizes, scroll regions, high DPI, automation gestures, preset actions, VST3 discovery, project reopen, standalone device changes.
6. **Manual listening:** level-matched review in solo and dense DnB context, with notes linked to preset, note, sample rate, and build.

## Required coverage matrix

- Sample rates: 44.1, 48, 88.2, 96, 192 kHz.
- Block sizes: 1, 64, 128, 256, 512, 1024, 2048.
- Notes: 12, 24, 36, 48, 60, 72, 84, 96; guaranteed tracking range is 24–84.
- Voice modes: mono, legato, glide, polyphony 1–8.
- Host conditions: valid tempo, missing tempo, transport stop, loop jump, state restore.
- Space and layer checks: Weight zero/max, Growl zero/max, Space zero/max, drive zero/max, limiter on/off.
- Presets: every factory preset, one full-featured fixture, malformed and future-version fixtures.

## Release gates

A release candidate requires:
- All functional and state acceptance checks in the parent spec pass.
- No known crash, stuck-note, NaN/Inf, or sub-centering failure.
- Build succeeds for VST3 and standalone on declared platforms.
- Performance test records machine, compiler, sample rate, block size, voice count, and measured CPU.
- Host matrix identifies exact application versions tested.
- UI manual/screenshot review covers default/minimum sizes and Advanced scrolling.
- Factory presets and all embedded assets have provenance/license records.
- Test report contains command, result, timestamp/build identifier, and failures. Do not report tests that were not run.

## Failure triage

Classify failures as DSP correctness, MIDI/host integration, state compatibility, UI/UX, performance, or test harness. A harness failure must be isolated from a plug-in failure. Preserve minimal reproduction data: parameter snapshot, MIDI sequence, host timing state, sample rate, block size, host/build details, and audio render when available.

## Build commands

Exact build and test commands are intentionally unresolved because the repository currently contains no project source or build configuration. During implementation Milestone 0, record the selected JUCE version, toolchain matrix, configured formats, and exact configure/build/test commands here or in the repository build guide before the first code milestone is accepted.
