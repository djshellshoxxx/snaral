---
agent: 'agent'
description: 'Implement Snaral’s five JUCE host macro parameters and verify their contract'
---

Implement the next Stage 1 slice in this repository: expose Snaral’s five macro parameters to the JUCE host with stable IDs, validated defaults, normalized values, and state round-tripping.

## Read first

- `docs/DEVELOPMENT_PLAN.md`
- `docs/specs/parameter-state.md`
- `docs/specs/macro-mapping.md`
- `docs/SNARAL_INITIAL_DEVELOPER_SPEC.md`, especially the parameter table and voice behavior
- `Source/Parameters/ParameterRegistry.h` and `Source/Parameters/MacroMappings.h`
- `Source/PluginProcessor.h` and `Source/PluginProcessor.cpp`
- `skills/juce-guide/SKILL.md`, `skills/juce-review/SKILL.md`, and `skills/audio-dsp-review/SKILL.md`
- Relevant records in `troubleshooting/INDEX.yaml`; verify advice against the pinned JUCE 9.0.3 version

## Scope

1. Create the five host parameters from the existing registry: `weight`, `growl`, `bite`, `motion`, and `space`. Preserve their display names, ranges, and defaults exactly as specified. IDs are compatibility keys; do not rename or reuse them.
2. Use JUCE’s supported parameter APIs and normalized host values in [0, 1]. Keep the JUCE wrapper separate from the JUCE-independent registry and macro mapping functions.
3. Make host automation changes observable by the processor without allocation, locks, UI access, or other unbounded work on the audio callback. Do not add parameter listeners that write back into the same parameters during display refresh.
4. Implement host project state save/restore for the parameter tree. Validate the complete candidate state before applying it; malformed or unsupported state must leave the active values unchanged. Do not perform file or network I/O from audio processing.
5. Add focused tests for parameter IDs, count, defaults, normalized ranges, automation value access, and state round-trip/rejection where the existing test architecture permits. Keep the pure engine tests buildable without fetching JUCE.
6. Do not fabricate macro sound behavior. Use `MacroMappings.h` for any destination mapping that is actually connected; where a destination does not yet exist in the engine, leave it unwired and record the remaining work in the development plan.

## Constraints

- Keep the real-time callback bounded and allocation-free. Read parameter atomics or use another reviewed real-time-safe mechanism.
- Preserve the current MIDI sine foundation and its release behavior.
- Do not add Advanced parameters, preset files, UI controls, randomization, or unrelated dependencies in this task.
- Follow the pinned JUCE 9.0.3 API and the build troubleshooting record about generating `JuceHeader.h` after linking JUCE modules.
- Update specs or the development plan only where implementation evidence requires a precise status change. Never mark a milestone complete based only on compilation.

## Verification

Run the engine-only build and test path from `docs/BUILD_AND_DEVELOPMENT_GUIDE.md`, plus the focused parameter tests. If the full JUCE plugin build is available, build VST3 and Standalone and report the exact commands and outcomes. If blocked by unavailable tooling or network access, state that clearly and do not imply the plugin build passed.

Finish with a concise summary of files changed, verified behavior, test results, and remaining Stage 1 work. Do not merge or publish changes.
