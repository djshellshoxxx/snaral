# Feature Spec: Plug-in Format Support (VST3, Standalone, CLAP, AU, LV2)

**Category:** Product value  
**Status:** Promoted into the v0.2 beta (see [implementation plan](../IMPLEMENTATION_PLAN.md)); the filename is kept for link stability.  
**Depends on:** host-integration, parameter-state, verification, [release-packaging](release-packaging.md)

## Objective

Ship the same instrument in every format listed below with identical DSP, parameter IDs, state, presets and UI, and label each format honestly by what has been validated.

## Beta format matrix

| Format | Windows x64 | macOS arm64 | macOS x86_64 | Linux x64 | Beta status |
|---|---|---|---|---|---|
| VST3 | yes | yes | yes | yes | Supported when pluginval level 5 passes |
| Standalone | yes | yes | yes | yes | Supported |
| CLAP | yes | yes | yes | yes | Supported when clap-validator passes |
| AU (`aumu Snrl Cdlb`) | n/a | yes | yes | n/a | Supported when `auval` passes |
| LV2 | n/a | n/a | n/a | yes | Best-effort, labelled experimental |

Not in beta: AAX, VST2, AUv3, LADSPA, Windows on ARM, universal macOS binaries (arm64 and x86_64 ship as separate artefacts), Linux ARM. 32-bit is never built.

## Build facts (current CMake)

- JUCE 9.0.3 `juce_add_plugin` formats: `VST3 Standalone`, plus `AU` on Apple, plus `LV2` on Linux. CLAP comes from `clap_juce_extensions_plugin` (target `Snaral_CLAP`) when `SNARAL_BUILD_CLAP=ON` (default). Targets: `Snaral_VST3`, `Snaral_Standalone`, `Snaral_AU`, `Snaral_LV2`, `Snaral_CLAP`.
- IDs: bundle `com.circuitdriftlabs.snaral`; AU type `aumu`, subtype `Snrl`, manufacturer `Cdlb`; CLAP id `com.circuitdriftlabs.snaral`, features instrument, synthesizer, bass, stereo. Changing any of these breaks saved projects and needs a recorded decision.
- Minimum macOS 11.0 (`CMAKE_OSX_DEPLOYMENT_TARGET`). Windows uses MSVC 2022 (CI windows-2022). Linux binaries are built on ubuntu-24.04 and need a comparable or newer glibc.
- LV2 requires a unique `LV2URI` that must change on any incompatible parameter or state change. The CMake default is derived from the company website (unset here), so the beta must set an explicit URI (task for the integration owner); until then LV2 is not published.
- Standard install locations (user level): VST3 `~/.vst3` (Linux), `~/Library/Audio/Plug-Ins/VST3` (macOS), `%CommonProgramFiles%\VST3` (Windows); CLAP `~/.clap` (Linux), `~/Library/Audio/Plug-Ins/CLAP` (macOS), `%CommonProgramFiles%\CLAP` (Windows); AU `~/Library/Audio/Plug-Ins/Components`; LV2 `~/.lv2`.

## Format-neutral contract

- Same registry-derived parameters: 99 parameters, 91 automatable (`pfNoAutomation` excluded). A format that cannot represent a parameter type (for example LV2 choice ports) maps it to the nearest native type without changing IDs or ranges.
- Host state blobs are the same bytes in every format; a state saved by one format loads in another without value drift (1e-6).
- Latency, MIDI input acceptance, bypass, and automation gesture behaviour follow host-integration.
- No format-specific features. Format-specific code is limited to wrapper configuration.

## Edge cases

- Hosts that scan unsigned bundles: macOS requires at least an ad-hoc signature for arm64 code; Windows may warn on unsigned binaries (see release-packaging).
- AU: description conflicts with an installed older build are resolved by removing the old component before `auval`.
- CLAP and LV2 hosts may not deliver MIDI CC to instruments; this is a host matter recorded in the QA report.
- A format whose validator fails or cannot run is not published; if built, it is labelled EXPERIMENTAL or excluded by the release owner with a recorded reason.

## Acceptance criteria

1. Each Supported format passes its validator (`pluginval --strictness-level 5`, `clap-validator validate`, `auval -v aumu Snrl Cdlb`) with exit code 0, logged in the QA report; LV2 passes a bundle structure check (manifest and plugin descriptions present, loads in at least one LV2 host).
2. Parameter IDs, counts, ranges, MIDI acceptance, automation, state save and restore, latency and bypass match VST3 in a cross-format test.
3. CI builds every cell of the matrix above; a missing artefact fails the job.
4. A cross-format state fixture saved by VST3 restores in CLAP and AU with no drift, and vice versa where the wrapper exposes state.
5. Release notes list each format with its status and the exact hosts tested; unverified cells say UNVERIFIED.
6. No format is published until criteria 1 to 4 pass for it.

## Test fixtures

Cross-format state fixture (`tests/juce/fixtures/state_full.bin` produced by the state worker), validator logs under the QA report, host matrix table.
