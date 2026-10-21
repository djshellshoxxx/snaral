# Developer Guide

## Engine-only (no JUCE)

```sh
cmake -S . -B build-core -DSNARAL_BUILD_PLUGIN=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build-core -j 2
ctest --test-dir build-core --output-on-failure   # 2 of 2 pass on this snapshot
```

Tests: `tests/SineVoiceTests.cpp`, `tests/ParameterContractTests.cpp`.

## Full build (plugin)

`SNARAL_BUILD_PLUGIN` defaults to ON and fetches JUCE 9.0.3 through `FetchContent`. See `docs/BUILD_AND_DEVELOPMENT_GUIDE.md` for prerequisites. The plugin targets were not built in this pass.

## Conventions

- Parameter IDs and the macro mapping are contracts: `Source/Parameters/ParameterRegistry.h`, `MacroMappings.h`, `MacroStateValidation.h`.
- Read `standards/` (audio app baseline, GUI, accessibility, release requirements) before UI or release work.
- `KNOWLEDGE_INDEX.yaml` and `troubleshooting/` hold shared guidance.
