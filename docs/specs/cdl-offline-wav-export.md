# Feature Spec: Offline WAV Export

**Requirement source:** [Copied approved baseline](../../standards/AUDIO_APP_BASELINE.md)  
**Applies to:** Snaral standalone and plug-in editor  
**Depends on:** parameter-state, midi-voice, host-integration, expansion-oversampling  
**Implementation:** JUCE-free render driver and WAV sample-conversion in `Source/Core/`; file writing with JUCE `WavAudioFormat` (JUCE-dependent, tests in `tests/juce/`)

## Objective

Render the current patch and a MIDI phrase to a standard WAV file faster than or independent of real time, with exact, documented file properties.

## User flow

1. Export WAV (preset row) opens a dialog that first states: "Renders Snaral only. Host effects, host automation and host transport are not included."
2. Source: a MIDI file (.mid, SMF type 0 or 1) or the built-in test phrase.
3. Settings: tempo (BPM), sample rate, channels, bit depth, tail, optional normalise, destination file.
4. Render shows determinate progress (rendered samples / total, updated at most 10 Hz), elapsed time, and Cancel. Completion shows the path, duration, peak dBFS and count of clipped samples.

## File format (exact)

- Container: RIFF/WAVE, little endian. No RF64 (total length is capped well below 4 GB).
- Sample rates: 44100, 48000, 88200, 96000, 192000. Default: the current host or device rate if listed, else 48000.
- Channels: stereo (default) or mono. Mono is `(L + R) / 2`, which leaves the centered sub at unchanged level and cannot exceed the stereo peak.
- Sample formats (all `fmt ` tag values are plain, 16-byte `fmt `): **24-bit integer PCM** (tag 1, default); **32-bit IEEE float** (tag 3, with the `fact` chunk); **16-bit integer PCM** (tag 1) with seeded TPDF dither of 1 LSB peak (fixed seed so output is repeatable). No dither for 24-bit and float.
- Integer conversion: `round(x * 2^(b-1))` clamped to the representable range; clipped samples are counted and reported. Float output is written unclipped.
- Metadata: RIFF LIST/INFO with product ("Snaral" and version), preset name, creation date (UTC date only). No host, project, user or path information.

## Render contract

- A separate engine instance is prepared at the export sample rate with block size 512. It never shares voices, smoothing or LFO state with the live engine. The patch is a copy of the validated parameter snapshot taken when Render is pressed; later edits do not affect it.
- Audio is rendered natively at the chosen rate; no resampling. Oversampling and the limiter follow the patch. Initial latency (`engine.latencySamples()`) is trimmed from the start and the render is extended by the same amount so the tail is not shortened.
- Timing: one constant tempo drives the whole phrase (tempo changes inside a MIDI file are ignored and the dialog says so). Host timing passed to the engine is "playing, from position 0, at that tempo", so synced LFO and Trajectory work. The MIDI-learn map is applied to CC events in the file.
- Length: phrase end (time of the last event) plus tail. Tail 0 to 30 s, default 2.0 s, fixed (no early stop). At phrase end all held notes get note-off and the sustain pedal is released. Total length is limited to 600 s; larger requests are refused before rendering.
- Normalisation is off by default. If enabled it applies one linear gain after the render so the sample peak equals -1.0 dBFS; this needs the full render, so rendering always goes to a temporary 32-bit float file first and the final file is converted from it.
- Built-in phrase: eight quarter notes of a C natural minor scale from the start note (default 36, range 24 to 84), steps +0 +2 +3 +5 +7 +8 +10 +12 semitones, note length 0.9 beat, velocity 100 (editable 1 to 127), 174 BPM default. A step-entry phrase editor is deferred (recorded scope cut; the MIDI file path covers custom phrases).
- Determinism: identical patch, MIDI, settings and build give identical audio data (the date metadata differs).
- Threading: a worker thread renders; the audio callback, host parameters, voices, transport, variation slots and dirty state are untouched. One export at a time. Closing the editor or destroying the plug-in cancels the job and removes temporary files.

## File naming and destination

- Default name `snaral_<preset-slug>_<rate>_<bits>.wav` where the slug is the lower-cased preset name with every run of characters outside `a-z0-9` replaced by `-`, trimmed, at most 48 characters, `init` if empty; bits is `16`, `24` or `32f`. Example: `snaral_foundation_48000_24.wav`.
- Default folder: the last used export folder, else `Documents/Circuit Drift Labs/Snaral/Exports` (created on demand).
- If the file exists the user chooses Replace, Save with a numeric suffix (`_2`, `_3`), or Cancel. Nothing is overwritten silently.

## Atomic write and errors

- Check free space (estimate = 2 x final size) before starting. The final file is written to a temporary name in the destination folder, flushed, then renamed over the target.
- Cancel (honoured within one block, under 1 s), disk full, write error, unwritable destination, or any exception: delete temporaries, keep any existing destination file, show the reason.
- MIDI file rejected before rendering when: not SMF, SMPTE time division, over 4 MiB, no notes, longer than the limit, or unparsable. The message names the cause.

## Acceptance criteria

1. The file has the chosen rate, channels, tag (1 or 3), bit depth, length equal to `round((phraseEnd + tail) * rate)` samples, and tempo-derived note onsets within 1 sample.
2. A reference render driver output and the exported audio match within 1 LSB for 24-bit and exactly for 32-bit float, for a fixture patch and phrase (with oversampling off, 2x and 4x).
3. Export leaves live parameters, voices, transport, variation slots, A/B and dirty state unchanged (state hash before and after).
4. Cancel, disk-full simulation, invalid MIDI and overwrite-decline all leave an existing destination byte-identical and no temp files.
5. Stereo export has bit-identical sub-only L/R; mono export is finite and the peak never exceeds the stereo peak; with normalise on the peak is -1.0 dBFS within 0.01 dB.
6. The file opens and plays in a second application (record application and version), and its duration and sample rate display correctly there.
7. Standalone and every plug-in format expose the same dialog and behaviour where the host wrapper permits it.

## Test fixtures

`tests/WavExportTests.cpp` (conversion, dither determinism, tail and latency-trim arithmetic, naming slug, phrase generator, MIDI-file rejection set) and `tests/juce/WavWriteTests.cpp` (header fields, temp-file atomicity, cancel, overwrite). Fixtures: one valid and one malformed `.mid`, a 3-note phrase, and a clipping patch with the limiter off.
