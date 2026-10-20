# Feature Spec: AU and CLAP Format Expansion

**Category:** Product value  
**Depends on:** host-integration, parameter-state, verification

## Objective
Extend the validated VST3/standalone instrument to AU on macOS and CLAP on supported desktop systems without changing DSP or preset semantics.

## Scope
- AU Audio Unit for supported macOS releases.
- CLAP for Windows, macOS, and Linux where the JUCE wrapper and toolchain support them.
- Reuse the same DSP, parameter IDs, state, presets, and UI behavior.
- Do not introduce format-specific features unless represented in the shared contract.

## Acceptance
1. Each format passes its official validator or the project’s documented equivalent.
2. Parameter IDs, MIDI acceptance, automation, state save/restore, latency, and MIDI behavior match VST3.
3. CI builds every declared OS/architecture combination.
4. Cross-format preset and project-state fixtures round-trip without value drift.
5. Release artifacts identify format, OS, architecture, minimum OS version, and signing/notarization status.
6. Do not publish a format until its validation and host matrix pass.
