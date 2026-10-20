# Feature Spec: Offline WAV Export

**Requirement source:** [Copied approved baseline](../../standards/AUDIO_APP_BASELINE.md)
**Applies to:** Snaral instrument standalone and plug-in

## Objective
Render a patch and supplied MIDI phrase to a shareable WAV file without requiring real-time recording of host playback.

## User flow
1. Open Export WAV from the common action row.
2. Choose a MIDI file or enter notes in a compact export phrase editor; if neither is supplied, offer a built-in one-octave test phrase.
3. Set tempo, note range/velocity as applicable, sample rate 44.1/48/88.2/96/192 kHz, mono/stereo, 16-bit or 24-bit PCM, and tail duration 0–30 seconds.
4. Choose destination and render. Show progress, Cancel, and a completion path.

## Render contract
- Use the current validated patch state and the same synthesis engine as real-time playback.
- Export excludes host effects, host automation, external plug-in chains, and live input; disclose this before render.
- Render off the audio callback on a worker or offline render path. It must not change active host parameters, notes, transport, variation slots, or dirty state.
- Stereo export retains centered sub. Mono export uses documented mono fold-down and peak protection.
- Write to a temporary file and atomically finalize. Cancel/failure removes temporary output and preserves any existing destination unless user confirms overwrite.
- Embed WAV metadata only for product, preset, sample rate, and render date; do not embed private host/project paths.

## Acceptance
1. Exported file has selected sample rate, channel count, PCM bit depth, tempo-derived note timing, and requested tail.
2. Offline render parameter and MIDI sequence results match reference render within documented floating-point tolerance.
3. Export leaves project state and host transport unchanged.
4. Cancel, disk-full, invalid MIDI, and overwrite cases preserve existing destination.
5. Mono and stereo exports pass peak, finite-sample, and centered-sub checks.
6. VST3 and standalone expose the same export behavior where the host wrapper permits it.
