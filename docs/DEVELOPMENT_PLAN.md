# Snaral Development Plan

**Status:** Active implementation plan  
**Roadmap:** [Initial Developer Specification](SNARAL_INITIAL_DEVELOPER_SPEC.md)  
**Build guide:** [Build and Development Guide](BUILD_AND_DEVELOPMENT_GUIDE.md)  
**Spec index:** [Feature Specification Index](SNARAL_FEATURE_SPEC_INDEX.md)  
**Shared requirements:** [CDL Compliance Map](SNARAL_SHARED_CDL_COMPLIANCE.md)

## Delivery stages

| Stage | Outcome | Exit evidence |
|---|---|---|
| 0. Build foundation | Reproducible JUCE VST3/standalone build, CI, modular source layout, minimal MIDI sine | CI builds all target OSes; test target runs; MIDI note produces finite stereo audio |
| 1. Engine and parameter contract | Stable parameter registry, voice lifecycle, sub/mid split, envelopes, macros, filters, modulation, centered sub | Unit, render, aliasing, note-storm, state-independent DSP tests pass |
| 2. Host and state | Automation, transport, pitch/tuning, project restore, versioned preset JSON, four variations | Host round-trip, malformed input, migration, automation stress pass |
| 3. CDL workflow | Themes/options, Stop/Panic, Reset/Randomize/Bypass, context actions, preset selection, MIDI CC Learn, A/B, help/onboarding, scope and CPU indicator | Baseline mapping complete; usability, accessibility, and performance evidence recorded |
| 4. Content and standalone utilities | Ten factory presets, import/export, standalone devices, reference tone, offline WAV render | Preset review and WAV interoperability checks pass; standalone recovery verified |
| 5. Release validation | Functional, host, format, audio, performance, UI, security/license, and packaging checks | All release gates passed or each exception is explicitly approved and documented |

Stages 1–4 may overlap only where module contracts are already stable. Preset content should not be authored against unstable parameter IDs. The CDL workflow scope is part of initial release completion and must not be moved into post-release expansion without a recorded product decision.

## First implementation tasks

1. Bootstrap CMake/JUCE and CI; maintain the no-allocation callback rule.
2. Prove MIDI note-on/off, polyphonic allocation, and clean release on VST3 and standalone.
3. Define parameter IDs and state shape before exposing host automation.
4. Implement the parallel sub/mid engine and test sub isolation before adding advanced effects.
5. Complete baseline workflow, content, and export requirements in the order above.

### Active Stage 1 slice

1. Register the five public macro IDs, display names, normalized defaults, and normalized/domain conversion rules in a JUCE-independent header.
2. Implement and test the normative macro mapping functions at boundary and interior values.
3. Expose the macro contract through JUCE host parameters and connect those values to DSP without UI-side mapping.
4. Expand the registry with Advanced IDs and validated defaults before adding those controls to host automation or presets.

The current implementation begins with tasks 1–2. Host parameter integration is the next vertical slice; this does not count Stage 1 as complete.

## Expansion gate

Only after Stage 5 passes, select additional features from the [Expansion Roadmap](SNARAL_EXPANSION_ROADMAP.md). Performance Gesture Map remains excluded by instruction. Expansion estimates do not replace initial-release baseline work.
