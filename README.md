# Snaral

Snaral is a MIDI-playable drum and bass synthesizer built around a stable, centered sub layer and a separately shaped mid voice.

**Current state:** prototype. The repository contains an eight-voice MIDI sine instrument, five host-automatable macro controls, and extensive product specifications. Only Weight currently changes the sine voice level; Growl, Bite, Motion, and Space await their specified DSP destinations. This is not a finished or production-ready instrument. The build commands below compile development targets; successful compilation alone does not establish DAW compatibility or a published release.

## Start here

- [Development plan](docs/DEVELOPMENT_PLAN.md)
- [Build and development guide](docs/BUILD_AND_DEVELOPMENT_GUIDE.md)
- [Initial developer specification](docs/SNARAL_INITIAL_DEVELOPER_SPEC.md)
- [Feature specification index](docs/SNARAL_FEATURE_SPEC_INDEX.md)
- [UI layout specification](docs/specs/ui-layout.md)
- [CDL shared baseline compliance](docs/SNARAL_SHARED_CDL_COMPLIANCE.md)
- [Shared knowledge index](KNOWLEDGE_INDEX.yaml)
- [DSP and workflow skills](skills/KNOWLEDGE_INDEX.yaml)
- [Troubleshooting index](troubleshooting/INDEX.yaml) and [entry template](troubleshooting/templates/TROUBLESHOOTING_TEMPLATE.md)
- [Copilot prompt: implement macro host parameters](.github/prompts/implement-macro-host-parameters.prompt.md)

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel 2
ctest --test-dir build -C Release --output-on-failure
```

The first configure fetches the pinned JUCE dependency. See the [build guide](docs/BUILD_AND_DEVELOPMENT_GUIDE.md) for prerequisites and engine-only testing.
