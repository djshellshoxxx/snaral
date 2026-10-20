# Feature Spec: Onboarding Sound Tour

**Category:** Usability  
**Depends on:** macros-ui, preset-variation

## Objective
Teach the split-layer concept and five macros through a short, skippable in-plugin demonstration.

## Behavior
- First launch offers a tour; users can skip, restart, or disable future prompts.
- The tour uses a disposable demo state and never overwrites the current patch.
- Steps demonstrate Weight, Growl, Bite, Motion, Space, Advanced view, preset recall, and layer separation.
- Use built-in audio/presets only. No network calls, analytics, or account requirement.
- Each step has concise text, a visual highlight, Back/Next/Skip, and keyboard support.
- If the host is processing MIDI/audio, tour state changes must be safe and must not change the user’s saved project state.

## Acceptance
1. First-run prompt appears once unless user opts to show again.
2. Skip and close return to the previous patch without state changes.
3. Every step can be completed using keyboard only.
4. Tour works at minimum UI size and supported DPI settings.
5. No external network request occurs during tour.
