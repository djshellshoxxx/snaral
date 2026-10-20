# Snaral Shared CDL Audio-App Compliance

**Source of truth:** [Copied approved CDL Audio Application Baseline](standards/AUDIO_APP_BASELINE.md)  
**Product spec:** [Initial Developer Specification](SNARAL_INITIAL_DEVELOPER_SPEC.md)  
**Status:** Design compliance mapping; implementation evidence is pending.

## Applicability decisions

Snaral is a MIDI instrument plug-in plus a standalone audio application. Therefore presets, state restore, themes, help, signal visualization, Options, WAV export, MIDI behavior, and release evidence apply. Plug-in device selection remains host-owned. Standalone device selection and 440 Hz reference tone apply.

| Shared baseline requirement | Snaral decision/specification | Planned milestone |
|---|---|---|
| CDL default theme, additional themes, logo/signature, fonts and control language | [CDL Theme and Options](specs/cdl-theme-options.md); use signature notch until approved logo asset exists | UI baseline |
| Common Stop/Panic, Reset, Randomize, Bypass, preset selector and context menu | [Shared Actions and Bounded Randomize](specs/cdl-common-actions.md); Panic and layer controls in Performance Foundation | UI baseline |
| Minimum ten distinct presets, user save/import/export, safe state restore | Parent preset specs and [Factory Presets](specs/factory-presets.md) | State/preset and content milestones |
| A/B compare where architecture supports it | [Undo/Redo and A/B Compare](specs/expansion-undo-ab.md); promote A/B subset into initial release | UI baseline |
| Options; device/MIDI setup; themes, tooltips, animation; reference tone | [CDL Theme and Options](specs/cdl-theme-options.md) | UI baseline |
| Audio visualization and CPU-use gauge | [Signal Visualization and CPU Gauge](specs/cdl-signal-visualization.md) | Release feature milestone |
| Offline WAV export for an instrument | [Offline WAV Export](specs/cdl-offline-wav-export.md) | Release feature milestone |
| Help, first-run introduction, rerun onboarding | [Shared Help and Onboarding](specs/cdl-help-and-onboarding.md) and [Onboarding Tour](specs/expansion-onboarding-tour.md) | UI baseline |
| Accessibility, resizing, scrolling, non-color-only state | Parent UI specs and copied CDL baseline | UI baseline |
| Audio/MIDI lifecycle, real-time safety, tests and release evidence | Parent host/verification/performance specs | All milestones |

## Required changes to the core plan

Promote these items from expansion candidates into the initial CDL-compliant release:
- CDL theme set, Options surface, tooltips and animation preferences.
- Reset, bounded deterministic Randomize, Bypass, parameter context actions, and basic preset dropdown/directory scan.
- A/B compare; full 100-step Undo/Redo can remain a later usability enhancement.
- MIDI CC Learn for the five macros and relevant performance controls.
- First-run onboarding/help.
- Standalone device setup and reference tone.
- Offline WAV rendering, live signal scope, and plug-in DSP CPU gauge.

Keep full-text preset search/favorites, variation morph, professional preset packs, preset exchange, all differentiator features/effects, MPE, oversampling, AU, and CLAP in staged expansion unless a later scope decision promotes them.

## Evidence checklist

Before release, update this table with implementation paths and evidence:
- [ ] Compliance checklist completed per baseline section 10.
- [ ] Theme screenshot review and high-DPI checks pass.
- [ ] Main action/context menu behavior reviewed.
- [ ] Preset and project-state round-trips pass.
- [ ] WAV export files inspected and played in a second application.
- [ ] Scope and CPU gauge accuracy measured.
- [ ] Standalone device failure/recovery and reference tone tested.
- [ ] Help/onboarding checked against current parameter registry.
- [ ] Deferred/non-applicable items have a stated reason.
