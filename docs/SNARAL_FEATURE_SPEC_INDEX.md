# Snaral Feature Specification Index

**Status:** Feature-level specification index (updated for the v0.2 beta scope)
**Parent:** [Initial Developer Specification](SNARAL_INITIAL_DEVELOPER_SPEC.md)
**Beta scope and module ownership:** [Beta Implementation Plan](IMPLEMENTATION_PLAN.md)
**Source brief:** [Dark DnB Bass Synth Design Brief](DARK_DNB_BASS_SYNTH_DESIGN_BRIEF.md)

This index defines the capability map, the status of every feature spec, and the implementation dependency order. Each feature document supplies behaviour contracts and acceptance checks. The parent spec remains authoritative for product scope and shared limits. A narrower feature spec clarifies details but must not weaken the centered-sub, stable-state, or real-time requirements.

Every spec listed here has objective, contract, edge cases, acceptance criteria and test fixtures. "Spec complete" means the document is implementable; it does not mean the feature is implemented. Implementation status is tracked in the [Development Plan](DEVELOPMENT_PLAN.md).

## Status legend

- **Core**: required for the first release and for the beta.
- **Beta**: promoted from the expansion roadmap into the v0.2 beta by the implementation plan.
- **Post-beta**: fully specified, scheduled after the beta; do not implement yet.

## Capability map

| Module ID | Responsibility | Depends on |
|---|---|---|
| parameter-state | Stable parameter IDs, normalized values, defaults, smoothing handoff, state snapshots | none |
| midi-voice | MIDI events, voice modes, pitch, velocity, bend, glide, note lifecycle | parameter-state |
| sub-layer | Dedicated low foundation generation, level path, optional Sub Anchor gain | parameter-state, midi-voice |
| mid-source | Pitched mid sources, source blend, source position | parameter-state, midi-voice |
| motion-filter | Note contour, filter emphasis, LFO sync/free, Snarl Trajectory | parameter-state, midi-voice, mid-source |
| mid-processing | Mid-only effects (Furnace, Fracture), drive, texture, stereo width, oversampling | parameter-state, mid-source, motion-filter |
| output-stage | Layer summing, centered sub invariant, output trim, limiter, latency | sub-layer, mid-processing |
| host-integration | Transport, automation, formats, standalone behaviour, latency | parameter-state, midi-voice, motion-filter |
| preset-variation | JSON presets, validation/migration, user files, four snapshots, browser, undo and A/B | parameter-state |
| macros-ui | Main and Advanced UI, macro mappings, accessibility, status | parameter-state, host-integration, preset-variation |
| cdl-services | Shared actions, Randomize, MIDI Learn, WAV export, themes, help | macros-ui, host-integration |
| verification | Unit, integration, audio, UI, stress, format, QA, audit and release checks | all modules |

## Build order

1. parameter-state
2. midi-voice and performance-foundation
3. sub-layer and mid-source
4. motion-filter (including Trajectory)
5. mid-processing (effects, oversampling) and output-stage
6. host-integration and formats
7. preset-variation (presets, undo, A/B, browser)
8. macros-ui and cdl-services
9. integrated verification: [verification](specs/verification.md), [audit](specs/audit-checklist.md), [QA](specs/qa-plan.md), [packaging](specs/release-packaging.md)

Sub-layer and mid-source can be developed independently once parameter-state and midi-voice contracts are fixed. Do not add a broad modulation matrix, a general effect rack, a sample player, or an unbounded randomizer under these modules; the beta effects are two fixed, mid-only blocks.

## Global engineering requirements

- Keep DSP independent from UI and file-format code.
- The audio callback performs no allocation, file I/O, or blocking lock acquisition.
- State restoration applies a complete validated snapshot at a block boundary.
- Stable parameter IDs and normalized values are shared by DSP, UI, automation, presets, and tests. The registry is `Source/Parameters/ParameterTable.h` with `ParameterList.def`.
- Each module has boundary unit tests. Cross-module acceptance remains in the parent specification.
- Numeric ranges and CPU targets are design targets; record listening or measurement results only after running them.

## Project guidance

- [Beta Implementation Plan](IMPLEMENTATION_PLAN.md)
- [Development Plan](DEVELOPMENT_PLAN.md)
- [Build and Development Guide](BUILD_AND_DEVELOPMENT_GUIDE.md)
- [CDL baseline compliance map](SNARAL_SHARED_CDL_COMPLIANCE.md)
- [Staged expansion roadmap](SNARAL_EXPANSION_ROADMAP.md)
- [Feature opportunities and candidate effects](SNARAL_FEATURE_OPPORTUNITIES.md)

## Feature specifications

### Engine and controls

| Spec | Status |
|---|---|
| [Parameter and state foundation](specs/parameter-state.md) | Core |
| [MIDI voice and pitch](specs/midi-voice.md) | Core |
| [Sub and mid source engine](specs/sub-mid-engine.md) | Core |
| [Oscillator bank and spectral sources](specs/oscillator-bank.md) | Core |
| [Filter and movement](specs/motion-filter.md) | Core |
| [Macro mapping](specs/macro-mapping.md) | Core |
| [Mid processing and output](specs/processing-output.md) | Core |
| [Real-time safety and performance](specs/realtime-performance.md) | Core |
| [Performance foundation](specs/performance-foundation.md) | Core |
| [Snarl Trajectory](specs/expansion-snarl-trajectory.md) | Beta |
| [Sub Anchor](specs/expansion-sub-anchor.md) | Beta |
| [Nonlinear oversampling](specs/expansion-oversampling.md) | Beta |
| [Formant Furnace](specs/expansion-effect-formant-furnace.md) | Beta |
| [Harmonic Fracture](specs/expansion-effect-harmonic-fracture.md) | Beta |
| [Clockwork Gate](specs/expansion-effect-clockwork-gate.md) | Post-beta |
| [Inharmonic Striker](specs/expansion-effect-inharmonic-striker.md) | Post-beta |
| [Spectral Shard](specs/expansion-effect-spectral-shard.md) | Post-beta |
| [MPE and per-note expression](specs/expansion-mpe.md) | Post-beta |
| [Motion Snapshot](specs/expansion-motion-snapshot.md) | Post-beta |
| [Bass Readability Monitor](specs/expansion-readability-monitor.md) | Post-beta |

### User workflow and content

| Spec | Status |
|---|---|
| [Macros and UI behaviour](specs/macros-ui.md) | Core |
| [Main and Advanced UI layout](specs/ui-layout.md) | Core, extended for beta (Effects tab, Trajectory panel, browser, undo/A-B bar, oversampling) |
| [Presets and variations](specs/preset-variation.md) | Core |
| [Preset file contract](specs/preset-file-contract.md) | Core |
| [Factory preset set](specs/factory-presets.md) | Core (10 minimum), Beta (24 or more) |
| [Preset browser](specs/expansion-preset-browser.md) | Beta |
| [Undo, redo and A/B](specs/expansion-undo-ab.md) | Core (basic A/B), Beta (100-step history) |
| [Professional preset library](specs/expansion-preset-library.md) | Post-beta (packs); the larger factory set is Beta |
| [Preset exchange bundles](specs/expansion-preset-exchange.md) | Post-beta |
| [Variation morph](specs/expansion-variation-morph.md) | Post-beta |

### Shared CDL release requirements

| Spec | Status |
|---|---|
| [Themes and Options](specs/cdl-theme-options.md) | Core |
| [Shared actions and bounded randomize](specs/cdl-common-actions.md) | Core |
| [Offline WAV export](specs/cdl-offline-wav-export.md) | Core |
| [Signal visualization and CPU gauge](specs/cdl-signal-visualization.md) | Core |
| [Help and onboarding](specs/cdl-help-and-onboarding.md) | Core |
| [Onboarding sound tour](specs/expansion-onboarding-tour.md) | Core (file name kept) |
| [MIDI CC Learn](specs/expansion-midi-cc-learn.md) | Core (file name kept) |

### Integration, formats and quality

| Spec | Status |
|---|---|
| [Host integration and automation](specs/host-integration.md) | Core |
| [Plug-in format support](specs/expansion-format-support.md) | Beta (VST3, Standalone, CLAP, AU, LV2) |
| [Verification and release gates](specs/verification.md) | Core |
| [Light QA protocol](specs/qa-plan.md) | Beta |
| [Audit checklist](specs/audit-checklist.md) | Beta |
| [Release packaging](specs/release-packaging.md) | Beta |

Performance Gesture Map is excluded by instruction and has no spec.
