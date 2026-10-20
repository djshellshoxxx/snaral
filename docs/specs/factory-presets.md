# Feature Spec: Factory Preset Set

**Module:** preset-variation  
**Depends on:** parameter-state, midi-voice, sub-layer, mid-source, motion-filter, processing-output

## Objective

Ship at least ten factory patches that teach the intended workflow and demonstrate genuinely different sound paths. Presets are starting points, not artist or track recreations.

## Required preset roles

| Name | Purpose | Required behavior |
|---|---|---|
| Foundation | clean centered low bass | sine sub prominent, restrained mid, no texture |
| Plain Weight | sub-forward basic tone | movement off, sub/mid balance easy to hear |
| Throat Sweep | vowel-like moving growl | contour drives Formant Pair position and filter |
| Saw Rasp | harmonically dense rasp | Saw Stack, moderate drive, controlled peak |
| Folded Jaw | sharper serrated mid | Folded Triangle, Bite useful below maximum |
| Machine Bell | optional metallic edge | metal layer enabled quietly with short decay |
| Sync Stalker | tempo-locked movement | audible synced LFO; clear fallback free rate |
| Free Rotor | free-rate motion | host tempo not required |
| Wide Teeth | wide mid texture | Space above 0, mono sub verified |
| Legato Pressure | bassline performance patch | mono/legato/glide demonstrated, pitch stays clear |

At least three additional presets may expand the range (for example noise-driven grit, layered source morph, or restrained mid-only chord texture). All presets must remain usable at notes 24, 36, 48, and 60 and must not depend on one key.

## Authoring rules

- Normalize perceived loudness before comparing macro behavior; preserve meaningful sub/mid balance rather than loudness matching away intended character.
- Keep peak level within the limiter’s operating target with default output trim.
- Include at least one patch with Weight, Growl, Bite, Motion, and Space each producing a useful audible change.
- Do not set optional noise/metal on in every patch.
- Include descriptive tags for layer balance, movement, and texture.
- Names and descriptions must avoid claims that a patch recreates a named musician, track, sample, or preset.

## Review record

For every preset, maintain a review table with preset ID/name, parameter-state hash, tested notes, sample rate, level-matched reference condition, peak/RMS measurements, listening observations, and reviewer/date. Measurements and listening claims are added only after the actual render/review.

## Acceptance criteria

1. At least ten valid, distinct factory presets are included.
2. Every required role above is represented.
3. Each passes schema validation and note/velocity matrix from the parent spec.
4. Repeated load/render under identical conditions produces repeatable output.
5. The sub remains centered and clean in every preset; Space never widens it.
6. No preset uses an artist/track name or unsupported content license.
