# Developer Conventions

## Style

- C++20. JUCE idioms in the plug-in code. The engine (`Source/Engine/`) stays free of JUCE plug-in types so it can be tested alone.
- Parameter IDs and ranges live in one table (`ParameterRegistry.h`). Do not copy them into other files.
- Keep mapping functions pure (`MacroMappings.h`).

## Real-time rules

- No allocation, no locks and no string parameter lookup on the audio thread. The processor caches raw parameter pointers at construction and copies them in `refreshMacroSnapshot()`.
- Do not change the restore sequence logic in `refreshMacroSnapshot()` without checking the state restore path. It skips the copy during a restore so the audio thread keeps coherent values.
- Voice count is fixed at eight in the constructor. Changing it changes polyphony.

## State and presets

- Every state load goes through `validateStateTree()` and the candidate checks in `MacroStateValidation.h`. Do not bypass them.
- Parameter IDs are saved state. Renaming one breaks saved projects.

## Specs and scope

- The specs in `docs/specs/` describe future behaviour. Do not claim a feature works until the code and tests show it.
- The README says only Weight changes the sound. Keep that statement true until the other macros are wired.

## How to add a feature

1. Check the feature spec in `docs/specs/` and the stage in `docs/DEVELOPMENT_PLAN.md`.
2. Add any new parameter to `ParameterRegistry.h`, then a contract test.
3. Put DSP in `Source/Engine/` with an engine test.
4. Wire it in `PluginProcessor.cpp`.
5. Run `ctest` and update the docs.

## Known limitations

From the README and the development plan:

- Prototype. Only Weight changes the sound.
- Growl, Bite, Motion and Space are defined but not yet wired to DSP.
- Presets, the GUI layout from `docs/specs/ui-layout.md` and most of the feature set are still planned.
