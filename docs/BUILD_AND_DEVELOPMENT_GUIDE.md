# Snaral Build and Development Guide

**Status:** Initial implementation guide; update commands when toolchain policy changes.  
**Primary plan:** [Development Plan](DEVELOPMENT_PLAN.md)  
**Product contract:** [Initial Developer Specification](SNARAL_INITIAL_DEVELOPER_SPEC.md)

## Requirements

- CMake 3.22 or newer.
- C and C++20 compilers supported by JUCE 9.0.3 (JUCE’s CMake configuration probes both languages).
- Git and network access during first configure so CMake can fetch the pinned JUCE source.
- Platform audio/GUI development libraries required by JUCE. The CI workflow installs Linux packages.
- Confirm JUCE licensing and third-party notices before distributing builds; the pinned source dependency does not by itself grant redistribution rights.

The JUCE dependency is pinned in `CMakeLists.txt`. Update it deliberately and run all supported CI targets before merging.

## Configure, build, test

From the repository root:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel 2
ctest --test-dir build -C Release --output-on-failure
```

The build produces a VST3 and a standalone target. For DSP-only development without downloading JUCE:

```sh
cmake -S . -B build-engine -DSNARAL_BUILD_PLUGIN=OFF -DCMAKE_BUILD_TYPE=Debug
cmake --build build-engine --parallel 2
ctest --test-dir build-engine --output-on-failure
```

## CI artifacts and GitHub Releases

Push a version tag such as `v0.1.0-alpha.1` to run the Linux, macOS, and Windows build-and-test matrix. The release job runs only after every platform build and test passes. It packages each platform's VST3 and standalone outputs into separate ZIP files and attaches them to the GitHub Release for that tag. A tag containing a hyphen, such as `v0.1.0-alpha.1`, is marked as a prerelease. Ordinary pushes and pull requests continue to publish temporary Actions artifacts only.

Release publication requires the repository's standard `GITHUB_TOKEN` to have contents write permission. Do not publish a tag until the branch contains the intended source and the CI results have been reviewed.

## Source boundaries

- `Source/Engine`: audio-rate DSP with no UI, file-format, or host dependencies.
- `Source/Parameters`: stable parameter IDs and host-visible parameter model.
- `Source/State`: validated serialization and migration.
- `Source/Host`: JUCE processor, MIDI/transport adaptation, and format integration.
- `Source/UI`: views and user interaction; no direct DSP ownership.
- `tests`: engine contract and integration tests.

Only `Engine` code runs on the audio thread. Keep its callback path allocation-free, lock-free, and free from file/network I/O. UI, preset parsing, and file export run off the audio thread.

## Change workflow

1. Link the change to a feature spec and acceptance criterion.
2. Add a focused failing test for behavior before implementing it.
3. Implement one vertical slice and run its focused test.
4. Run the full CTest suite and all CI targets before merge.
5. Update the spec, traceability matrix, and [CDL compliance map](SNARAL_SHARED_CDL_COMPLIANCE.md) when behavior or scope changes.
6. Keep generated build outputs out of version control.

Before work on a shared CDL domain, consult the root [knowledge index](../KNOWLEDGE_INDEX.yaml), then the relevant skill and troubleshooting references. Imported troubleshooting entries are cross-project references; verify behavior against Snaral's pinned JUCE version and record Snaral-specific findings in `troubleshooting/`.

## Current prototype scope

The current implementation is an eight-voice sine instrument with pitch wheel support, validated host state for five macro parameters, and a main editor exposing Weight, Growl, Bite, Motion, and Space. Only Weight currently changes the sine voice level. The other four controls are exposed for host automation but do not yet have their specified audible destinations. Presets, the production sub/mid engine, the remaining macro DSP, and most CDL workflow requirements remain release gates in the development plan. CI artifacts and alpha releases must describe this scope accurately.
