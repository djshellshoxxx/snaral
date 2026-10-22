# Requested Workflow, Keyboard, and Effects Specification

**Status:** Product requirements and staged implementation contract  
**Source:** User's annotated Snaral UI screenshots and follow-up request  
**Parent:** [Feature specification index](../SNARAL_FEATURE_SPEC_INDEX.md)

## 1. Playback and keyboard access

- Standalone must produce an audible note without a DAW or external MIDI controller. Provide a clearly visible Play/Stop audition control; Space toggles audition, and note-off must be sent on stop, editor close, transport stop, and panic.
- Preserve host MIDI input. Provide a virtual piano keyboard connected to the same MIDI event path. Keyboard panel opens from a bright, prominent **Keyboard** button, can be moved, collapsed to a narrow draggable header, expanded by double-clicking its header, and closed with an X.
- Provide QWERTY piano input while the virtual keyboard is open. Tab order covers every action and parameter; arrows adjust focused controls. Shortcuts must not trigger while a text entry control owns focus.
- Import and export standard MIDI files. Import previews the file in standalone with transport controls, tempo-aware event timing, and clean stop/rewind behavior. Export writes recorded keyboard input or the selected pattern as a valid Standard MIDI File. MIDI file operations never run on the audio thread.
- Add a main-page pitch-bend control with configurable range and center detent. It must combine predictably with incoming MIDI pitch bend.

## 2. Main-page macros and macro detail window

- Right-clicking any macro opens a context menu containing **Macro controls**.
- The detail window exposes the macro's effect chain in processing order. For every effect, expose its relevant major parameters plus enable/bypass, wet, dry/level, mix, modulation amount/rate/shape, and pan where stereo processing applies.
- The window remains open while controls are edited and closes when the user clicks outside. Changes apply immediately and persist with patch/project state. **Reset** restores only that macro chain's factory defaults.
- All macro and effect settings have stable IDs, automation/state behavior, sensible bounds, keyboard support, and no parameter duplication that causes macro movement to rewrite advanced baselines.

## 3. Signal display and Options

- Clicking the main waveform toggles between an output waveform and a layered signal view. The layered view uses distinct colors for actual intermediate and final DSP signals; it must never invent an effect contribution that is not present in the audio path.
- Add an Options control for disabling all visualization animation. When disabled, retain a static display and stop unnecessary UI repaint work.
- Add selectable themes. Theme selection is UI-only, persists between launches, and does not alter presets or sound parameters.

## 4. Gator / rhythmic gate

- The modern gate view displays the playhead synchronized to the active pattern and host/free-running clock. Draw gate segments with editable points, click-to-add, right-click-to-remove, and keyboard-editable step values.
- Show a frequency-energy color layer behind the gates, derived from the actual mid signal. Default is off. The gate processes mid only; the centered sub remains sample-identical.
- Controls include enable, tempo sync/free mode, rate/division, gate count, step level, swing, attack/release smoothing, retrigger/phase, depth, and modulation.

## 5. Texture and industrial effect modules

Every module has enable/bypass, level, wet/dry/mix, modulation, and pan when appropriate. Modulation is bounded, deterministic under a fixed state/clock, and can be disabled at zero depth.

### Noise

- Noise color: white, pink, and brown; high/low-pass filter and cutoff; attack/hold/release envelope; key tracking; stereo width; slow motion of tone/filter; level, wet, dry, mix, and pan.
- Noise remains above or behind the bass body as selected; Growl=0 mutes mid/texture paths.

### Metal

- Resonance, damping, inharmonicity, strike position, attack/release, velocity response, key tracking, pitch envelope, stereo spread, and a metal-decay control.

### New sections

- **Crackle / Static:** crackle size/density, tone, randomness, envelope amount, impact/pick attack, transient bias, wet/dry/mix/pan and modulation.
- **Impact / Knock:** resonator pitch, decay, brightness, velocity response, pitch tracking, stereo amount, wet/dry/mix/pan and modulation.
- **Formant / Vowel:** vowel selection/morph, formant shift/width, resonance, envelope and LFO movement, key tracking, and wet/dry/mix/pan.
- **Lo-Fi / Digital grit:** bit depth, sample-rate reduction, jitter, filter, noise, mix, and modulation.
- **Ring Mod / Sidebands:** carrier frequency/tracking, carrier waveform, sideband amount, frequency ratio, feedback, modulation, wet/dry/mix/pan.
- **Comb / Feedback Grinding:** delay time or note division, feedback, damping, pitch tracking, stereo amount, and safety-limited feedback.
- **Digital Shred:** bit depth, sample-rate reduction, jitter, pre/post filter, transient preservation, and modulation.
- **Rhythmic Gate:** clock-linked pattern, division, swing, depth, smoothing, phase/retrigger and modulation.
- **Transient Impact:** attack/decay, pitch, brightness, velocity response and mix.
- **Granular Shred:** grain size/density, pitch scatter, position, jitter, reverse chance and mix. Grain storage is fixed-capacity and preallocated.

### Formant Furnace and Harmonic Fracture

- Formant Furnace controls: vowel, formant shift, vowel morph, formant width, formant drive, envelope attack/release, pitch-envelope amount, envelope speed, harmonic fracture amount, and per-effect wet/dry/mix/pan.
- Harmonic Fracture controls: crossover, independent low/mid/high drive, band tilt, fold symmetry, fracture bias, transient response, envelope drive, and per-band blend. Add band solo/listen controls for low, mid, high, and all.

## 6. Time stretch and pitch variation

- Provide an editable time/pitch region over a note or pattern, including arbitrary start/end positions and snap options. Support stretch/compress ratio, a multiplier control, pitch range, direction, and independent time and pitch curves.
- Curve choices include linear, logarithmic/exponential, and user-drawn points. Time can stretch near a note tail or compress near its onset. Pitch can rise/fall independently, including an aggressive logarithmic sigh/whine shape.
- The audio path must use a declared real-time-safe algorithm with bounded latency, CPU and memory. If a requested operation cannot meet those limits, render it offline and report latency/render status clearly. Never block the audio thread.
- The preview waveform is generated by the same transform and parameter snapshot used for audio; edits update the preview without consuming audio-thread resources.

## 7. Output section

- Output controls: trim, limiter enable/ceiling/release/character/lookahead, stereo link, stereo width, mono-below frequency, DC block, optional soft clip, and peak/RMS/limiter gain-reduction meters.
- Include a basic compressor that can target the master output or selected effect buses. Its detector, attack/release, ratio, threshold, knee, makeup, sidechain and mix are documented and bounded.
- Preserve the centered-sub invariant. Output trim must be labeled by signal-flow position (pre-limiter or post-limiter).

## 8. State, automation, and release acceptance

- New parameter IDs are versioned and migrations preserve existing Snaral presets/projects. Validate corrupted, unknown, non-finite and out-of-range values before applying state.
- Audio-thread work is allocation-free and lock-free; all file parsing, waveform preview rendering, preset work and UI persistence happen off the audio thread.
- Acceptance requires test coverage for note-on/off and panic, Space audition, QWERTY/virtual keys, MIDI file import/export round-trip, outside-click macro window dismissal, macro reset/state round-trip, animation off, theme persistence, gate timing/sub invariance, time/pitch preview/audio agreement, and effect parameter stress across supported sample rates/block sizes.
- Do not call the product complete or publish a production release until the full feature acceptance matrix, builds, audio checks, UI checks, and supported-host checks pass. A clearly labeled development build may be published only with a truthful scope note and passing artifact checks.
