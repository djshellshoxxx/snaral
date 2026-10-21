# Developer Overview

## What it is

A JUCE 9.0.3 plug-in (C++20) with product name **Snaral**. Targets: VST3 and Standalone. A separate engine test target runs the sine voice without a host. CMake fetches JUCE via FetchContent.

Prototype state (from README): eight-voice MIDI sine instrument, five macro parameters, most macros not yet wired.

## Design decisions (and why)

**Engine separate from the plug-in.** `Source/Engine/SineVoice.h` does not depend on the processor. That lets `snaral_engine_tests` test DSP without a host.

**Parameter registry as the contract.** `Source/Parameters/ParameterRegistry.h` holds `macroParameterSpecs` (ID, label, range, default). The processor builds its APVTS layout from this table and looks up IDs from it. `snaral_parameter_tests` checks the contract.

**Macro mappings as pure functions.** `Source/Parameters/MacroMappings.h` turns a macro value into a multiplier or offset (for example `weightSubMultiplier`, `growlMidMultiplier`, `biteDriveOffsetDb`, `spaceWidthPercent`). Pure functions are easy to test and keep the audio code simple.

**Validated state.** `MacroStateValidation.h` checks a candidate state before it is accepted. `setStateInformation()` uses it, so a bad preset cannot push out-of-range values into the processor.

**Raw parameter pointers on the audio side.** The processor caches `getRawParameterValue` pointers for each macro in `rawMacroValues_` at construction. `refreshMacroSnapshot()` copies them into `activeMacroValues_`. It skips the copy while a state restore is in progress (sequence counter), so the audio thread keeps the last coherent values. It does not look up IDs.

**Voices owned by a JUCE Synthesiser.** Eight `SnaralAudioProcessor::Voice` objects are added to `synth_` in the constructor. Each wraps a `SineVoice` and implements `startNote`, `stopNote` and `renderNextBlock`.

**Specs ahead of code.** `docs/specs/` describes features that are not yet implemented. The README and `docs/DEVELOPMENT_PLAN.md` say what is active. Treat the specs as intent, not as shipped behaviour.

## Signal flow (current build)

```
MIDI in ──► JUCE Synthesiser (8 voices)
                 │  Voice::startNote / stopNote
                 ▼
            SineVoice::renderSample()  (frequency from note + pitch wheel)
                 │
                 ▼  level from Weight (mapping in MacroMappings.h)
            stereo output ◄── processBlock()
                 ▲
   macro values ─┘  refreshMacroSnapshot() copies raw parameter values
```

The other macros (Growl, Bite, Motion, Space) have mapping functions in `MacroMappings.h` but are not yet connected in `processBlock()`.

## Threads

- Audio thread: `processBlock()`, `Voice::renderNextBlock()`, `refreshMacroSnapshot()`.
- Message thread: editor, state load and save (`getStateInformation`, `setStateInformation`), parameter changes from the host UI.

## Data

- `ParameterSpec` (`ParameterRegistry.h:9`): one macro's ID, label, range, default
- `MacroStateCandidate` (`MacroStateValidation.h:12`): a state being validated
- `SineVoice` (`SineVoice.h:9`): one voice's note, frequency, envelope and active flag

See [[Developer-Code-Map]] for locations and [[Developer-Conventions]] for the rules.
