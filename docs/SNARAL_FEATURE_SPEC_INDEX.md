# Snaral Feature Specification Index

**Status:** Initial feature-level specifications  
**Parent:** [Initial Developer Specification](SNARAL_INITIAL_DEVELOPER_SPEC.md)  
**Source brief:** [Dark DnB Bass Synth Design Brief](DARK_DNB_BASS_SYNTH_DESIGN_BRIEF.md)

This index defines the capability map and implementation dependency order. Each feature document supplies behavior contracts and acceptance checks. The parent spec remains authoritative for product scope and shared limits. A narrower feature spec clarifies details but must not weaken the centered-sub, stable-state, or real-time requirements.

## Capability map

| Module ID | Responsibility | Depends on |
|---|---|---|
| parameter-state | Stable parameter IDs, normalized values, defaults, smoothing handoff, state snapshots | — |
| midi-voice | MIDI events, voice modes, pitch, velocity, bend, glide, note lifecycle | parameter-state |
| sub-layer | Dedicated low foundation generation and level path | parameter-state, midi-voice |
| mid-source | Pitched mid sources, source blend, source position | parameter-state, midi-voice |
| motion-filter | Note contour, filter emphasis, LFO sync/free and bounded destinations | parameter-state, midi-voice, mid-source |
| mid-processing | Mid-only drive, optional noise/metal, stereo width | parameter-state, mid-source, motion-filter |
| output-stage | Layer summing, centered sub invariant, output trim and limiter | sub-layer, mid-processing |
| host-integration | Host tempo/transport, automation, plug-in and standalone behavior | parameter-state, midi-voice, motion-filter |
| preset-variation | JSON presets, validation/migration, user files, four named snapshots | parameter-state |
| macros-ui | Main and Advanced UI, macro mappings, accessibility and status | parameter-state, host-integration, preset-variation |
| verification | Unit, integration, audio, UI, stress, format and release checks | all modules |

## Build order

1. parameter-state
2. midi-voice
3. sub-layer and mid-source
4. motion-filter
5. mid-processing
6. output-stage
7. host-integration
8. preset-variation
9. macros-ui
10. integrated verification

The sub-layer and mid-source can be developed independently once parameter-state and midi-voice contracts are fixed. Do not add a broad modulation matrix, effect rack, sample player, or generated randomizer under these modules.

## Global engineering requirements

- Keep DSP independent from UI and file-format code.
- The audio callback performs no allocation, file I/O, or blocking lock acquisition.
- State restoration applies a complete validated snapshot at a block boundary.
- Stable parameter IDs and normalized values are shared by DSP, UI, automation, presets, and tests.
- Each module has boundary unit tests. Cross-module acceptance remains in the parent specification.
- Numeric ranges and CPU targets are design targets; record listening or measurement results only after running them.

## Product planning

- [Staged expansion roadmap](SNARAL_EXPANSION_ROADMAP.md)

- [Feature opportunities and candidate effects](SNARAL_FEATURE_OPPORTUNITIES.md)

## Feature specifications

### Engine and controls

- [Parameter and state foundation](specs/parameter-state.md)
- [MIDI voice and pitch](specs/midi-voice.md)
- [Sub and mid source engine](specs/sub-mid-engine.md)
- [Oscillator bank and spectral sources](specs/oscillator-bank.md)
- [Filter and movement](specs/motion-filter.md)
- [Macro mapping](specs/macro-mapping.md)
- [Mid processing and output](specs/processing-output.md)
- [Real-time safety and performance](specs/realtime-performance.md)
- [Performance foundation: envelopes, sustain, tuning, Panic, and layer audition](specs/performance-foundation.md)

### User workflow and content

- [Macros and user interface](specs/macros-ui.md)
- [Main and Advanced UI layout](specs/ui-layout.md)
- [Presets and variations](specs/preset-variation.md)
- [Preset file contract](specs/preset-file-contract.md)
- [Factory preset set](specs/factory-presets.md)

### Integration and quality

- [Host integration and automation](specs/host-integration.md)
- [Verification and release gates](specs/verification.md)
