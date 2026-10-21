# Developer Build and Test

## Toolchain

- CMake 3.22 or newer. The first configure fetches JUCE 9.0.3 with FetchContent, so the network is needed once
- C++20 compiler
- Linux audio and GUI dependencies. CI installs `libasound2-dev`, `libjack-jackd2-dev` and the JUCE GUI packages with apt
- Plug-in formats: VST3 and Standalone. The engine test target does not need a host

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel 2
```

Plug-in outputs go to `build/Snaral_artefacts/`, including `Standalone/` and VST3. For the engine-only workflow see `docs/BUILD_AND_DEVELOPMENT_GUIDE.md`.

## Test

```sh
ctest --test-dir build -C Release --output-on-failure
```

Two test executables are registered with CTest:

- `snaral_engine_tests` (`tests/SineVoiceTests.cpp`): the sine voice
- `snaral_parameter_tests` (`tests/ParameterContractTests.cpp`): the parameter registry and contracts

## CI (`.github/workflows/build.yml`)

- Linux: install dependencies, configure, build, run `ctest`
- Uploads `build/Snaral_artefacts/Standalone/**` as an artefact

## Release

`standards/RELEASE_REQUIREMENTS.md` lists what a release needs. The current build is a prototype and is not a release candidate.

## Troubleshooting

- JUCE fetch fails: check the network and the pinned tag in `CMakeLists.txt`.
- Missing ALSA or JACK headers on Linux: install the packages listed in the workflow.
- Troubleshooting entries live in `troubleshooting/` (index and template).
