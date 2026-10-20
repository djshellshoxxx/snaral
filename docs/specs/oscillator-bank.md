# Feature Spec: Oscillator Bank and Spectral Sources

**Module:** mid-source  
**Depends on:** parameter-state, midi-voice  
**Used by:** motion-filter, factory presets

## Objective

Define the initial set of pitched source materials for the mid layer without relying on licensed samples or one signature wavetable. The sources should span useful harmonic character and remain stable across the supported pitch range.

## Initial source set

Use four built-in, generated oscillator families:

1. **Saw Stack:** band-limited saw with a small set of deterministic phase offsets, summed with bounded normalization.
2. **Pulse Pair:** band-limited pulse/square with pulse width selectable from 20% to 80%.
3. **Folded Triangle:** triangle with bounded wavefold amount; default fold is mild and stable.
4. **Formant Pair:** two band-limited harmonic sources with movable spectral peaks approximating vowel-like emphasis. This is a synthesis shape, not a voice sample.

Source A and Source B select from this bank. The initial blend crossfades the two source outputs. A future user wavetable import feature is out of scope.

## Pitch and aliasing

- Sources follow the shared voice pitch, glide, and pitch bend.
- Use band-limited oscillators or mipmapped wavetables that select a safe harmonic level for current pitch and sample rate.
- Built-in tables are generated offline or at initialization, never during audio rendering.
- Oscillator state is per voice and reset/reused deterministically according to the voice/retrigger contract.
- No random phase behavior in the initial implementation. A deterministic phase offset per oscillator in a source family is allowed and documented.
- No DC offset greater than −90 dBFS in a steady-state render.

## Source position

A single source-position parameter is available for the Formant Pair and Folded Triangle. It sweeps only within each family’s defined safe range. Position modulation from contour/LFO must be bounded and smoothed.

## Acceptance criteria

1. Each family produces finite, pitch-tracking output at notes 12, 24, 36, 60, 84, and 96 at all supported sample rates.
2. A 20 kHz upper-band sine sweep render at 44.1 and 48 kHz shows no unexpected foldback above the agreed anti-aliasing threshold; define and record the measurement method in implementation.
3. Source generation performs no allocation or table generation in the audio callback.
4. Repeated render from the same reset state and same MIDI/parameter input is bit-identical where the chosen math path permits, otherwise sample deviation is documented and bounded.
5. The source manifest documents oscillator family, algorithm version, table generation, and any external dependency (expected none).
6. Blend endpoints and midpoint pass the level-step requirement in the sub-mid engine spec.
