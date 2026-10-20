# Snaral Expansion Roadmap

**Status:** Post-core-release feature plan  
**Core plan:** [Initial Developer Specification](SNARAL_INITIAL_DEVELOPER_SPEC.md)  
**Feature index:** [Snaral Feature Specification Index](SNARAL_FEATURE_SPEC_INDEX.md)

This roadmap contains post-release features only. The CDL audio-app baseline features (including MIDI CC Learn, onboarding, A/B compare, themes/options, shared actions, signal visualization, and WAV export) are required in the core release and are tracked in the [CDL Compliance Map](SNARAL_SHARED_CDL_COMPLIANCE.md). Start an expansion only after the core release gates pass and its scope is selected.

**Excluded by instruction:** Performance Gesture Map has no feature spec and is not scheduled here.

## Expansion phases

### E1 — Workflow and patch management

Build after the core preset/state implementation is stable:

1. [Preset browser](specs/expansion-preset-browser.md)
2. [Undo/redo history and enhanced A/B workflow](specs/expansion-undo-ab.md) — basic A/B compare is core; this spec’s history and advanced compare behavior are expansion.
3. [Professional Preset Library](specs/expansion-preset-library.md)
4. [Preset Exchange Bundles](specs/expansion-preset-exchange.md)

The browser and history system should be implemented before pack exchange so new content has a useful discovery workflow. Preset packs and exchange share manifest and validation code.

### E2 — Performance and sound-design differentiation

Build after core voice, parameter, state, and host contracts are stable:

1. [Variation Morph](specs/expansion-variation-morph.md)
2. [Snarl Trajectory Editor](specs/expansion-snarl-trajectory.md)
3. [Sub Anchor Compensation](specs/expansion-sub-anchor.md)
4. [Motion Snapshot and Loop](specs/expansion-motion-snapshot.md)
5. [Bass Readability Monitor](specs/expansion-readability-monitor.md)
6. [MPE and Per-Note Expression](specs/expansion-mpe.md)

MPE is scheduled after the voice allocator supports reliable channel/note identity. Variation Morph should reuse exact variation snapshots rather than introduce a second preset-state mechanism.

### E3 — Nonlinear quality and effect framework

Build and verify a common latency, oversampling, and dry/wet alignment contract before adding effects:

1. [Nonlinear Oversampling Modes](specs/expansion-oversampling.md)
2. [Formant Furnace](specs/expansion-effect-formant-furnace.md)
3. [Clockwork Gate](specs/expansion-effect-clockwork-gate.md)
4. [Inharmonic Striker](specs/expansion-effect-inharmonic-striker.md)
5. [Harmonic Fracture](specs/expansion-effect-harmonic-fracture.md)
6. [Spectral Shard](specs/expansion-effect-spectral-shard.md)

Add one effect at a time and retain the clean sub bypass invariant. Reassess each remaining effect after the previous one is tested; do not bundle all effects into one release.

### E4 — Format expansion

1. [AU and CLAP Format Expansion](specs/expansion-format-support.md)

Run after VST3 and standalone state/automation behavior is stable. Treat each format/OS/architecture combination as a separate build and validation target.

## Candidate catalog by requested category

### Usability
- Preset browser with search, tags, favorites, and audition.
- Undo/redo history and enhanced A/B workflow; basic A/B compare is core.
- Four-slot variation morph.

### Distinctiveness
- Snarl Trajectory editor.
- Sub Anchor compensation.
- Motion Snapshot and loop.
- Bass Readability Monitor.
- Performance Gesture Map is intentionally omitted and has no specification.

### Product value
- AU/CLAP format expansion.
- Professional preset library.
- Nonlinear oversampling.
- MPE/per-note expression.
- Preset exchange bundles.

### Effects for consideration
- Formant Furnace.
- Inharmonic Striker.
- Harmonic Fracture.
- Clockwork Gate.
- Spectral Shard.

## Release and planning rules

- Each feature has a separate spec and acceptance criteria. Select one feature or a tightly coupled group for an implementation milestone.
- Update this roadmap and the feature spec before implementation when scope, parameter IDs, or state format changes.
- Any feature adding serialized data must define migration and project-state compatibility before code begins.
- Any effect adding latency must specify dry/wet alignment, host latency reporting, and CPU/memory bounds before implementation.
- Avoid feature creep in the core release: complete the first VST3/standalone gate set before beginning E1.
