# Snaral

Snaral is a MIDI-playable drum and bass bass synthesizer being built around a stable, centered sub layer and a separately shaped mid voice.

**Current state: pre-beta prototype.** The playable plug-in is still an eight-voice MIDI sine instrument with pitch wheel support. Five macro parameters (Weight, Growl, Bite, Motion, Space) are host-automatable, and only Weight changes the sound. The full 99-parameter registry, its tests and a JUCE layout generator exist in the repository but are not connected to the plug-in yet. The sub/mid engine, effects, presets, CDL workflow features, WAV export and the other formats are specified, not implemented. Nothing here is a finished or production-ready instrument, and compiling successfully does not establish DAW compatibility. No released build exists yet.

**Planned v0.2 beta:** VST3 and Standalone on Windows x64, macOS (arm64 and Intel) and Linux x64; CLAP on all three; AU on macOS; LV2 on Linux (experimental). Builds will be unsigned or ad-hoc signed pre-releases. The scope is defined in the [Beta Implementation Plan](docs/IMPLEMENTATION_PLAN.md); a format is only listed as supported after its validator and a host test pass.

## Start here

- [Beta implementation plan](docs/IMPLEMENTATION_PLAN.md)
- [Development plan](docs/DEVELOPMENT_PLAN.md)
- [Build and development guide](docs/BUILD_AND_DEVELOPMENT_GUIDE.md)
- [Initial developer specification](docs/SNARAL_INITIAL_DEVELOPER_SPEC.md)
- [Feature specification index](docs/SNARAL_FEATURE_SPEC_INDEX.md) and [expansion roadmap](docs/SNARAL_EXPANSION_ROADMAP.md)
- [UI layout specification](docs/specs/ui-layout.md)
- [Verification](docs/specs/verification.md), [QA plan](docs/specs/qa-plan.md), [audit checklist](docs/specs/audit-checklist.md), [release packaging](docs/specs/release-packaging.md)
- [CDL shared baseline compliance](docs/SNARAL_SHARED_CDL_COMPLIANCE.md)
- [Shared knowledge index](KNOWLEDGE_INDEX.yaml), [DSP and workflow skills](skills/KNOWLEDGE_INDEX.yaml), [troubleshooting index](troubleshooting/INDEX.yaml)

## Build

Unit tests only (no JUCE download, fast):

```sh
tools/build_tests.sh
```

Plug-in and standalone (the first configure fetches the pinned JUCE 9.0.3 and clap-juce-extensions):

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel 2
ctest --test-dir build -C Release --output-on-failure
```

Options: `-DSNARAL_JUCE_DIR=<path>` (use a local JUCE), `-DSNARAL_CLAP_EXT_DIR=<path>`, `-DSNARAL_BUILD_CLAP=OFF`, `-DSNARAL_BUILD_PLUGIN=OFF`. See the [build guide](docs/BUILD_AND_DEVELOPMENT_GUIDE.md) for prerequisites and per-format targets.
