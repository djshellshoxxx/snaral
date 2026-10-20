# Snaral — Initial Developer Specification

**Repository:** `djshellshoxxx/snaral`  
**Document status:** Initial implementation specification  
**Source:** `docs/DARK_DNB_BASS_SYNTH_DESIGN_BRIEF.md`  
**Implementation status:** Bootstrap code exists on `codex/initial-synth-foundation`; it is an early development slice, not a release-ready implementation.

## 1. Product definition

Snaral is a MIDI-playable bass synthesizer for drum and bass. Its defining sound-engine requirement is a clean, centered low-frequency foundation running in parallel with a separately shaped midrange voice. The mid voice can move from a restrained body through a resonant, distorted peak and return while the sub remains stable.

The first-use path is macro-first: users can get a useful sound with **Weight**, **Growl**, **Bite**, **Motion**, and **Space**. Detailed oscillator, filter, envelope, modulation, and drive settings belong in an Advanced view.

The sound direction is broad and genre-oriented. Factory content and feature names must not claim to reproduce a particular artist, track, preset, or recording.

### Product goals

1. Keep pitch and rhythmic identity readable while the midrange becomes aggressive.
2. Make note evolution understandable and controllable without a modulation matrix.
3. Provide repeatable DAW automation, preset recall, and named variations.
4. Keep the first release small enough to validate sound quality and host behavior before adding secondary systems.

### Non-goals for the first implementation

- Sample playback or sample import.
- A general-purpose modular modulation matrix.
- An effect rack, uncontrolled feedback, or a large resonator bank.
- Automatic composition, phrase generation, or a deep randomizer.
- A product claim that the sound matches any named artist.
- A standalone preset browser service or cloud account.

## 2. Decisions resolving the design brief assumptions

| Open question | Decision for the initial implementation |
|---|---|
| Plug-in formats | Build a JUCE instrument as VST3 first. Provide a standalone target for development, sound audition, and MIDI-device testing. AU and CLAP are a follow-on format milestone after the engine and state format stabilize. |
| MIDI and voice mode | Default to monophonic mode. Include optional legato and glide. Provide a polyphonic mode capped at 8 voices for chords and host testing. |
| Pitch range | Guarantee correct MIDI tracking from note 24 through 84. Test the wider range 12 through 96 for predictable operation; sub audibility is only a design target below note 60, not a promise of equal perceived level over all notes. |
| Pitch bend | Default range is ±2 semitones, configurable from ±1 to ±12 semitones. Both sub and mid follow the same bend. |
| Variation slots | Provide 4 named A–D snapshots per instance. They are exact parameter snapshots, not generated variations. |
| Preset representation | Use readable, versioned JSON as the canonical interchange format. Compressed JSON may be added later without changing the JSON schema. |
| Preset count | Ship at least 10 distinct factory presets before a release candidate. |
| Audio framework | Use JUCE and C++20 unless repository-level build constraints discovered during implementation require an explicit documented change. Keep DSP, parameter/state, UI, and host integration in separate modules. |
| Peak protection | Use a bounded final safety limiter with a bypass/enable parameter and output trim. It must not be marketed as transparent at arbitrary levels; ordinary operating levels should not depend on limiting. |
| Randomness | No random generator is required for the initial implementation. If variation generation is added later, store generated parameter values as well as seed and source/version metadata. |

These are design decisions for the first implementation, not claims that the repository already uses these technologies or builds these formats.

## 3. User stories

- As a producer, I can load an initialized patch or preset, play MIDI notes, and shape a bass sound with five clearly named controls.
- As a producer, I can raise the midrange aggression without sending the sub through mid distortion or stereo widening.
- As a producer, I can make a note evolve with a one-shot contour and optional repeating movement, then hear it settle back.
- As a producer, I can choose host-synced or free-rate movement and know when host tempo is unavailable.
- As a producer, I can automate the five macros and selected Advanced controls with stable DAW parameter identities.
- As a producer, I can save and reopen a preset and get the same parameter values.
- As a producer, I can store and recall four named variations in the project instance.
- As a developer, I can test the DSP modules without launching the UI or a DAW.
- As a developer, I can verify state migration and parameter-ID stability using fixture presets.

## 4. Signal flow and module boundaries

Required signal flow:

`MIDI + host timing → voice/pitch → parallel sub and mid sources → independent layer processing → mid-only stereo width → sum → output trim → safety limiter → output`

### Engine modules

1. **Voice and MIDI**
   - Receives note-on, note-off, velocity, pitch bend, and all-notes-off.
   - Owns mono/legato/glide or polyphonic voice allocation.
   - Provides the same smoothed pitch trajectory to the sub and mid sources.
2. **Sub layer**
   - Sine oscillator by default; triangle is an Advanced option.
   - Own level envelope and Weight gain.
   - Does not pass through mid filter, mid distortion, noise/metal layer, or Space width.
   - Is centered at every output stage.
3. **Mid source**
   - Two pitched sources, Source A and Source B, using a curated initial oscillator/wavetable set.
   - Blend and table position are available in Advanced.
   - Any table data must be synthesized or properly licensed and included in the repository with its source/license record.
4. **Mid contour and modulation**
   - Note-triggered contour shapes filter cutoff and one selected spectral target.
   - One LFO is available in free or host-synced mode.
   - No user-configurable routing matrix in the initial implementation.
5. **Mid processing**
   - Resonant low-pass and one band-pass/vowel emphasis stage, with a bounded routing order documented in the UI.
   - Tone-shaped saturation with at least two useful drive characters.
   - Optional synthesized noise and one bounded metallic resonator.
6. **Stereo and output**
   - Space affects the mid layer only.
   - Output trim precedes the safety limiter.
   - Internal buffers must not clip due to parameter changes; final limiter behavior is independently testable.

Every module must have a narrow interface, independent unit tests where practical, and no UI dependencies in the audio callback. Audio processing must allocate no memory, perform no file I/O, and take no locks on the real-time thread.

## 5. Parameter specification

All continuous parameters use normalized host values from 0.0 to 1.0 unless a unit is shown. IDs are permanent after release; labels may be localized without changing IDs. Parameter smoothing is required for audible continuous controls. Discrete choices must switch without invalid enum values or unsafe callback work.

### Main macros

| Display name | Stable ID | Range | Required behavior |
|---|---|---:|---|
| Weight | `weight` | 0–100% | Controls sub level using a perceptually useful curve. At 0%, sub is silent; at 100%, it reaches the preset-defined nominal maximum. It must not change mid processing. |
| Growl | `growl` | 0–100% | Controls the mid layer’s nominal level and a bounded character blend from body toward the preset’s more harmonically rich source. At 0%, mid is silent. It must not alter sub level. |
| Bite | `bite` | 0–100% | Coordinates a bounded increase in filter emphasis and drive. At 0%, it adds no macro emphasis/drive; at 100%, it must not create runaway resonance or unbounded gain. Advanced controls can offset its destinations. |
| Motion | `motion` | 0–100% | Scales the note contour and LFO depth. At 0%, modulation depth is zero. At 100%, modulation remains within configured destination limits. It does not change oscillator pitch. |
| Space | `space` | 0–100% | Controls mid-only stereo width from mono at 0% to the maximum validated width at 100%. The sub output must remain identical in left and right channels. |

Initialized macro defaults for the first implementation are Weight 0.75, Growl 0.65, Bite 0.20, Motion 0.00, and Space 0.00. These are product defaults rather than factory-preset values; they provide an audible sub/mid patch with modulation and width initially disabled. Revisit after the first listening pass, before release parameter IDs or preset schema are declared stable.

Macros are source parameters with deterministic DSP mappings. The mappings must be implemented as explicit functions or tables with tests, not scattered UI-side calculations. The displayed macro value must correspond to the host-automated value. Presets may set macro and Advanced parameters together; Advanced offset controls must make this interaction predictable.

### Advanced controls

Minimum controls and IDs:

| Group | Parameter IDs | Initial range / choices |
|---|---|---|
| Voice | `voice_mode`, `legato`, `glide_ms`, `pitch_bend_range`, `velocity_to_level`, `velocity_to_attack` | Mono / Poly (max 8); glide 0–500 ms; bend 1–12 semitones; velocity destinations 0–100% |
| Sub | `sub_waveform`, `sub_tune_cents`, `sub_level`, `sub_attack_ms`, `sub_release_ms` | Sine / Triangle; tune −50 to +50 cents; level 0–100%; attack 0–100 ms; release 5–2000 ms |
| Mid sources | `mid_source_a`, `mid_source_b`, `mid_blend`, `mid_table_position`, `mid_tune_cents` | At least 4 curated source choices; blend/position 0–100%; tune −50 to +50 cents |
| Filter/contour | `filter_cutoff_hz`, `filter_resonance`, `filter_type`, `contour_attack_ms`, `contour_decay_ms`, `contour_sustain`, `contour_release_ms`, `contour_shape`, `contour_depth` | Cutoff 20 Hz–20 kHz with suitable logarithmic scaling; resonance 0–80% of validated stable range; LP/BP emphasis; attack 0–500 ms, decay 5–3000 ms, sustain 0–100%, release 5–5000 ms; 3 contour shapes |
| LFO | `lfo_mode`, `lfo_rate_hz`, `lfo_sync_division`, `lfo_waveform`, `lfo_retrigger`, `lfo_phase`, `lfo_depth` | Free / Sync; 0.02–20 Hz; divisions 1/1 through 1/32 including dotted/triplet selections; sine/triangle/saw/pulse; retrigger on/off; phase 0–360°; depth 0–100% |
| Drive | `drive_type`, `drive_amount`, `drive_tone`, `drive_mix` | At least 2 bounded characters; amount 0–100%; tone 0–100%; mix 0–100% |
| Texture | `noise_level`, `noise_tone`, `metal_level`, `metal_decay_ms`, `metal_tune` | Noise and metal default off; levels 0–100%; decay 10–2000 ms; tune follows MIDI with bounded inharmonic offsets |
| Output | `output_trim_db`, `limiter_enabled`, `limiter_ceiling_db` | Trim −24 to +12 dB; limiter on/off; ceiling −12 to 0 dBFS |

Ranges may be narrowed after listening tests, but IDs and units must remain consistent and any range change after a public preset release requires a state-compatibility review. Numeric range endpoints are UI/parameter limits, not claims of a particular tone.

### Macro interaction rules

- Advanced edits update the underlying parameters and immediately affect sound.
- When an Advanced parameter changes a macro destination, the macro remains automatable and its destination offset is retained in state.
- If macro mapping offsets are too complex for the first UI, use a simpler rule: macros directly set their mapped parameter bundle and Advanced values show the resulting values. In either approach, document the rule and test that preset load and DAW automation do not produce a jump.
- Loading a preset restores all parameter values atomically at the audio-block boundary. No partial state may be heard.
- Parameter updates are smoothed at the DSP layer. Suggested minimum smoothing: 5–20 ms for gain and continuous timbre controls; use a musically suitable glide/envelope rather than generic smoothing for pitch.

## 6. Musical, MIDI, and host behavior

### Voice behavior

- Mono mode is the default. A new note replaces the active pitch; note-off releases the active voice.
- Legato off retriggers the note contour on each note-on. Legato on holds the contour through overlapping notes and applies glide only when enabled.
- Glide is 0–500 ms; 0 ms disables glide. Glide applies consistently to both layers.
- Poly mode supports up to 8 simultaneous voices. Voice stealing policy is oldest released voice, then oldest active voice. The policy is deterministic and documented.
- Velocity is normalized from MIDI 1–127 and has no effect by default unless a preset or user assignment enables level or attack response.
- Pitch bend is applied to all active voices and both layers using the configured range.
- All Notes Off and transport stop must release or silence voices safely.

### Host timing and automation

- Read host tempo and play state when available. In Sync mode, the LFO follows tempo and selected division.
- If tempo is absent, invalid, or unavailable, hold the LFO in Free mode at its last valid free rate and show a clear “Host tempo unavailable — using free rate” status.
- Transport start/stop and loop changes must not create a discontinuity or stuck note. LFO retrigger behavior follows the explicit `lfo_retrigger` parameter.
- All main macros and meaningful Advanced controls are host automatable. Parameter IDs remain stable across UI labels and presets.
- The plug-in must report itself as a MIDI effect/instrument that accepts MIDI and produces audio in supported hosts.

## 7. UI specification

### Main view

- Show preset name and preset navigation, initialize/reset, save, and a clear modified-state indicator.
- Keep the five macros visible at the default window size.
- Include a compact Sync/Free selector, selected sync division or free rate, voice mode, and four A–D variation controls.
- Provide a clear output trim/meter region. Do not imply that a loudness meter is a sound-quality score.
- Default size target: 900 × 560 logical pixels. Minimum supported size: 720 × 480. Resizing adds workspace; it must not scale control hit targets or text below readable minimums.
- UI must remain usable with high-DPI scaling and keyboard focus. Controls need text labels, value readouts, and accessible names/tooltips.

### Advanced view

Use tabs or clearly separated collapsible sections: **Voice**, **Sub**, **Sources**, **Filter & Motion**, **Drive**, **Texture**, and **Output**. Content outside the viewport must be reachable by scrolling. Advanced controls must not replace or hide the five main macros.

### Interaction behavior

- Every knob/slider supports mouse drag, click-to-edit numeric entry where practical, and host automation.
- Preset save/import/export and destructive replacement actions require an explicit user action and report success or validation errors in plain language.
- No preset or variation action may block the audio callback.
- Preset navigation does not silently overwrite user data. Unsaved changes are indicated before replace.
- UI refreshes parameter values without creating feedback loops or modifying host automation.

## 8. Presets and variation state

### Factory and user presets

Ship at least 10 factory presets, each with a distinct purpose and audible contrast: clean sub/bass body, restrained growl, moving vowel, rasp/drive, metallic peak, rhythmic synced movement, free movement, mid-only width, mono/legato bass, and a hybrid patch. Names should describe sound or use, not make artist/track claims.

Every preset must be tested at MIDI notes 24, 36, 48, and 60, with velocity 64 and 127. Record level-matched audio review notes during sound-design validation; do not treat these notes as automated measurement results.

Users can save, recall, import, and export presets. Import validates the whole file before changing current state. Invalid, unsupported, or partial files leave the current patch unchanged and report the reason.

### JSON schema, version 1

Canonical human-readable UTF-8 JSON:

```json
{
  "format": "snaral-preset",
  "schema_version": 1,
  "plugin": { "name": "Snaral", "state_version": "0.1.0" },
  "preset": { "name": "Example Bass", "author": "", "description": "", "tags": [] },
  "parameters": {
    "weight": 0.72,
    "growl": 0.58,
    "bite": 0.44,
    "motion": 0.63,
    "space": 0.18,
    "voice_mode": "mono",
    "lfo_mode": "sync"
  }
}
```

The example is illustrative and not a factory preset. The actual schema must include all serialized parameters. Requirements:

- Reject unknown top-level format identifiers and unsupported future schema versions without modifying current state.
- Unknown parameter IDs are ignored with a warning for forward compatibility; known parameter values must be type-checked, range-checked, and clamped only when the schema explicitly allows it.
- Include a migration function per supported older schema version. Never infer user values from missing fields except via documented version defaults.
- Export is deterministic: stable key ordering and numeric formatting make diffs reviewable.
- Preset files contain no executable content, paths, or external asset references.
- Factory presets are version-controlled JSON fixtures with provenance and license notes for any embedded oscillator data.

### Named variations

- Four slots, A–D, store the complete parameter state and a user-editable name.
- Store variation state in the plug-in instance/project state. Switching slots is sample-safe and restores a complete state atomically.
- A slot may be empty. Saving to a slot is explicit and updates that slot only.
- Recall must restore all serialized parameters to their exact normalized values (tolerance ≤ 1e-6 in state round-trip tests).
- Variations are instance state and are not silently written to factory or user preset files.
- If later adding generated variations, extend schema with seed, source preset ID/version, algorithm version, and the full resulting parameter map.

## 9. Audio quality and real-time requirements

- Audio processing supports 44.1, 48, 88.2, 96, and 192 kHz sample rates and common host block sizes from 1 to 2048 samples. Block-size changes must not reset parameter state or create an audio spike.
- The DSP must safely handle mono input/no-input instrument configurations, stereo output, zero-length blocks if the host provides them, and transport discontinuities.
- No NaN, Inf, denormal runaway, out-of-bounds memory, unbounded feedback, or state-dependent random output is permitted.
- Sub-only output with Space at 0% and 100% must have equal left/right samples within −120 dBFS numerical tolerance. At 100% Space, the sub must remain centered while the mid may widen.
- Setting Growl to 0 must silence the mid/texture path; setting Weight to 0 must silence the sub path. These checks use isolated layer test renders.
- With limiter disabled and safe nominal control settings, the engine must not produce NaN/Inf or internal overflow. With limiter enabled, output peaks must remain at or below the configured ceiling within 0.1 dB for test signals inside the limiter’s specified operating envelope.
- Parameter sweeps and MIDI note transitions must not produce discontinuities exceeding the documented control smoothing behavior. Capture and inspect test renders for parameter steps and note changes.
- Measure CPU in the test harness on a documented reference machine. Initial target: under 10% of one modern desktop core for 8 voices at 48 kHz / 256 samples, excluding UI. Record hardware and compiler settings with the result; this is a target to validate, not an existing result.

## 10. Testable acceptance criteria

A release candidate is not accepted until the following pass:

### Functional

1. A fresh instance produces audio for MIDI notes 24–84 and stops after note-off/release.
2. All five macros change their specified layer or behavior; automated and UI edits produce the same parameter value and sound.
3. Weight affects only the sub path; Growl affects only the mid/texture path; Space does not affect the sub path.
4. Motion at 0 disables contour/LFO depth; increasing it changes the mid sound without changing played pitch.
5. Sync mode follows supplied host tempo; missing tempo falls back to free rate and reports status.
6. Mono, legato, glide, poly mode, pitch bend, velocity assignments, and all-notes-off follow the rules in section 6.
7. VST3 and standalone targets load and render audio on the documented build platforms. Host certification and additional plug-in formats are separate gates.
8. Main and Advanced parameters are host-automatable with stable IDs.

### State and presets

9. All 10 or more factory presets load and pass schema validation.
10. Saving and reopening a user preset restores every parameter; malformed and future-version files are rejected without changing the active patch.
11. Four variation slots round-trip all parameter values within 1e-6 and preserve their names.
12. A project save/reopen restores patch, variation slots, active variation, and parameter values.
13. Schema migration tests cover each supported prior version before that version is released.

### Audio and stability

14. Automated tests cover supported sample rates, block sizes 1, 64, 128, 256, 512, 1024, and 2048, including repeated prepare/release cycles.
15. Stress tests randomize all valid parameter endpoints and selected midpoints for at least 10,000 blocks; results contain no NaN/Inf, crash, stuck note, or invalid state.
16. MIDI fuzz tests include repeated note-on/off for the same pitch, overlapping notes, all-notes-off, sustain controller if supported, extreme pitch bend, and transport stop during a held note.
17. Isolated sub, mid, Space, limiter, and preset-load tests validate the invariants in section 9.
18. Audio renders are reviewed level-matched in solo and with a dense drum-and-bass reference mix. Review findings are recorded as observations, not numerical claims unless measured.
19. CPU target is measured and recorded; if missed, profile and document the cause before release.

### UI

20. At 720 × 480 and 900 × 560 logical pixels, all main controls remain visible and usable; Advanced content is scrollable.
21. Resizing the window does not enlarge control hit targets or cause clipping; high-DPI scaling preserves readable labels and value text.
22. Preset import errors, save success, modified state, and missing host tempo are visible and understandable.
23. Keyboard focus, tooltips/accessibility names, and host automation gestures work for all main controls.

Automated test results, manual listening notes, supported hosts/OS versions, and any waived criterion must be recorded in the release test report. A design specification alone does not satisfy these gates.

## 11. Revised implementation sequence

The [CDL shared audio-app baseline](SNARAL_SHARED_CDL_COMPLIANCE.md) is part of initial-release completion. The expansion roadmap contains only post-release features; baseline actions, onboarding, MIDI learn, A/B comparison, themes/options, visual diagnostics, and WAV export are core deliverables.

### Milestone 0 — Repository and build foundation
- Pin JUCE, verify applicable license/redistribution obligations, and establish CMake targets for VST3, standalone, and engine tests.
- Establish modular source layout and CI build/test jobs for supported operating systems.
- Produce a MIDI-playable sine instrument with finite output, note-off release, and a minimal development editor.
- Record exact developer setup and build commands.

### Milestone 1 — Parameter, voice, and performance foundation
- Implement stable parameter registry, atomic state snapshots, and serialization fixtures.
- Implement mono/poly voice management, note lifecycle, pitch bend, glide, velocity assignments, sustain pedal CC64, CC120/123, and Panic.
- Add global A4 tuning and semitone transpose.
- Add independent Sub and Mid ADSR amplitude envelopes; keep the mid timbre contour separate.
- Add unit tests for note ordering, sustain behavior, tuning, envelope timing, and state restore.

### Milestone 2 — Split-layer synthesis
- Implement sine/triangle sub and the four specified mid oscillator families.
- Add independent layer level, layer solo/mute, deterministic source blend, band-limited operation, and centered-sub summing.
- Add pitch, aliasing, layer-isolation, DC, and mono-fold-down tests.

### Milestone 3 — Movement and controlled aggression
- Implement filter modes, note contour, synced/free LFO, fixed modulation destinations, two bounded drive characters, optional noise and metallic texture.
- Implement normative macro maps and parameter smoothing.
- Validate host tempo fallback, phase/retrigger behavior, peak bounds, and sub isolation.

### Milestone 4 — Host and state behavior
- Complete VST3 and standalone host integration, automation IDs, transport handling, host save/restore, latency reporting, and state compatibility.
- Implement versioned preset JSON, validation, migration, user file safety, and four named variations.
- Pass host state round-trip and malformed-preset tests before starting factory content.

### Milestone 5 — CDL workflow and UI
- Implement Main and Advanced views, responsive/accessibility requirements, theme selection, Options, tooltips, and animation preferences.
- Include Stop/Panic, Reset, deterministic bounded Randomize, Bypass, parameter context actions, preset selection, and Export WAV entry point.
- Include MIDI CC Learn for macros/performance parameters, A/B compare, first-run help/onboarding, signal visualization, and a meaningful plug-in DSP CPU indicator.
- Keep full undo/redo history, preset search/favorites, variation morph, and exchange workflows in post-release expansion.

### Milestone 6 — Content, standalone utilities, and release verification
- Author/review at least ten distinct factory presets; implement user save/import/export.
- Implement standalone device setup and explicit 440 Hz reference tone. Device selection remains host-owned in plug-in mode.
- Implement offline WAV export for instrument phrases.
- Run functional, MIDI fuzz, parameter stress, audio, UI, performance, and host compatibility matrices. Record OS/host/build settings, CPU results, listening notes, WAV interoperability, and remaining defects.
- Produce VST3 and standalone release candidates only after all core and CDL baseline gates pass.

### Deferred expansion

Begin post-release expansion only after Milestone 6 gates pass. Use the [Staged Expansion Roadmap](SNARAL_EXPANSION_ROADMAP.md) for search/history beyond A/B, sound-design differentiation, optional effects, and AU/CLAP support. Performance Gesture Map is excluded by instruction and has no specification.

## 12. Traceability

This specification preserves the brief’s core requirements: split sub/mid architecture, macro-first workflow, one clear movement system, controlled distortion, optional texture, host synchronization, repeatable state, and deliberate deferral of broad modulation/effect systems. Where this document adds numeric ranges or platform choices, those values are explicit initial design decisions that must be validated during implementation. Any change to the centered-sub invariant, parameter IDs, JSON compatibility, or stated first-release scope requires a documented design revision.
