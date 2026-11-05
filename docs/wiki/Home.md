# Snaral Wiki

Snaral is a MIDI-playable drum and bass synthesizer. It is built around a stable, centred sub layer and a separately shaped mid voice. It is a JUCE 9 VST3 and standalone plug-in.

**Status:** prototype. The current build has an eight-voice MIDI sine instrument and five host-automatable macros. Only **Weight** changes the sound so far. Growl, Bite, Motion and Space are defined but do not yet drive any DSP. This is not a finished instrument.

**Who it is for:** producers who want a sub and mid bass tool for DnB and dark bass, and developers working on the synth. Most of the design is in `docs/specs/`.

## Quick start

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel 2
ctest --test-dir build -C Release --output-on-failure
```

The first configure fetches the pinned JUCE dependency. Plug-in and engine-only test targets are described in [[Developer-Build-and-Test]].

## Pages

**For users**
- [[User-Guide]]: what works now, the five macros, and what is still planned

**For developers**
- [[Developer-Overview]]: architecture, design decisions, signal flow
- [[Developer-Code-Map]]: files and symbols, and where to change what
- [[Developer-Build-and-Test]]: build, tests, CI
- [[Developer-Conventions]]: rules, real-time constraints, gotchas

**Repo docs (authoritative)**
- `docs/DEVELOPMENT_PLAN.md`, `docs/BUILD_AND_DEVELOPMENT_GUIDE.md`, `docs/SNARAL_INITIAL_DEVELOPER_SPEC.md`
- `docs/SNARAL_FEATURE_SPEC_INDEX.md` and the `docs/specs/` directory
- `standards/` (shared CDL standards)
