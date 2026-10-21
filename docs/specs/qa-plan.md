# Feature Spec: Light QA Protocol (Beta)

**Module ID:** qa-plan  
**Depends on:** verification, expansion-format-support, release-packaging, audit-checklist  
**Output:** `docs/release/QA_REPORT.md` (written by the QA worker from the templates below)

## Objective

A short, repeatable QA pass that finds blocking defects before a beta pre-release without claiming exhaustive coverage. Every line of the report is either evidence from a command that was run or is marked UNVERIFIED.

## Stage A: automated gates (CI, every candidate)

JUCE-free suite via `tools/build_tests.sh`, the JUCE tests via ctest, the ASan/UBSan run, and a ThreadSanitizer run of parameter, state and MIDI-learn tests. All must pass. Exact commands: [verification](verification.md).

## Stage B: plug-in validators (per candidate artefact)

| Artefact | Command | Pass rule |
|---|---|---|
| VST3 (every OS) | `pluginval --strictness-level 5 <Snaral.vst3>` | exit code 0 (blocking). Level 5 is described by the pluginval project as the lowest level recognised for host compatibility. |
| VST3, once per candidate | `pluginval --strictness-level 10 <Snaral.vst3>` | informational; failures are triaged, not automatically blocking. Higher levels add slower tests such as parameter fuzzing and repeated state restores. |
| AU (macOS) | `auval -v aumu Snrl Cdlb` and `pluginval --strictness-level 5 <Snaral.component>` | both pass (blocking) |
| CLAP | `clap-validator validate <Snaral.clap>` | all tests pass or skip with reason (blocking) |
| LV2 (Linux) | bundle structure check plus load in one LV2 host | experimental; failures listed as known issues |

Capture the full output file for each run; record the validator version. Run pluginval headless where no display exists (use `--help` for the flags of the installed version; do not assume flags that were not checked).

## Stage C: headless render checks (offline render driver, no DAW)

Rendered at 48 kHz, block 256, then spot-checked at 44.1, 96 and 192 kHz and block 1, 64, 2048. Assertions per render:
1. Every sample finite; no sample magnitude above 1.0 with the limiter on (peak at or below ceiling + 0.1 dB).
2. Sub-only render: left equals right exactly at Space 0 and 100.
3. DC: mean of a 1 s steady-state segment below 1e-3 in magnitude.
4. Click detector: on steady notes and after each parameter step in the sweep list, the largest sample-to-sample jump is below a documented bound (default 0.25 of peak for filtered content) and no jump above the smoothing criteria for gain macros.
5. Release: output below -90 dBFS within release time + one block after note-off (excluding intentional long tails).
6. Determinism: two runs with identical inputs are bit-identical.
Matrix: every factory preset at notes 24, 36, 48, 60 and velocities 64, 127; plus the stress renders in verification (randomised endpoints, 10,000 blocks), oversampling off/2x/4x, Furnace and Fracture on, Trajectory on, Anchor on, WAV export round trip. Output: a CSV (`preset, note, vel, rate, block, peak_dbfs, rms_dbfs, dc, max_jump, pass`) attached to the report.

## Stage D: host smoke test (manual, about 15 minutes per host and format)

Minimum: Windows (VST3 and CLAP host plus standalone), macOS arm64 (AU and VST3 host plus standalone), macOS x86_64 or a documented Rosetta/cross run, Linux (VST3 or CLAP host plus standalone). Hosts and versions are recorded exactly; not-run cells say UNVERIFIED.
Checklist: scan and load; MIDI notes sound and stop; automate one macro and one Advanced parameter; save project, close, reopen, patch intact; change sample rate and buffer size; transport stop and loop with held notes; tempo sync at 60/120/180; open editor at default and minimum size; Panic; undo/redo; A/B; preset browser; Export WAV plays elsewhere; unload without crash.

## Stage E: listening notes

Level-match each pair or preset set to the same short-term loudness on a stated meter before judging. Listen solo and against a dense drum and bass reference at the project tempo. Record impressions only; no numbers unless measured. Template:

```
Build: <sha/tag>  Date: <>  Listener: <>  Playback: <headphones/monitors model>  Rate/Block: <>
Preset: <name>  Notes: <24/36/48/60>  Vel: <64/127>  Level match: <meter + target>
Sub clarity: __  Mid character: __  Movement: __  Mono check: __  In-mix: __
Defects (artefacts, clicks, aliasing, imbalance): __  Verdict: keep | tweak | drop
```

## Bug triage

| Severity | Definition | Release rule |
|---|---|---|
| S1 | crash, hang, audio-thread block, stuck note, NaN/Inf, sub off-centre, data loss, state not restored | blocks the beta |
| S2 | wrong sound or mapping, unwired control, validator failure, wrong latency, broken export | blocks unless an owner-approved, documented exception |
| S3 | UI glitch, clipped text, wrong tooltip, minor workflow bug | fix if cheap, otherwise Known Issues |
| S4 | cosmetic | backlog |

Bug record: `ID, build, OS/host/format, steps, expected, actual, attachment (preset, state, render, MIDI), severity, spec reference, owner, status`. A harness defect is tagged separately from a plug-in defect. Each S1 and S2 fix needs a regression test or a recorded manual re-run.

## Exit criteria

QA is complete when Stage A and B pass on the final candidate, Stage C has zero failing rows, Stage D covers the minimum cells (others UNVERIFIED), Stage E has notes for every factory preset, and no open S1 or S2 remains without an approved exception. Only then may the report state BETA READY.

## Acceptance criteria

1. The report contains an entry (PASS, FAIL, BLOCKED or UNVERIFIED) with evidence for every row of stages A to D.
2. Validator outputs and the render CSV are attached or linked.
3. Every open bug has severity, owner and a release decision.
4. No result is stated that the report does not trace to a command or a named observer.
