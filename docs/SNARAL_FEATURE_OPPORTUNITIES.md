# Snaral Feature Opportunities

**Purpose:** Prioritized product ideas beyond the essential initial-release scope  
**Current product contract:** [Initial Developer Specification](SNARAL_INITIAL_DEVELOPER_SPEC.md) and [Feature Specification Index](SNARAL_FEATURE_SPEC_INDEX.md)

The essential additions required before a first release are specified in [Performance Foundation](specs/performance-foundation.md). The ideas below are proposals, not current requirements. They should be selected only after the initial instrument passes its sound, state, and host acceptance gates.

## Five features to improve usability

| Feature | User benefit | Suggested scope |
|---|---|---|
| Preset browser with search, tags, and favorites | Finds the right sound quickly as the library grows | Search name/description/tags; filter by sub/mid balance, motion, and texture; favorite and sort; never block audio |
| Undo/redo plus A/B compare | Makes sound exploration safer and fast to compare | 100-step parameter history; A/B snapshots; compare at matched output trim; exclude transient meter values |
| MIDI CC Learn | Maps hardware knobs to macros without manual CC setup | Learn one CC per target, conflict warning, per-instance mapping persistence, reset-to-default |
| Four-slot variation morph | Smoothly moves between exact stored A–D patches | Morph macro with selectable parameter interpolation; discrete enums switch at midpoint; sub level remains bounded |
| Onboarding sound tour | Teaches the five macros and split-layer workflow | Short interactive in-plugin walkthrough with skip/restart; no network dependency; uses built-in patches |

## Five features to make Snaral more distinctive

| Feature | Differentiator | Suggested scope |
|---|---|---|
| Snarl Trajectory editor | Lets users draw a deliberate body → peak → return contour instead of choosing only a generic envelope | Three to five draggable points; synced or millisecond time; fixed filter/source destinations; no matrix |
| Sub Anchor range compensation | Maintains a chosen low-end balance as notes move across octaves | User sets anchor note and strength; bounded compensation; optional, default off; never changes stereo center |
| Performance Gesture Map | Maps mod wheel, channel pressure, and selected MIDI CC to macro depth in real time | Small fixed set of mappings with depth/curve; no arbitrary routing graph |
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

Build the usability features first when they remove workflow friction without changing the sound engine: preset browser, A/B and undo/redo, then MIDI CC Learn. Treat Sub Anchor and Snarl Trajectory as sound-engine differentiators for a later milestone. Evaluate each effect independently; do not add all five to one first expansion. The best first effect candidate is Formant Furnace because it extends the existing growl concept while reusing the current filter/motion architecture.
