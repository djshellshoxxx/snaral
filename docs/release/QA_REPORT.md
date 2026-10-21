# QA report (light QA, qa-plan.md)

Branch `feat/audit` (base `work/beta` `78e2a48`). Machine: 2-core Linux x86_64 container, GCC 13, no audio device, Xvfb for the GUI. Only things that were run are reported; everything else is marked UNVERIFIED.

## Summary

| Stage | Result |
|---|---|
| A. JUCE-free suite (`tools/build_tests.sh`) | PASS, 38 of 38 |
| A. JUCE tests (`tools/build_juce.sh`, Release, ctest) | PASS, 45 of 45 (the 38 above plus core, project state, UI kit, UI snapshots, real editor, real processor) |
| A. ASan + UBSan over the JUCE-free suite | PASS, 38 of 38 after one test-harness fix (below) |
| A. ThreadSanitizer (MIDI learn, CC hook, engine, parameter contract, voice allocator fuzz, undo fuzz) | PASS for the JUCE-free tests listed; JUCE tests UNVERIFIED |
| B. pluginval 1.x strictness 5 on the VST3 (Linux, headless, `--validate-in-process`) | PASS, exit 0, with and without `--skip-gui-tests` |
| C. Offline render of all 27 factory presets | PASS (table below) |
| C. Matrix: 27 presets x notes 24/36/48/60 x velocities 64/127 (216 renders) | PASS |
| Standalone 10 s under Xvfb | PASS (running and drawing after 14 s, killed by `timeout`; screenshot taken) |
| D. Host smoke (Windows, macOS, DAWs) | UNVERIFIED (no such machines; CI builds the cells) |
| E. Listening notes | UNVERIFIED (no audio device or listener) |
| AU (`auval`), CLAP validator, LV2 load test | UNVERIFIED (macOS only, validator not installed, no LV2 host) |

## Bugs found and fixed during QA

| ID | Severity | Bug | Fix |
|---|---|---|---|
| Q1 | S3 (test harness) | `tests/EngineOutputTestUtil.h` replaced `operator new` but not the nothrow form, so `std::stable_sort`'s temporary buffer produced an ASan alloc-dealloc mismatch in `SnaralEngineStressTests` | nothrow `operator new` / `new[]` replaced as well (counting allocations too) |
| Q2 | S2 | Scope Sub and Mid views were permanently disabled (no taps in the engine) | see AUDIT.md gap 1 |
| Q3 | S2 | The CPU overrun marker used the smoothed load, not a measured over-deadline call | see AUDIT.md gap 2 |
| Q4 | S3 | The status line missed "tempo unavailable" while Sync was requested, and had no overrun message | see AUDIT.md gap 3 |
| Q5 | S3 | Mono output layout accepted although host-integration requires stereo only | see AUDIT.md gap 4 |
| Q6 | S2 | Offline render / WAV export measured the engine latency before applying the patch, so presets with Oversampling x2 or x4 were trimmed by 50 samples instead of 96 or 106: the audio started 46 to 56 samples (about 1 ms) late and the file lost that much of its tail. Found by the new latency-trim check (onset off / x2 / x4 = 1 / 47 / 57 samples) | `OfflineRenderer::runLoop` applies the patch with `setParameters` before reading `latencySamples()`; onset is now 1 / 1 / 1 sample for all modes (`FactoryRenderQaTests`) |

Q6 was the only defect found by the new render checks themselves; it changes exported WAV timing, not live playback. No S1 found: no crash, NaN or Inf, stuck note, off-centre sub, state loss or allocation in the audio path in any run below.

## Stage A details

- `tools/build_tests.sh`: 38 of 38 passed (includes the new `snaral_wiringperturbationtests`, `snaral_factoryrenderqatests`, and the existing consistency check).
- `tools/build_juce.sh` (also builds VST3, CLAP, LV2, Standalone): 45 of 45 passed.
- Sanitizers: configured with `-O1 -fsanitize=address,undefined -fno-omit-frame-pointer`, `ASAN_OPTIONS=detect_leaks=0`. All JUCE-free tests passed, including the 76 s perturbation run and the stress and fuzz tests.

## Stage B: pluginval

```
pluginval --strictness-level 5 --skip-gui-tests --validate-in-process Snaral.vst3     -> SUCCESS, exit 0
pluginval --strictness-level 5 --validate-in-process Snaral.vst3 (under xvfb-run)      -> SUCCESS, exit 0
```

pluginval was the `latest` Linux release downloaded on 2026-10-21 (the log does not print a version string). Open cold and warm, plugin info, programs, audio processing at 44.1/48/96 kHz and block sizes 64 to 1024, state, automation, automatable parameters and bus tests ran. The `vst3 validator` sub-test was skipped by pluginval because no validator path was set, and `auval` does not apply on Linux. Strictness 10 was not run.

## Stage C: all factory presets, offline

`tests/FactoryRenderQaTests.cpp` (`tools/preset_qa.sh`) renders each preset through the real `SnaralEngine` with the Core `OfflineRenderer` (174 BPM, 48 kHz, no normalisation, 7-note bass phrase with an octave jump, a stab, a 127 velocity hit, an overlap and a note at 60, 1.5 s tail; limiter and oversampling as stored in the preset). Active RMS is measured over samples above -80 dBFS. Thresholds that make a preset fail: non-finite sample, peak above full scale, less than 5 % of the render non-silent, peak below -24 dBFS, absolute DC above 0.01, active RMS outside -30 to -3 dBFS, tail above -60 dBFS after 1.5 s, sub-solo render with L different from R.

| # | Preset | Peak dBFS | RMS dBFS (active) | Crest dB | DC | Tail (last 100 ms) |
|---|---|---|---|---|---|---|
| 1 | Foundation | -2.67 | -6.51 | 3.84 | 0.00069 | silent |
| 2 | Plain Weight | -3.39 | -7.12 | 3.73 | 0.00060 | silent |
| 3 | Throat Sweep | -3.54 | -8.27 | 4.74 | 0.00037 | silent |
| 4 | Saw Rasp | -2.91 | -6.60 | 3.69 | 0.00042 | silent |
| 5 | Folded Jaw | -2.13 | -8.04 | 5.91 | 0.00034 | silent |
| 6 | Machine Bell | -3.34 | -7.74 | 4.40 | 0.00047 | silent |
| 7 | Sync Stalker | -1.61 | -6.45 | 4.84 | 0.00047 | silent |
| 8 | Free Rotor | -1.50 | -6.27 | 4.76 | 0.00049 | silent |
| 9 | Wide Teeth | -1.00 | -7.19 | 6.19 | 0.00037 | silent |
| 10 | Legato Pressure | -1.67 | -5.95 | 4.28 | 0.00052 | silent |
| 11 | Rolling Reese | -1.50 | -9.08 | 7.59 | 0.00042 | silent |
| 12 | Neuro Growl | -4.77 | -11.59 | 6.82 | 0.00023 | silent |
| 13 | Vowel Talker | -6.43 | -13.05 | 6.62 | 0.00032 | silent |
| 14 | Hollow Stab | -11.62 | -26.77 | 15.15 | 0.00023 | silent |
| 15 | Clean Sub | -1.00 | -6.00 | 5.00 | 0.00087 | silent |
| 16 | Wobble Quarter | -1.00 | -5.62 | 4.62 | 0.00052 | silent |
| 17 | Wobble Triplet | -1.24 | -5.73 | 4.49 | 0.00049 | silent |
| 18 | Tearout | -6.43 | -14.49 | 8.06 | 0.00018 | silent |
| 19 | Noise Gravel | -3.92 | -8.26 | 4.34 | 0.00037 | silent |
| 20 | Five Hands | -2.50 | -7.35 | 4.86 | 0.00044 | silent |
| 21 | Pulse Lurker | -2.76 | -7.11 | 4.35 | 0.00047 | silent |
| 22 | Murk Chord | -1.00 | -13.97 | 12.97 | -0.00014 | silent |
| 23 | Trajectory Snarl | -2.45 | -6.32 | 3.86 | 0.00042 | silent |
| 24 | Bounce Pluck | -3.64 | -13.19 | 9.55 | 0.00023 | silent |
| 25 | Splinter | -7.07 | -10.86 | 3.79 | 0.00017 | silent |
| 26 | Slide Weight | -1.77 | -7.22 | 5.44 | 0.00050 | silent |
| 27 | Velocity Rasp | -3.56 | -8.18 | 4.62 | 0.00039 | silent |

Observations (not failures):

- Level spread is wide: 19 of 27 presets sit between -5.6 and -9 dBFS active RMS with peaks 1 to 3.6 dB below full scale, while Hollow Stab (-26.8), Tearout (-14.5), Vowel Talker (-13.1), Murk Chord (-14.0) and Bounce Pluck (-13.2) are 7 to 20 dB lower. The first group is very hot for a bass patch meant to sit in a mix. Option: lower `output_trim_db` of the loud ones by about 3 dB. Needs a listening decision, so it was not changed.
- Latency trimmed by the offline renderer is now taken from the patch: 50 samples at 48 kHz with oversampling off, 96 with x2 and 106 with x4 (the table in this report was produced after the fix; before it every preset was trimmed by 50).
- Tails end in exact digital silence (the 100 ms before the end of the render).

### Matrix (qa-plan Stage C)

216 renders, each a single 4-beat note (1.38 s) plus 2 s tail. Per render: all samples finite; peak at or below the limiter ceiling + 0.1 dB (worst peak -1.00 dBFS, exactly the default ceiling); Hann-weighted DC of 0.3 s to 1.3 s below 1e-3 (worst absolute value 1.9e-4); at velocity 127 the sub-solo render is sample-identical L/R and its largest sample-to-sample jump is below 0.25 of its peak. Largest jump on the full mix: 0.197 (Tearout, note 36). A CSV in the qa-plan format (`preset, note, vel, rate, block, peak_dbfs, rms_dbfs, dc, max_jump, pass`) is written when `SNARAL_QA_CSV=<path>` is set; the rates 44.1, 96, 192 kHz and block sizes 1, 64, 2048 are covered by `SnaralEngineTests::testSampleRates`, `testPrepareRelease` and pluginval, not by this matrix. The release-below -90 dBFS and determinism checks of the plan are covered by `SnaralEngineTests` and `WavExportTests` (bit-identical repeat renders), not repeated per preset.

## Standalone

`xvfb-run` with a 1280x800 virtual screen, no sound device (ALSA reports `/dev/snd/seq` missing; JUCE falls back to no audio). The standalone ran for the full 14 s until `timeout` killed it (exit 124), the editor drew correctly at the default size (screenshot reviewed: header, action row, five macros, scope with OUT, SUB, MID and MONO buttons now all enabled, DSP CPU gauge, meter, variations row and the first-run tour card). Real audio output through a device is UNVERIFIED.

## ThreadSanitizer

Built with `-O1 -fsanitize=thread` (JUCE-free tests only). `MidiLearnTests`, `CcHookTests`, `SnaralEngineTests`, `ParameterContractTests`, `VoiceAllocatorFuzzTests` and `UndoFuzzTests` all passed with no data race reported. Tests that start a second thread (publisher versus audio-thread reader) are the ones that give this result meaning; the JUCE project-state and processor tests were not run under TSan (UNVERIFIED).

## Not covered (UNVERIFIED)

Windows and macOS builds, AU, CLAP validator, LV2 host load, DAW matrix (Reaper, Ableton, FL Studio, Logic, Bitwig), real-time audio on a device, listening tests, pluginval strictness 10, host-reported latency in a DAW.
