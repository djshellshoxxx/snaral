# Developer Code Map

Paths are relative to the repo root.

## Directory tree

```
snaral/
├── CMakeLists.txt                  C++20, JUCE 9.0.3 via FetchContent, juce_add_plugin(Snaral),
│                                   engine and parameter test executables with ctest entries
├── Source/
│   ├── PluginProcessor.h/.cpp      SnaralAudioProcessor: APVTS, synth with 8 voices, macro snapshot,
│   │                               processBlock, state load and save
│   ├── PluginEditor.h/.cpp         editor
│   ├── Engine/
│   │   └── SineVoice.h             SineVoice: frequency, note on and off, pitch wheel, renderSample
│   └── Parameters/
│       ├── ParameterRegistry.h     macroParameterSpecs (id, label, range, default), findMacroParameter()
│       ├── MacroMappings.h         pure mapping functions from macro value to DSP multiplier or offset
│       └── MacroStateValidation.h  MacroStateCandidate and state validation before a restore
├── tests/
│   ├── SineVoiceTests.cpp          engine tests (snaral_engine_tests)
│   └── ParameterContractTests.cpp  registry and contract tests (snaral_parameter_tests)
├── docs/                           development plan, build guide, initial spec, feature spec index,
│   │                               specs/ (one spec per feature), SNARAL_* analyses
│   └── wiki/                       these wiki pages
├── standards/                      shared CDL standards (theme, GUI, accessibility, help, release)
├── troubleshooting/                troubleshooting index and template
├── skills/, shared/                shared tooling and skills (not plug-in code)
├── .github/workflows/build.yml     Linux build, ctest, artefact upload
├── .github/prompts/                Copilot prompt for macro host parameters
└── KNOWLEDGE_INDEX.yaml            index of imported shared records
```

## Key symbols

| Symbol (file:approx line) | Purpose |
|---|---|
| `SnaralAudioProcessor::SnaralAudioProcessor()` (`Source/PluginProcessor.cpp:16`) | stereo output bus, APVTS creation, macro pointer caching, adds 8 voices (lines 28 to 29) |
| `createParameterLayout()` (`PluginProcessor.cpp:33`) | builds the APVTS layout from the registry |
| `prepareToPlay()` (`:48`), `releaseResources()` (`:59`) | sample rate setup |
| `isBusesLayoutSupported()` (`:61`) | bus check |
| `processBlock()` (`:67`) | renders the synth into the output |
| `refreshMacroSnapshot()` (`:98`) | copies macro raw values with a restore sequence guard |
| `Voice::startNote()` (`:124`), `stopNote()` (`:130`), `renderNextBlock()` (`:140`) | JUCE voice hooks |
| `getStateInformation()` (`:160`), `parseStateValue()` (`:172`), `validateStateTree()` (`:187`), `setStateInformation()` (`:209`) | state with validation |
| `createEditor()` (`:155`) | editor factory |
| `class SineVoice` (`Source/Engine/SineVoice.h:9`) | `prepare()` (`:12`), `noteOn()` (`:23`), `noteOff()` (`:37`), `setPitchWheel()` (`:54`), `renderSample()` (`:65`), `isActive()` (`:88`), `updateFrequency()` (`:91`) |
| `macroParameterSpecs` (`ParameterRegistry.h`) | the five macros: weight 75, growl 65, bite 20, motion 0, space 0 (0 to 100 each) |
| `findMacroParameter()` (`ParameterRegistry.h:47`) | lookup by ID |
| mapping functions (`MacroMappings.h:8` to `:53`) | `normalizedMacroValue`, `weightSubMultiplier`, `growlMidMultiplier`, `growlSourceBBias`, `growlSourceBlend`, `biteFilterPeakOffsetSemitones`, `biteDriveOffsetDb`, `motionDepthMultiplier`, `spaceWidthPercent` |
| `MacroStateCandidate` (`MacroStateValidation.h:12`) | state candidate for validation |

## Where do I find X / to change X, edit Y

| I want to... | Edit |
|---|---|
| add or change a macro | `Source/Parameters/ParameterRegistry.h` (`macroParameterSpecs`), then `findMacroParameter` users, then `tests/ParameterContractTests.cpp` |
| change how a macro scales the sound | `Source/Parameters/MacroMappings.h`, then the mapping call in `PluginProcessor.cpp` |
| connect Growl, Bite, Motion or Space to DSP | the render path in `processBlock()` and `Voice::renderNextBlock()`. Mapping functions already exist. Add engine tests in `tests/SineVoiceTests.cpp` |
| change the sine voice | `Source/Engine/SineVoice.h` |
| change note handling or voice count | `PluginProcessor.cpp` constructor (voice loop) and the `Voice` hooks |
| change state save, load or validation | `PluginProcessor.cpp` (`getStateInformation`, `setStateInformation`) and `MacroStateValidation.h` |
| change the plug-in identity (codes, product name) | `CMakeLists.txt` `juce_add_plugin` block (`PLUGIN_MANUFACTURER_CODE Cdlb`, `PLUGIN_CODE Snrl`, `PRODUCT_NAME "Snaral"`) |
| change the editor | `Source/PluginEditor.cpp` and `.h` |
| add a test | `tests/` plus the `add_executable` and `add_test` lines in `CMakeLists.txt` |

## Gotchas for navigation

- `SineVoice` is in `Source/Engine/` and has no JUCE plug-in dependency. Keep it that way so the engine test can build alone.
- The test executables are built from explicit source lists in `CMakeLists.txt`. A new test file needs an entry there.
