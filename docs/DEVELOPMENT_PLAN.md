# Snaral Development Plan

**Status:** Active implementation plan
**Roadmap:** [Initial Developer Specification](SNARAL_INITIAL_DEVELOPER_SPEC.md)
**Beta scope and ownership:** [Beta Implementation Plan](IMPLEMENTATION_PLAN.md)
**Build guide:** [Build and Development Guide](BUILD_AND_DEVELOPMENT_GUIDE.md)
**Spec index:** [Feature Specification Index](SNARAL_FEATURE_SPEC_INDEX.md)
**Shared requirements:** [CDL Compliance Map](SNARAL_SHARED_CDL_COMPLIANCE.md)

## Delivery stages

| Stage | Outcome | Exit evidence | Status |
|---|---|---|---|
| 0. Build foundation | Reproducible JUCE VST3/standalone build, CI, modular source layout, minimal MIDI sine | CI builds all target OSes; test target runs; MIDI note produces finite stereo audio | Sine prototype, CMake and a three-OS CI workflow exist; no green cross-platform CI run is recorded in this repository's docs; CLAP and AU CMake added, not yet validated |
| 1. Engine and parameter contract | Stable parameter registry, voice lifecycle, sub/mid split, envelopes, macros, filters, modulation, centered sub | Unit, render, aliasing, note-storm, state-independent DSP tests pass | In progress: full 99-parameter registry and tests exist; engine still the sine prototype |
| 2. Host and state | Automation, transport, pitch/tuning, project restore, versioned preset JSON, four variations | Host round-trip, malformed input, migration, automation stress pass | Not started beyond the macro state schema v1 |
| 3. CDL workflow | Themes/options, Stop/Panic, Reset/Randomize/Bypass, context actions, preset selection, MIDI CC Learn, A/B, help/onboarding, scope and CPU indicator | Baseline mapping complete; usability, accessibility, performance evidence recorded | Specified; not started |
| 4. Content and standalone utilities | Ten or more factory presets (24 or more for the beta), import/export, standalone devices, reference tone, offline WAV render | Preset review and WAV interoperability checks pass; standalone recovery verified | Specified; not started |
| 5. Release validation | Functional, host, format, audio, performance, UI, security/license, packaging checks | All gates passed or each exception explicitly approved ([verification](specs/verification.md), [audit](specs/audit-checklist.md), [QA](specs/qa-plan.md), [packaging](specs/release-packaging.md)) | Specified; not started |

Stages 1 to 4 may overlap only where module contracts are stable. Preset content is not authored against unstable parameter IDs. The CDL workflow scope is part of initial release completion and must not be moved into post-release expansion without a recorded product decision.

## Beta wave mapping

The v0.2 beta runs the stages above in the waves of the [implementation plan](IMPLEMENTATION_PLAN.md): wave 1 builds DSP blocks, state, UI kit and specs in parallel; wave 2 integrates the engine and CDL services; wave 3 builds the editor; wave 4 audits, runs QA and releases. Beta additions beyond the core: Formant Furnace, Harmonic Fracture, 100-step undo/redo with A/B, Snarl Trajectory, oversampling, the 24-preset factory set, the preset browser, Sub Anchor, and AU, CLAP and LV2 formats. Everything else in the roadmap is post-beta.

## First implementation tasks

1. Bootstrap CMake/JUCE and CI; maintain the no-allocation callback rule.
2. Prove MIDI note-on/off, polyphonic allocation, and clean release on VST3 and standalone.
3. Define parameter IDs and state shape before exposing host automation.
4. Implement the parallel sub/mid engine and test sub isolation before adding advanced effects.
5. Complete baseline workflow, content, and export requirements in the order above.

### Active Stage 1 slice

1. Five public macro IDs, names, defaults and normalized/domain conversion in a JUCE-independent header. **Implemented; tests pass locally.**
2. Normative macro mapping functions at boundary and interior values. **Implemented; tests pass locally.**
3. Macros exposed as JUCE host parameters, host state validated and versioned, Weight connected to the sine prototype. **Implemented; JUCE build and host verification pending.**
4. MIDI pitch wheel routed to the sine voice with endpoint and center tests. **Implemented; cross-platform CI pending.**
5. Full parameter registry (99 IDs in `Source/Parameters/ParameterTable.h`, group files under `Source/Parameters/Groups/`), a JUCE layout generator (`JuceParameterLayout`) and registry tests. **Implemented in the foundation commit; the processor does not use the full layout yet, so only the five macros are host-visible in the current plug-in.**
6. Connect the full layout, state schema (parameters, slots, A/B, MIDI map) and engine to the processor. **Pending (integration and state workers).**

The current plug-in does not claim that Growl, Bite, Motion, or Space affect the sine prototype. Their tested mapping functions are ready for the sub/mid engine destinations. This slice does not count Stage 1 as complete.

## Open items register (documentation audit)

Every TODO, ambiguity or gap found when auditing the docs, with its resolution. Owner "spec" means resolved in this documentation pass.

| Finding | Where | Resolution |
|---|---|---|
| Exact build and test commands unresolved | verification | Resolved in [verification](specs/verification.md) and the build guide |
| No audit, QA or packaging spec; plan refers to `docs/release/AUDIT.md` and `QA_REPORT.md` | implementation plan | New [audit-checklist](specs/audit-checklist.md), [qa-plan](specs/qa-plan.md), [release-packaging](specs/release-packaging.md); release-eng writes the filled reports to `docs/release/` |
| Initial spec parameter table differs from registry (`sub_attack_ms`, `sub_release_ms`; missing `mid_level`, `mid_pulse_width`, mid ADSR, utility and beta IDs) | initial spec section 5 | Table updated to registry IDs and ranges; the registry stays authoritative |
| Initial spec defers AU, CLAP, undo, browser, oversampling | initial spec sections 2 and 11 | Beta-scope note added; format and feature specs completed |
| Sub Anchor spec had a flat/gentle/steep curve but no registry parameter; "bounded function" undefined | sub-anchor | Exact `tanh` formula, curve selector dropped, test values given |
| Bite "+12 dB of drive amount" ambiguous (drive amount is percent); Bite filter part at Motion 0 undefined; Motion scope for new modulators undefined | macro-mapping | Defined: pre-gain dB, contour peak offset, Motion also scales Trajectory and Furnace depths (consumers must confirm) |
| WAV export lacked float, length, naming, normalisation, progress and error rules | offline WAV export | Fully specified |
| MIDI Learn lacked learnable set, reserved list, conflict flow, persistence | midi-cc-learn | Fully specified |
| Format spec lacked per-OS matrix and beta decisions | format-support | Matrix, validators and decisions added |
| Common actions: duplicated acceptance number, undefined Randomize ranges, seed storage, Reset scope | cdl-common-actions | Algorithm version 1 specified |
| UI layout had no Effects tab, Trajectory panel, browser, undo/A-B bar, oversampling, status line | ui-layout | Added with parameter-to-control table |
| Post-beta specs (MPE, Variation Morph, Motion Snapshot, Readability Monitor, Preset Exchange) thin or inconsistent | specs | Completed with parameters/state needs, limits and fixtures; scope unchanged |
| Preset Library and Preset Exchange overlap (two pack formats) | library, exchange | One `.snaralpack` container; manifest carries library fields |
| Trajectory spec says 3 to 8 points; registry has five fixed points (x1=0, x5=1) | expansion-snarl-trajectory (not editable here) | UI follows the registry; trajectory owner to align the spec |
| Undo/A-B spec leaves "compare original level" conditional ("if ... proves confusing") | expansion-undo-ab (not editable here) | Needs a decision from its owner; default is matched trim |
| Host state schema v1 covers only the five macros | parameter-state (not editable here) | State owner must extend for the full registry, slots, A/B, MIDI map, solo/mute |
| `.github/prompts/implement-macro-host-parameters.prompt.md` is a finished one-off task and says not to add Advanced parameters | `.github/` (not editable here) | Archive or delete after the macro slice; do not reuse for the beta |
| `standards/RELEASE_REQUIREMENTS.md` is "pending approved product requirements" | shared standard (copied) | Snaral instantiation lives in verification, QA and packaging specs |
| CMake fetches clap-juce-extensions at branch `main` (not reproducible); no `LV2URI`; no LICENSE file; JUCE is AGPLv3/commercial dual-licensed | CMake, repo root | Release gate in [release-packaging](specs/release-packaging.md); pin the dependency and choose a licence before tagging |
| CI packages only VST3 and Standalone, names differ from the matrix, no macOS x86_64, no validators | `.github/workflows/build.yml` (release-eng) | Required changes listed in verification and release-packaging |
| `limiter_enabled` and `limiter_ceiling_db` are host-automatable, so automation can defeat the safety stage (see troubleshooting record on defeatable safety limiters) | registry | Accepted by the initial spec (limiter has an enable parameter); excluded from MIDI Learn; QA stress run must include limiter off |
| Hosts may not forward CC to instruments | MIDI Learn | Per-host verification recorded in QA |

## Expansion gate

The beta includes the roadmap items marked BETA. Remaining items in the [Expansion Roadmap](SNARAL_EXPANSION_ROADMAP.md) start only after the beta release and a recorded scope selection. Performance Gesture Map remains excluded by instruction.
