# Feature Spec: Verification and Release Gates

**Module ID:** verification  
**Depends on:** all feature modules  
**Consumers:** developers, QA, audit, release owner  
**Related:** [qa-plan](qa-plan.md), [audit-checklist](audit-checklist.md), [release-packaging](release-packaging.md)

## Objective

Create repeatable evidence that each feature works alone and that the integrated instrument meets its audio, state, host, UI and performance requirements. Nothing is reported as passing unless the command below was run and its output kept.

## Exact commands

Run from the repository root. Prerequisites: CMake 3.22+, Ninja, a C++20 compiler, and for plug-in builds the JUCE 9.0.3 checkout (fetched automatically, or passed with `-DSNARAL_JUCE_DIR`).

| Purpose | Command | Notes |
|---|---|---|
| JUCE-free unit tests | `tools/build_tests.sh [ctest-regex]` | Configures `build-tests` with `-DSNARAL_BUILD_PLUGIN=OFF`, Release, Ninja; runs ctest. Every `tests/*Tests.cpp` is auto-registered as `snaral_<name>`. |
| Same, by hand | `cmake -S . -B build-tests -G Ninja -DSNARAL_BUILD_PLUGIN=OFF -DCMAKE_BUILD_TYPE=Release && cmake --build build-tests -j2 && ctest --test-dir build-tests --output-on-failure` | Portable equivalent. |
| Plug-in + JUCE tests (dev container) | `tools/build_juce.sh [target...]` | Uses the container's JUCE and clap-juce-extensions checkouts and a build lock; paths are container-specific. |
| Plug-in + JUCE tests (anywhere) | `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release [-DSNARAL_JUCE_DIR=<juce>] [-DSNARAL_CLAP_EXT_DIR=<ext>] [-DSNARAL_BUILD_CLAP=OFF]` then `cmake --build build --config Release --parallel 2` then `ctest --test-dir build -C Release --output-on-failure` | `tests/juce/*Tests.cpp` register as `snaral_juce_<name>`. |
| Single format | `cmake --build build --config Release --target Snaral_VST3` (also `Snaral_Standalone`, `Snaral_CLAP`, `Snaral_AU`, `Snaral_LV2`) | Format availability per [format support](expansion-format-support.md). |
| macOS Intel cross-build | add `-DCMAKE_OSX_ARCHITECTURES=x86_64` to the configure line | Compile and package check; unit tests run only on an Intel or Rosetta machine. |
| Sanitizers (Linux/macOS) | configure `build-san` with `-DSNARAL_BUILD_PLUGIN=OFF -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"`; separate `-fsanitize=thread` build for race checks | Run the full JUCE-free suite. |
| Plug-in validation | `pluginval --strictness-level 5 <artefact>`; `clap-validator validate Snaral.clap`; `auval -v aumu Snrl Cdlb` | See [qa-plan](qa-plan.md); exit code 0 required. |

## CI matrix

Current (`.github/workflows/build.yml`): ubuntu-24.04, macos-15 (arm64), windows-2022; configure, build and ctest in Release; plug-in outputs uploaded as artefacts; tag build publishes zips. Required for beta (owner: release-eng): add (a) a Linux job running `tools/build_tests.sh` plus the ASan/UBSan build, (b) a macOS x86_64 cross-build job, (c) per-format artefact upload (VST3, CLAP, AU, LV2, Standalone), (d) pluginval, clap-validator and auval steps, (e) a packaging check that fails when an expected artefact is missing. Unrun steps stay marked UNVERIFIED in the report.

## Test layers

1. **Unit** (JUCE-free, each under 10 s): parameter table, macro maps, voice allocator, ADSR, oscillators, SVF, LFO, contour, trajectory, drive, limiter, oversampler, effects, MIDI-learn mapping logic, randomize, WAV writer, render driver.
2. **Integration:** MIDI to audio, sub/mid isolation, state round-trip, macro destinations, tempo fallback, limiter and width invariants.
3. **Stress/fuzz:** randomized parameter endpoints and midpoints for at least 10,000 blocks; malformed preset and state files; randomized MIDI order (100,000 events); repeated prepare/release.
4. **Audio render:** deterministic offline renders for pitch, aliasing, clicks, stereo/sub invariants, drive compensation, ceiling, preset comparison.
5. **UI/host:** window sizes, scroll, high DPI, automation gestures, format discovery, project reopen, standalone device changes.
6. **Manual listening:** level-matched, solo and in a dense drum and bass context, using the template in [qa-plan](qa-plan.md).

## Required coverage matrix

- Sample rates 44.1, 48, 88.2, 96, 192 kHz. Block sizes 1, 64, 128, 256, 512, 1024, 2048 (plus a 0-sample block).
- Notes 12, 24, 36, 48, 60, 72, 84, 96; guaranteed tracking range 24 to 84.
- Voice: mono, legato, glide, polyphony 1 to 8, sustain pedal, panic.
- Host: valid tempo, missing tempo, transport stop, loop jump, state restore.
- Layers: Weight 0/max, Growl 0/max, Space 0/max, drive 0/max, limiter on/off, solo/mute combinations.
- Beta features: oversampling off/2x/4x, Furnace and Fracture on/off, Trajectory on/off and loop, Sub Anchor on/off, undo/A-B, MIDI CC mapping, WAV export (each sample rate and bit depth).
- Presets: every factory preset, the full-featured fixture, malformed and future-version fixtures.
- Exhaustive parameter combinations are not claimed; targeted pairwise and random sampling are.

## Release gates (BETA READY)

All must hold, with evidence linked in the QA report:
- Every acceptance criterion of every beta spec passes or carries an approved, recorded exception with owner and follow-up.
- No known crash, stuck note, NaN/Inf, or sub-centering failure.
- Every shipped artefact in [release-packaging](release-packaging.md) builds in CI and passes its validator; unvalidated formats are excluded or labelled experimental.
- [audit-checklist](audit-checklist.md) finished with zero unwired parameters.
- Performance recorded: machine, compiler, rate, block, voices, median and p99 callback time.
- UI screenshots at 720x480, 900x560 and 1280x720, Advanced scroll, 150% and 200% scaling.
- Factory presets and embedded assets have provenance and license records; JUCE and clap-juce-extensions licensing is confirmed before distribution.

## Test report template

```
Build: <git sha> <tag>   Date: <UTC>   Machine: <CPU/OS/compiler>
Command: <exact command>
Result: PASS | FAIL | BLOCKED | UNVERIFIED   Evidence: <log path or CI run URL>
Failures: <test name, parameter snapshot, MIDI sequence, rate/block, host timing, audio file>
Classification: DSP | MIDI/host | state | UI/UX | performance | harness
```

## Failure triage

Classify as DSP correctness, MIDI/host integration, state compatibility, UI/UX, performance, or harness. Isolate harness failures from plug-in failures. Keep minimal reproduction data: parameter snapshot, MIDI sequence, host timing, rate, block size, host and build details, rendered audio.
