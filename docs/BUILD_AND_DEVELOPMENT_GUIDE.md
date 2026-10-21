# Snaral Build and Development Guide

**Status:** Implementation guide; update commands when toolchain policy changes.  
**Primary plan:** [Development Plan](DEVELOPMENT_PLAN.md) and [Beta Implementation Plan](IMPLEMENTATION_PLAN.md)  
**Product contract:** [Initial Developer Specification](SNARAL_INITIAL_DEVELOPER_SPEC.md)  
**Evidence rules:** [Verification](specs/verification.md)

## Requirements

- CMake 3.22 or newer; Ninja recommended.
- C and C++20 compilers supported by JUCE 9.0.3 (JUCE's CMake configuration probes both languages): MSVC 2022 on Windows, Xcode/AppleClang on macOS (deployment target 11.0), GCC or Clang on Linux.
- Git and network access during the first configure so CMake can fetch JUCE 9.0.3 and clap-juce-extensions, unless local checkouts are supplied (see options).
- Linux audio/GUI development packages: `libasound2-dev libjack-jackd2-dev libfreetype6-dev libfontconfig1-dev libgl1-mesa-dev libx11-dev libxext-dev libxi-dev libxinerama-dev libxrandr-dev libxcursor-dev` (the CI workflow installs these on Ubuntu 24.04).
- Confirm JUCE licensing (AGPLv3 or commercial) and third-party notices before distributing builds. Fetching the source does not grant redistribution rights.

JUCE is pinned in `CMakeLists.txt`. clap-juce-extensions is currently fetched from branch `main`, which is not reproducible; pin it to a commit before a release tag.

## CMake options

| Option | Default | Meaning |
|---|---|---|
| `SNARAL_BUILD_PLUGIN` | ON | Build the plug-in formats, standalone and JUCE-dependent tests. OFF builds only the JUCE-free tests. |
| `SNARAL_BUILD_CLAP` | ON | Also build the CLAP plug-in (needs clap-juce-extensions). |
| `SNARAL_JUCE_DIR` | empty | Path to a local JUCE checkout instead of fetching. |
| `SNARAL_CLAP_EXT_DIR` | empty | Path to a local clap-juce-extensions checkout instead of fetching. |
| `CMAKE_OSX_ARCHITECTURES` | host | `arm64` or `x86_64` for macOS builds (separate artifacts in the beta). |

## Commands

JUCE-free unit tests (fast, no JUCE download). Every `tests/*Tests.cpp` is registered automatically as `snaral_<name>`:

```sh
tools/build_tests.sh              # all tests
tools/build_tests.sh macro        # only tests whose name matches the ctest regex
```

Equivalent by hand:

```sh
cmake -S . -B build-tests -G Ninja -DSNARAL_BUILD_PLUGIN=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build-tests -j2
ctest --test-dir build-tests --output-on-failure
```

Plug-in, standalone and JUCE-dependent tests (`tests/juce/*Tests.cpp`, registered as `snaral_juce_<name>`):

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel 2
ctest --test-dir build -C Release --output-on-failure
```

With local dependency checkouts, or without CLAP:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DSNARAL_JUCE_DIR=/path/to/JUCE -DSNARAL_CLAP_EXT_DIR=/path/to/clap-juce-extensions
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DSNARAL_BUILD_CLAP=OFF
```

One format only: `cmake --build build --config Release --target Snaral_VST3` (also `Snaral_Standalone`, `Snaral_CLAP`, `Snaral_AU` on macOS, `Snaral_LV2` on Linux). Outputs appear under `build/Snaral_artefacts/<config>/<Format>/` (the config folder depends on the generator).

`tools/build_juce.sh [target...]` is a helper for the shared development container: it builds in `build-juce` with Ninja, uses the container's local JUCE and clap-juce-extensions checkouts (`/home/claude/deps/...`) and a global build lock. On any other machine use the plain CMake commands above. Sanitizer and validator commands are in [verification](specs/verification.md) and [QA plan](specs/qa-plan.md).

## CI artifacts and GitHub Releases

`.github/workflows/build.yml` builds and tests on ubuntu-24.04, macos-15 (arm64) and windows-2022, uploading plug-in outputs as temporary artifacts. A tag starting with `v` runs the release job after every platform passes; a tag containing a hyphen (for example `v0.2.0-beta.1`) is marked as a pre-release. Beta packaging requirements (the artifact matrix, naming, ad-hoc signing, per-format validation) are defined in [release packaging](specs/release-packaging.md); the workflow still has to be extended to match. Release publication needs `GITHUB_TOKEN` with contents write permission. Do not tag until the intended source is on the branch and CI results are reviewed.

## Source boundaries

The authoritative module map and ownership are in the [implementation plan](IMPLEMENTATION_PLAN.md). In short:

- `Source/Parameters`: the single parameter registry (`ParameterTable.h`, `ParameterList.def`, `Groups/*.def`), macro mappings, JUCE layout generator. Append-only; never reorder or reuse IDs.
- `Source/Dsp`: JUCE-free header-only DSP blocks. `Source/Engine`: JUCE-free voices and engine. `Source/Core`: JUCE-free services (MIDI Learn, Randomize, render driver).
- `Source/State`: preset JSON, project state, variations, A/B, undo. `Source/UI` and the plug-in editor: views; no DSP ownership.
- `tests`: JUCE-free tests; `tests/juce`: JUCE-dependent tests.

Only `Engine` and `Dsp` code runs on the audio thread. Keep its callback path allocation-free, lock-free, and free of file/network I/O. UI, preset parsing, and file export run off the audio thread.

## Change workflow

1. Link the change to a feature spec and acceptance criterion.
2. Add a focused failing test before implementing behavior.
3. Implement one vertical slice and run its focused test (`tools/build_tests.sh <regex>`).
4. Run the full ctest suite and let CI cover all targets before merge.
5. Update the spec, the [audit checklist](specs/audit-checklist.md) rows, and the [CDL compliance map](SNARAL_SHARED_CDL_COMPLIANCE.md) when behavior or scope changes.
6. Keep generated build outputs (`build*/`) out of version control.

Before work on a shared CDL domain, consult the root [knowledge index](../KNOWLEDGE_INDEX.yaml), then the relevant skill and troubleshooting references (for example `skills/plugin-packaging`, `skills/plugin-testing`, `skills/clap-review`, `skills/au-review`, `skills/vst3-review`). Imported troubleshooting entries are cross-project references; verify behavior against Snaral's pinned JUCE version and record Snaral-specific findings in `troubleshooting/`.

## Current prototype scope

The plug-in is an eight-voice sine instrument with pitch wheel support and validated host state for five macro parameters, with an editor exposing Weight, Growl, Bite, Motion and Space. Only Weight changes the sound. The full parameter registry and its tests exist but are not yet connected to the processor. Presets, the production sub/mid engine, the remaining macro DSP, effects, formats beyond what CMake already declares, and the CDL workflow features are release gates in the development plan. CI artifacts must describe this scope accurately.
