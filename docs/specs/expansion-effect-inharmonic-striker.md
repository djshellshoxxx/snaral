# Effect Spec: Inharmonic Striker

**Category:** Candidate effect  
**Depends on:** mid-source, mid-processing

## Objective
Add a short, pitched-but-inharmonic metallic strike layer for clangorous transitions and mechanical timbres.

## Controls
Strike amount, decay, partial spacing, damping, tune offset, wet mix, enable.

## Processing contract
- Use a small fixed bank of 3–6 resonant modes.
- MIDI note controls base frequency; bounded inharmonic offsets set partial spacing.
- Trigger strike on note-on. No feedback between modes. Default off.
- Decay 10–2000 ms, with hard energy bound and note-off/panic clear behavior.
- Mid-only; sub is not sent through the resonator.

## Acceptance
1. Same MIDI and state produce repeatable strikes.
2. Maximum decay ends within configured time plus one block.
3. Panic clears resonator energy within 5 ms.
4. No coefficient combination becomes unstable at supported sample rates.
5. CPU and memory remain within documented limits for 8 voices; sub output unchanged.
