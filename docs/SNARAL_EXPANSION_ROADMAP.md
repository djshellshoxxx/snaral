# Snaral Expansion Roadmap

**Status:** Staged feature plan; items marked BETA were promoted into the v0.2 beta by the [Beta Implementation Plan](IMPLEMENTATION_PLAN.md)
**Core plan:** [Initial Developer Specification](SNARAL_INITIAL_DEVELOPER_SPEC.md)
**Feature index:** [Snaral Feature Specification Index](SNARAL_FEATURE_SPEC_INDEX.md)

The CDL audio-app baseline features (MIDI CC Learn, onboarding, basic A/B compare, themes and Options, shared actions, signal visualization, WAV export) are core and are tracked in the [CDL Compliance Map](SNARAL_SHARED_CDL_COMPLIANCE.md). The items below are expansions. Those marked **BETA** ship in the v0.2 beta and are no longer waiting for the core release gates; those marked **POST-BETA** have complete specs but must not be implemented until the beta is released and a scope is selected.

**Excluded by instruction:** Performance Gesture Map has no feature spec and is not scheduled here.

## Promotion summary

| Expansion item | Spec | Status |
|---|---|---|
| Preset browser | [expansion-preset-browser](specs/expansion-preset-browser.md) | BETA |
| Undo/redo history (100 steps) and extended A/B | [expansion-undo-ab](specs/expansion-undo-ab.md) | BETA |
| Professional preset library: larger factory set (24 or more presets) | [factory-presets](specs/factory-presets.md) | BETA |
| Professional preset library: themed packs | [expansion-preset-library](specs/expansion-preset-library.md) | POST-BETA |
| Preset exchange bundles | [expansion-preset-exchange](specs/expansion-preset-exchange.md) | POST-BETA |
| Variation morph | [expansion-variation-morph](specs/expansion-variation-morph.md) | POST-BETA |
| Snarl Trajectory editor | [expansion-snarl-trajectory](specs/expansion-snarl-trajectory.md) | BETA |
| Sub Anchor compensation | [expansion-sub-anchor](specs/expansion-sub-anchor.md) | BETA |
| Motion Snapshot and loop | [expansion-motion-snapshot](specs/expansion-motion-snapshot.md) | POST-BETA |
| Bass Readability Monitor | [expansion-readability-monitor](specs/expansion-readability-monitor.md) | POST-BETA |
| MPE and per-note expression | [expansion-mpe](specs/expansion-mpe.md) | POST-BETA |
| Nonlinear oversampling (Off, 2x, 4x) | [expansion-oversampling](specs/expansion-oversampling.md) | BETA |
| Formant Furnace | [expansion-effect-formant-furnace](specs/expansion-effect-formant-furnace.md) | BETA |
| Harmonic Fracture | [expansion-effect-harmonic-fracture](specs/expansion-effect-harmonic-fracture.md) | BETA |
| Clockwork Gate | [expansion-effect-clockwork-gate](specs/expansion-effect-clockwork-gate.md) | POST-BETA |
| Inharmonic Striker | [expansion-effect-inharmonic-striker](specs/expansion-effect-inharmonic-striker.md) | POST-BETA |
| Spectral Shard | [expansion-effect-spectral-shard](specs/expansion-effect-spectral-shard.md) | POST-BETA |
| Format expansion: AU, CLAP, LV2 | [expansion-format-support](specs/expansion-format-support.md) | BETA (LV2 experimental) |

## Expansion phases

### E1 — Workflow and patch management

1. Preset browser. **BETA.**
2. Undo/redo history and extended A/B workflow. **BETA.** Basic A/B compare is core.
3. Professional Preset Library. **Split:** the expanded factory set is BETA; themed packs are POST-BETA.
4. Preset Exchange Bundles. **POST-BETA.** Library packs and exchange bundles share one `.snaralpack` container, manifest and validator (the exchange manifest carries `pack_id`, `pack_version`, `min_state_version`).

The browser and history system precede pack exchange so imported content has a discovery workflow.

### E2 — Performance and sound-design differentiation

1. Variation Morph. **POST-BETA.** Reuses the exact variation snapshots; no second preset-state mechanism.
2. Snarl Trajectory Editor. **BETA.**
3. Sub Anchor Compensation. **BETA** (exact formula, default off).
4. Motion Snapshot and Loop. **POST-BETA** (needs preset schema v2).
5. Bass Readability Monitor. **POST-BETA** (reads existing engine taps).
6. MPE and Per-Note Expression. **POST-BETA** (needs channel and note identity in the voice allocator).

### E3 — Nonlinear quality and effect framework

Common latency, oversampling and dry/wet alignment come first. Add one effect at a time and keep the clean sub bypass invariant.

1. Nonlinear Oversampling Modes. **BETA.**
2. Formant Furnace. **BETA.**
3. Harmonic Fracture. **BETA.**
4. Clockwork Gate. **POST-BETA.**
5. Inharmonic Striker. **POST-BETA.**
6. Spectral Shard. **POST-BETA.**

The beta ships exactly two fixed effects (Furnace then Fracture, both mid-only, before Drive). They do not form a general effect rack and the remaining three effects do not enter the beta.

### E4 — Format expansion

AU (macOS), CLAP (all desktop OS) and LV2 (Linux, best-effort) are **BETA** with VST3 and Standalone. Each format, OS and architecture is a separate build and validation target; a format whose validator has not passed is not published as supported. See [format support](specs/expansion-format-support.md) and [release packaging](specs/release-packaging.md).

## Candidate catalog by requested category

### Usability
- Preset browser with search, tags, favourites, audition (BETA).
- Undo/redo history and extended A/B (BETA).
- Four-slot variation morph (POST-BETA).

### Distinctiveness
- Snarl Trajectory editor (BETA). Sub Anchor compensation (BETA).
- Motion Snapshot and loop (POST-BETA). Bass Readability Monitor (POST-BETA).
- Performance Gesture Map is intentionally omitted and has no specification.

### Product value
- AU, CLAP and LV2 formats (BETA). Nonlinear oversampling (BETA). Larger factory library (BETA).
- Preset library packs, MPE, preset exchange bundles (POST-BETA).

### Effects
- Formant Furnace and Harmonic Fracture (BETA). Inharmonic Striker, Clockwork Gate, Spectral Shard (POST-BETA).

## Release and planning rules

- Each feature has a separate spec and acceptance criteria. Select one feature or a tightly coupled group for a milestone.
- Update this roadmap and the feature spec before implementation when scope, parameter IDs, or state format changes.
- Any feature adding serialized data defines migration and project-state compatibility before code begins. POST-BETA items that need schema changes say so in their specs.
- Any effect adding latency specifies dry/wet alignment, host latency reporting, and CPU and memory bounds before implementation.
- Avoid feature creep: nothing outside the BETA rows enters the v0.2 beta without a recorded scope decision in the implementation plan.
