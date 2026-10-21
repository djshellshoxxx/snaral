# Feature Spec: Onboarding Sound Tour (Core CDL Requirement)

**Category:** Usability  
**Depends on:** macros-ui, preset-variation

**Release scope:** Core CDL-compliant release. The filename is retained for link stability; this feature is not deferred to expansion.

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

## Implementation notes (UI kit)
- `OnboardingOverlay` (`Source/UI/OnboardingOverlay.{h,cpp}`) is the visual half: a dimmed overlay with a cut-out around the registered component of the current step, a card with concise text and Back / Next / Skip, a "do not offer this tour again" checkbox on the first step, and keyboard support (Right or Enter next, Left back, Escape skip, other keys swallowed while it is open).
- Default steps: welcome, Weight, Growl, Bite, Motion, Space, preset recall, layer separation, Advanced, finish. Targets are registered by id (`macro.weight`, `macro.growl`, `macro.bite`, `macro.motion`, `macro.space`, `preset.selector`, `scope`, `advanced.tabs`, `options.button`); a step without a registered target shows a centred card.
- The overlay never touches parameters or audio. The editor uses `onStepShown` to switch views or run a demo on a disposable copy of the patch, and must restore the user's patch in `onFinished` (Completed or Skipped). `OptionsModel` keeps `firstRunComplete` and `tourPromptDisabled`; `shouldOfferTour()` gives the first-run decision. The tour can be restarted with `start()` (Options and Help buttons).
- The card stays inside the window and away from the highlighted target at 720x480 and 900x560 (snapshots `tour_step*`).
