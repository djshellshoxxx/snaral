# Dark DnB Bass Synth — Three-Iteration Design Brief

**Repository:** djshellshoxxx/snaral  
**Status:** Design brief for a future specification pass. No synth code or developer specifications are included here.  
**Reference direction:** Broad mood and sound-design goals associated with dark, aggressive drum and bass, including Dieselboy and related music. This brief does not attempt to recreate any specific track, preset, sample, or artist signature.

## Iteration 1 — Instrument and workflow

### Concept summary

Snaral is a focused bass instrument for drum-and-bass producers who want to make forceful, moving bass sounds quickly, then refine them without needing to build a complex modular patch. It pairs a dependable, centered low foundation with an independently shaped midrange voice. The player should be able to load a starting sound, play a bassline, and make it move from blunt weight through a growling or metallic peak and back.

The first screen should present a small set of musical controls with clear names: **Weight**, **Growl**, **Bite**, **Motion**, and **Space**. A sound should be usable with these alone. An Advanced view can expose oscillator blends, filter behavior, modulation rates and depths, distortion tone, and optional noise or metal layers. Avoid making users select a musical key to get a dark result; the timbre must come from synthesis, movement, and dynamics.

### Typical user workflow

1. Load a factory preset or initialize a patch, then play notes from a MIDI keyboard or the host piano roll.
2. Set **Weight** for the fundamental and **Growl** for the midrange character.
3. Raise **Motion** to make the note change over time; use **Bite** to add a sharper, raspier or more clangorous edge.
4. Adjust **Space** for the midrange width while keeping the low end centered.
5. Choose tempo-synced motion or free-running motion, then audition the phrase in context.
6. Use the Advanced view only when the broad controls do not reach the desired result.
7. Save the patch as a preset or store a named variation for repeatable recall.

### Observable sound targets

- **Mean / aggressive:** Strong attack and audible harmonic density; the bass remains forceful at normal mix levels without relying only on extreme loudness.
- **Dark:** Weight and tension come from the spectral balance, envelope, and movement. No special key or permanently muffled tone is required.
- **Growling / snarling:** A pitched midrange voice with changing vowel-like or throat-like emphasis, rasp, and a controlled return toward its lower body.
- **Metallic / mechanical:** Inharmonic or sharply resonant upper components that sound like a designed machine or struck metal, kept optional and bounded.
- **Readable in a mix:** The note's fundamental and rhythmic shape remain identifiable; the sub does not wander in stereo, and midrange distortion does not bury note changes.

### Design principles

1. Separate low-frequency weight from midrange character so either can be changed without destabilizing the other.
2. Make the first useful sound reachable from a few performance-oriented controls; put specialist detail behind Advanced.
3. Use movement with a clear beginning, peak, and return, so a bass note can tell a short sonic story.
4. Preserve note identity and headroom while adding aggression.
5. Make settings and variations recallable and repeatable.
6. Keep optional layers optional; a useful bass must not depend on noise or a metallic resonator.

**Iteration 1 record:** Established a two-layer bass instrument and a macro-first workflow. Defined target adjectives as audible, mix-relevant qualities rather than vague mood labels. Identified split sub/mid control as essential to preserve pitch and low-end clarity.

## Iteration 2 — Sound engine and control model

### Preferred architecture

**MIDI note and host timing → voice and pitch handling → parallel sub and mid layers → layer-specific processing → level and stereo management → output protection**

The sub and mid layers follow the same played note, but use independent generation and processing. They recombine only after the sub has been kept mono and protected from the mid layer's modulation and distortion.

| Block | Purpose and user-facing control | Status |
|---|---|---|
| MIDI voice and pitch | Track MIDI note, velocity, note-on/off, and pitch bend. Offer a practical monophonic bass mode and a polyphonic option; keep glide a deliberate performance setting. | Essential |
| Sub oscillator | A dedicated sine-first oscillator, with a restrained triangle option for extra audibility on small speakers. **Weight** adjusts level; optional tuning control is precise but not exposed in the main view. | Essential |
| Mid oscillator pair | Two harmonically rich oscillator/wavetable sources blended to create the body of the growl. Wavetable position or oscillator blend provides changing spectra; keep the initial set curated and varied rather than relying on one table. **Growl** is the main level/character macro. | Essential |
| Mid filter | A resonant low-pass plus a band-pass or vowel-like emphasis stage shapes the mid voice. Filter envelope gives each note a moving contour. **Bite** increases emphasis and edge within a useful range. | Essential |
| Modulation | One tempo-syncable LFO and a note-triggered contour shape filter position and selected mid timbre. An optional, restrained oscillator phase/frequency modulation source adds roughness. **Motion** controls modulation depth; choose sync division or free rate in Advanced. | Essential core movement; extra modulation source optional |
| Mid distortion | A tone-shaped saturation stage adds harmonics. Use a small number of distinct drive characters, with level compensation, rather than one fixed extreme distortion sound. **Bite** can influence drive, while Advanced exposes drive and tone separately. | Essential |
| Noise / metal layer | A quiet noise source adds breath or grit. A bounded resonant or inharmonic layer adds clang and machinery. Give these independent levels and simple envelopes. | Optional |
| Width and output | Stereo width applies to the midrange only. Sum or constrain the low band to mono. Provide output trim and gentle peak containment so level changes do not masquerade as better tone. | Essential width handling and output trim; advanced protection details optional |

### Movement through a note

A note begins with a stable, pitch-clear low foundation and a relatively restrained mid tone. A note-triggered envelope opens the mid filter and moves the wavetable or blend toward a more nasal, raspy region. The LFO adds a repeating or free-moving sweep to sustain. A short resonant emphasis or optional metal layer can mark the peak. As the envelope falls, the filter and spectral blend return toward a less abrasive tone, allowing the fundamental and bassline to read again.

The exact contour should remain adjustable. The primary workflow should offer a clear **Motion** control and a small number of contour choices; the Advanced view can expose attack, decay, sustain, release, rate, phase/retrigger behavior, and modulation depth. A user should not have to learn a large modulation matrix to make one note evolve.

### Musical and recall behavior

- **Pitch:** Every pitched oscillator follows MIDI note pitch. Keep the sub's waveform and tuning stable across the keyboard; document any deliberate pitch tracking limits if they are needed for sub performance.
- **MIDI:** Support note input, velocity, pitch bend, and host MIDI automation where the plug-in format permits. Velocity can affect level or attack as a controllable assignment, not an unexplained fixed behavior.
- **Tempo:** Provide host-synced LFO divisions and free-running rate. State clearly when sync is unavailable because the host has not supplied tempo.
- **Automation:** Expose the main macros and meaningful advanced parameters to the DAW with stable parameter identities and usable names.
- **Presets:** Include at least ten varied factory presets that demonstrate weight, growl, movement, and metallic options. Allow user save, recall, import, and export. Use a simple documented JSON representation, optionally compressed, with versioning for future compatibility.
- **Variations:** Let users store a small set of named snapshots and recall them exactly. If variation generation is included, use a visible seed and save the resulting parameter state; the same seed and source patch should reproduce the same variation. Do not make random generation necessary for the core workflow.

### Why this architecture fits

A dedicated clean sub and an independently processed mid voice solve the central sound-quality tension: the user can make the upper sound violent while the low note remains solid and centered. A pair of morphable mid sources, filter contour, modulation, and tone-shaped distortion provide several routes to aggressive timbres. The optional noise and resonant layer add texture without making the instrument depend on sampled or artist-specific material. The macro layer keeps this expressive engine approachable.

**Iteration 2 record:** Chose a parallel sub/mid architecture with independent processing, one primary movement system, and optional texture layers. Made sync, free movement, MIDI, automation, preset recall, and exact variation recall part of the planned behavior. Limited first-version modulation to controls that support a clear audible task.

## Iteration 3 — Challenge and refinement

### Risks found and resolutions

- **Too many technical controls could overwhelm new users.** Keep five sound-shaping macros visible and move detailed rates, envelopes, oscillator tuning, and modulation depth into Advanced. Add useful starting presets.
- **The sub could become unstable or distorted along with the mid.** Keep it on its own path, centered, level-controlled, and outside midrange modulation and distortion. Do not let the Space control widen it.
- **The mid layer could mask pitch or become harsh without purpose.** Give its filter motion a defined note contour and return. Keep drive level-compensated and range-bounded; leave extreme feedback and uncontrolled resonator interaction out of the initial design.
- **The design could become dependent on one table, distortion, or key.** Use several complementary oscillator shapes and more than one useful drive character, and ensure the macros work across the keyboard. The factory presets should demonstrate contrasting approaches rather than minor variations of one patch.
- **Movement could fail across hosts.** Support both host-tempo sync and free rate; make retrigger behavior clear. Provide a sensible free-rate fallback if host tempo is missing.
- **A seed alone may not guarantee durable recall if algorithms change.** Save the generated parameter state alongside the seed and source preset/version metadata.
- **A broad modulation matrix, large effect rack, complex resonator bank, and deep randomizer would delay a usable first version.** Defer them. Keep one clear movement path, one distortion block, and at most a simple optional metal layer in the initial design.

### Remaining assumptions

- The first version is a DAW instrument plug-in with MIDI input; standalone support can be decided in the later product specification.
- The sub is intended for bassline registers, while upper notes should still track predictably. The precise range and any sub-level compensation need listening tests.
- Mono/legato behavior and glide are useful for bass performance but should be optional and explicit.
- “Metallic” should be achievable through synthesis; no real-world samples are required.
- The proposed factory preset minimum follows the user's shared audio-plugin standard and should be confirmed against the repository's current product requirements when implementation planning begins.

**Iteration 3 record:** Simplified the first version around reliable sub separation, understandable macros, and controlled midrange movement. Deferred complexity that does not strengthen the first useful workflow. Added an explicit recall strategy that stores parameter values as well as any generation seed.

## Final handoff brief

### 1. Instrument concept

Snaral is a macro-driven, two-layer bass instrument for dark drum and bass. It gives producers a stable, centered sub and a separately shaped aggressive midrange that can evolve through a note, then return without losing the bassline's pitch identity.

### 2. Sound targets

Create bass tones that are weighty, guttural, snarling, nasal, rasping, serrated, metallic, and mechanical. A typical note moves from a blunt, readable low body into an abrasive or clangorous midrange peak and settles back toward its fundamental. “Dark” comes from timbre, rhythm, dynamics, and contrast rather than a fixed key or static distortion preset.

### 3. Typical workflow

1. Load a preset or initialize a patch and play a MIDI phrase.
2. Set **Weight** and **Growl** to balance sub and mid.
3. Shape the attack and edge with **Bite**.
4. Set **Motion** and select synced or free movement.
5. Add **Space** to the midrange if useful; retain a centered sub.
6. Audition in the mix, refine in Advanced if needed, and save a named preset or exact variation.

### 4. Preferred engine architecture

MIDI and host timing feed a pitched voice. The voice drives parallel layers: a dedicated sine-first sub oscillator, and a mid voice built from two blendable, harmonically rich sources. The mid voice passes through a resonant filter with a note envelope, a synced/free LFO, and tone-shaped distortion. Optional noise and bounded metallic resonance add texture. The sub bypasses mid modulation and distortion, stays mono, and has independent level. Mid-only width, output trim, and gentle peak control finish the signal.

### 5. Core controls

| Control | User purpose | Status |
|---|---|---|
| Weight | Set the clean low foundation | Essential |
| Growl | Set midrange level and core character | Essential |
| Bite | Add nasal emphasis, rasp, and controlled drive | Essential |
| Motion | Set the amount of evolving movement | Essential |
| Space | Adjust midrange stereo width | Essential |
| Sync / Free | Choose tempo-locked or free movement | Essential |
| Sub waveform / fine tune | Adjust sub harmonics and tuning precisely | Optional / Advanced |
| Oscillator blend / wavetable position | Refine midrange tone | Essential capability / Advanced control |
| Filter contour and modulation depth | Refine how a note evolves | Essential capability / Advanced control |
| Drive type, drive, and tone | Refine distortion character | Essential capability / Advanced control |
| Noise level and metal resonance | Add breath, grit, or clang | Optional |
| Mono/legato and glide | Shape bassline note transitions | Optional |
| Named variation slots / deterministic variation seed | Recall exact alternatives | Essential recall behavior; generated variations optional |
| Output trim and peak protection | Control final level and peaks | Essential |

### 6. Musical and host behavior

Track MIDI pitch accurately across the intended range, with note-on/off, velocity, and pitch bend. Provide clear mono/legato and optional glide behavior. Offer tempo-synced LFO divisions and a free-running rate, with a defined fallback when host tempo is unavailable. Make macros and meaningful advanced parameters automatable in the DAW with stable identities. Supply at least ten distinct factory presets plus user save, recall, import, and export using a documented, versioned JSON format, optionally compressed. Store exact named variations; for generated variants, preserve parameter state as well as seed and source metadata.

### 7. Sound-quality principles

Keep the sub on an independent, centered path with no accidental stereo widening, midrange modulation, or mid distortion. Avoid excess low-frequency buildup and sudden level jumps. Let the mid layer become abrasive while preserving clear pitch movement and note rhythm. Use level compensation around distortion so increased loudness is not mistaken for improved tone. Test both solo and in a dense drum-and-bass mix, across bassline notes and host buffer/rate conditions in the later test plan.

### 8. Scope boundaries

**Initial design:** Parallel sub and mid layers; MIDI pitch tracking; five macros; two mid sources; filter and note contour; one LFO with sync/free modes; tone-shaped distortion; centered sub; mid-only width; output trim; DAW automation; factory and user presets; exact named variation recall; optional bounded noise/metal texture.

**Defer:** Large modulation matrix, multiple effect racks, deep randomization system, uncontrolled feedback, large resonator banks, sample playback, artist/track recreations, and nonessential visual complexity.

### 9. Open assumptions

- Confirm plug-in formats and whether standalone operation belongs in the first product scope.
- Define tested pitch range and acceptable sub behavior across that range.
- Decide the number of named variation slots and final mono/legato/glide options.
- Choose the JSON compression and migration policy during specification work.
- Verify the shared audio-plugin preset standard and repository conventions before implementation.
- Tune macro ranges and preset balance through listening tests; these are design targets, not measured results.

### 10. Handoff instruction

Turn this brief into initial developer-ready design specifications for Snaral. Include user stories, signal and parameter behavior, UI organization, preset and variation data requirements, host/MIDI behavior, implementation boundaries, and measurable acceptance criteria with testable behavior. Preserve the clean independent sub and macro-first workflow. Resolve the listed assumptions explicitly, and do not claim implementation or test results until code exists and those tests have been run.
