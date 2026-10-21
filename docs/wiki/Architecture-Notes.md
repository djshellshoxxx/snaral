# Architecture Notes

Sources: README, `CMakeLists.txt`, `Source/`, `standards/`.

- **Engine separate from the plugin.** `Source/Engine/SineVoice.h` is tested without JUCE (`snaral_engine_tests`). The plugin wrapper (`PluginProcessor`, `PluginEditor`) is built only when `SNARAL_BUILD_PLUGIN=ON`.
- **Parameters as a registry.** `ParameterRegistry.h` holds the host parameters, and `MacroMappings.h` maps the five macros to DSP destinations. `MacroStateValidation.h` validates state. *(inferred from file names.)*
- **Honest status.** The README separates what compiles from what is verified in a DAW and what is released.
- **Standards as gates.** `standards/` defines baseline requirements for audio apps, GUI, accessibility, theme and release.
