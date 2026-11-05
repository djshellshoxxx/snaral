# User Guide

## What Snaral is

A MIDI instrument. Play it from a keyboard or a sequencer. It has eight voices, so you can play chords or overlapping notes.

## Current sound

- The sound source is a sine oscillator per voice.
- **Weight** sets the level of the sine voice. It is the only macro that changes the sound right now.
- Output is stereo (the bus layout is stereo).

## Macros

Five host-automatable controls, each 0 to 100. Defaults are in brackets.

| Macro | Default | Status |
|---|---|---|
| Weight | 75 | works: sets the sine voice level |
| Growl | 65 | defined, no DSP yet |
| Bite | 20 | defined, no DSP yet |
| Motion | 0 | defined, no DSP yet |
| Space | 0 | defined, no DSP yet |

Macro values are saved with the project and can be automated from the host.

## Planned (from the specs)

The specs in `docs/specs/` describe the intended behaviour: a stable sub layer, a shaped mid voice, MIDI expression, a preset browser, oversampling, undo and A/B, and several effects. None of these is in the current build. Check `docs/SNARAL_FEATURE_SPEC_INDEX.md` for status before you rely on any of them.

## Install

The build produces VST3 and Standalone targets. Copy the built VST3 to your host's plug-in folder and rescan. Build steps are in [[Developer-Build-and-Test]].

## Limits

- Prototype. Not production ready.
- Only Weight is audible as a macro.
- Presets and factory sounds are specified but not yet present in the build.
