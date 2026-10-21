# Snaral Shared CDL Audio-App Compliance

**Source of truth:** [Copied approved CDL Audio Application Baseline](../standards/AUDIO_APP_BASELINE.md)
**Product spec:** [Initial Developer Specification](SNARAL_INITIAL_DEVELOPER_SPEC.md)  
**Status:** Design compliance mapping; every row has a complete spec, and implementation evidence is pending. Aligned with the [Development Plan](DEVELOPMENT_PLAN.md) and the [Beta Implementation Plan](IMPLEMENTATION_PLAN.md).

**Imported shared reference set:** [Knowledge index](../KNOWLEDGE_INDEX.yaml), [skills](../skills/KNOWLEDGE_INDEX.yaml), [troubleshooting records](../troubleshooting/INDEX.yaml), and [standards](../standards/README.md). Imported troubleshooting records are not Snaral-verified until reproduced on the pinned toolchain.

## Applicability decisions

Snaral is a MIDI instrument plug-in plus a standalone audio application. Presets, state restore, themes, help, signal visualization, Options, WAV export, MIDI behavior, and release evidence apply. Plug-in device selection remains host-owned. Standalone device selection and the 440 Hz reference tone apply. The Play/Pause/Stop transport controls of the baseline do not apply (no sequencer); Stop/Panic covers the "Stop" role. Audio input and input routing do not apply (instrument, no audio input).

| Shared baseline requirement | Snaral decision/specification | Milestone |
|---|---|---|
| CDL default theme, additional themes, logo/signature, fonts and control language | [Theme and Options](specs/cdl-theme-options.md), [UI layout](specs/ui-layout.md); signature notch until an approved logo exists | 5 |
| Common Stop/Panic, Reset, Randomize, Bypass, preset selector, context menu | [Shared Actions and Bounded Randomize](specs/cdl-common-actions.md); Panic in [Performance Foundation](specs/performance-foundation.md) | 5 |
| At least ten distinct presets, user save/import/export, safe state restore | [Presets and variations](specs/preset-variation.md), [Preset file contract](specs/preset-file-contract.md), [Factory Presets](specs/factory-presets.md) (24 or more in the beta) | 4 and 6 |
| A/B compare | [Undo, Redo and A/B](specs/expansion-undo-ab.md): basic A/B is core, the 100-step history is in the beta | 5 |
| Options; device/MIDI setup; themes, tooltips, animation; reference tone | [Theme and Options](specs/cdl-theme-options.md), [Host integration](specs/host-integration.md) | 5 |
| MIDI mapping or learn | [MIDI CC Learn](specs/expansion-midi-cc-learn.md) | 5 |
| Audio visualization and CPU-use gauge | [Signal Visualization and CPU Gauge](specs/cdl-signal-visualization.md) | 5 |
| Audio export (instrument render) | [Offline WAV Export](specs/cdl-offline-wav-export.md) | 6 |
| Help, first-run introduction, rerun onboarding | [Help and Onboarding](specs/cdl-help-and-onboarding.md), [Onboarding Tour](specs/expansion-onboarding-tour.md) | 5 |
| Accessibility, resizing, scrolling, non-color-only state | [Macros and UI](specs/macros-ui.md), [UI layout](specs/ui-layout.md), copied baseline | 5 |
| Audio/MIDI lifecycle, real-time safety, tests, release evidence | [Host integration](specs/host-integration.md), [Real-time](specs/realtime-performance.md), [Verification](specs/verification.md), [QA](specs/qa-plan.md), [Audit](specs/audit-checklist.md), [Packaging](specs/release-packaging.md) | all |

## Core and beta plan alignment

Core-release gates (milestones 5 and 6 of the parent plan):
- CDL theme set, Options surface, tooltips and animation preferences.
- Stop/Panic (clears only Snaral voices and pedal state), Reset, bounded deterministic Randomize, Bypass, parameter context actions, preset selection, Export WAV entry point.
- Basic A/B compare.
- MIDI CC Learn for the macros and relevant performance controls.
- First-run onboarding and help.
- Standalone device setup and reference tone.
- Offline WAV rendering, live signal scope, and plug-in DSP CPU gauge.

Promoted into the v0.2 beta by the implementation plan (not baseline requirements, but now release scope): 100-step undo/redo, preset browser with search and favourites, oversampling, Formant Furnace, Harmonic Fracture, Snarl Trajectory, Sub Anchor, a 24-preset factory library, and AU, CLAP and LV2 formats.

Still post-beta: variation morph, preset library packs, preset exchange, Motion Snapshot, Readability Monitor, MPE, Clockwork Gate, Inharmonic Striker, Spectral Shard.

## Evidence checklist

Before release, update this table with implementation paths and evidence (the audit and QA reports hold the detailed proof):
- [ ] Compliance checklist completed per baseline section 10.
- [ ] Theme screenshot review and high-DPI checks pass.
- [ ] Main action/context menu behavior reviewed.
- [ ] Preset and project-state round-trips pass.
- [ ] WAV export files inspected and played in a second application.
- [ ] Scope and CPU gauge accuracy measured.
- [ ] Standalone device failure/recovery and reference tone tested.
- [ ] Help/onboarding checked against the current parameter registry.
- [ ] MIDI CC Learn verified in the standalone and in each tested host.
- [ ] Deferred/non-applicable items have a stated reason, owner and follow-up milestone.
- [ ] BETA READY declared only after the verification release gates have recorded evidence.
