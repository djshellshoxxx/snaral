# Snaral Feature Opportunities

**Purpose:** Prioritized post-release product ideas
**Current product contract:** [Initial Developer Specification](SNARAL_INITIAL_DEVELOPER_SPEC.md) and [Feature Specification Index](SNARAL_FEATURE_SPEC_INDEX.md)

Core requirements, including the CDL audio-app baseline, are tracked in the [initial developer specification](SNARAL_INITIAL_DEVELOPER_SPEC.md) and [compliance map](SNARAL_SHARED_CDL_COMPLIANCE.md). The ideas below are post-release proposals. Select them only after the core sound, state, host, and CDL gates pass.

## Five features to improve usability

| Feature | User benefit | Suggested scope |
|---|---|---|
| Preset browser search and favorites | Finds a sound quickly as the library grows | Search metadata and tags; filter by sound character; favorites and sorting; never block audio |
| Undo/redo history | Makes sound exploration reversible | Bounded 100-step parameter history; coalesce continuous edits; exclude transient meter values |
| Variation morph | Moves between exact stored A–D snapshots | Selectable parameter interpolation; discrete enums switch at midpoint; keep sub level bounded |
| Preset comparison details | Helps choose between similar patches | Compare tags, macro values, and audio preview level without changing active state |
| Preset exchange bundles | Shares curated patches between users | Versioned JSON bundle, validation, optional thumbnail/metadata, no external paths |

## Five features to make Snaral more distinctive

| Feature | Differentiator | Suggested scope |
|---|---|---|
| Snarl Trajectory editor | Lets users draw a deliberate body → peak → return contour instead of choosing only a generic envelope | Three to five draggable points; synced or millisecond time; fixed filter/source destinations; no matrix |
| Sub Anchor range compensation | Maintains a chosen low-end balance as notes move across octaves | User sets anchor note and strength; bounded compensation; optional, default off; never changes stereo center |
| Gesture performance mapping | Adds playable variation to macro control | Small fixed mapping set with depth and curve; no arbitrary routing graph |
| Motion Snapshot and loop | Records one knob gesture and replays it as a tempo-synced or free loop | Record one macro lane at a time; quantize optional; stored as patch data; deterministic playback |
| Bass Readability Monitor | Shows sub level, mid/sub overlap, and stereo correlation as diagnostics | Read-only visual diagnostics; no automatic tone correction; clearly labels measurements and refresh rate |

## Five features that could increase product value

| Feature | Product value | Suggested scope |
|---|---|---|
| Format expansion after VST3 validation | Reaches users of more DAWs and operating systems | Add AU and CLAP after shared state and parameter behavior pass compatibility tests |
| Professional preset library | Gives users immediate production-ready starting points | Release themed packs with consistent loudness review, tags, and documented authorship |
| High-quality oversampling modes | Reduces aliasing in aggressive distortion settings | Off/2×/4× selectable; per-engine CPU measurements; report latency accurately |
| MPE/expression support | Enables expressive keyboard and controller performance | Per-note pitch bend and pressure only after voice architecture is stable; preserve ordinary MIDI behavior |
| Exportable preset exchange | Lets users share patches across installations and future releases | Versioned JSON bundle, optional thumbnail and metadata; no external asset/path dependencies |

## Five additional unique effects for consideration

These effects are candidates for a later effect module. Each should remain mid-only by default, preserve a clean centered sub path, have bounded parameters, and be compared against current processing before implementation.

### 1. Formant Furnace

A moving bank of two to four resonant peaks creates vowel-like, throat, and nasal motion.

**Controls:** vowel region, peak spacing, resonance, envelope amount, LFO amount, wet mix.
**Behavior:** normalize output gain; cap resonance below self-oscillation; allow contour to sweep the peak bank.
**Acceptance:** finite output at all controls; no self-oscillation; mono fold-down remains useful; sub samples remain unchanged.

### 2. Inharmonic Striker

A bounded resonator adds struck-metal or machine-clang partials that track MIDI with configurable inharmonic offsets.

**Controls:** strike amount, decay, partial spacing, damping, tune, wet mix.
**Behavior:** use a small fixed resonator bank; no feedback between resonators; default off.
**Acceptance:** deterministic response; bounded decay; no stuck energy after note-off; no sub-path processing.

### 3. Harmonic Fracture

A multiband waveshaper applies different bounded folding or saturation curves to low-mid, mid, and upper-mid bands.

**Controls:** crossover, per-band drive, fold symmetry, band mix, output compensation.
**Behavior:** operate above a configurable minimum frequency so sub remains clean; use smoothed crossover and level compensation.
**Acceptance:** no DC buildup, NaN/Inf, or uncontrolled peak growth; crossover sweep produces no persistent notch; mono fold-down passes fixture review.

### 4. Clockwork Gate

A host-synced stepped amplitude gate creates repeated cuts and accents on the mid layer.

**Controls:** rate/division, 8-step depth pattern, swing, smoothing, wet mix.
**Behavior:** sync to host tempo or use free-rate fallback; gate only mid; preserve MIDI note envelope and sub.
**Acceptance:** step timing matches host tempo within 1 ms or one sample; pattern repeats deterministically; no clicks beyond configured smoothing; sub output is unchanged.

### 5. Spectral Shard

A short, bounded spectral capture freezes a moment of the mid voice, then shifts selected spectral bands to create a metallic smear or rising/falling shard.

**Controls:** capture trigger, hold time, shift range, blur, decay, wet mix.
**Behavior:** capture only a fixed-size internal buffer; no disk access; reset on transport stop; keep latency and CPU cost visible.
**Acceptance:** fixed maximum memory use; reported latency is correct; capture/release produces no click or stuck state; effect is mid-only and disabled by default.

## Prioritization guidance

Build workflow enhancements first when they remove friction without changing the sound engine: search/favorites, undo history, then variation morph. MIDI CC Learn, first-run onboarding, and basic A/B compare are core CDL requirements. Treat Sub Anchor and Snarl Trajectory as sound-engine differentiators for a later milestone. Evaluate each effect independently; do not add all five to one first expansion. The best first effect candidate is Formant Furnace because it extends the existing growl concept while reusing the current filter/motion architecture.
