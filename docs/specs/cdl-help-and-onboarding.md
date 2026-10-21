# Feature Spec: Shared Help and First-Run Onboarding

**Requirement source:** [Copied approved baseline](../../standards/AUDIO_APP_BASELINE.md)
**Applies to:** Snaral plug-in and standalone

## Objective
Give users accurate, accessible help synchronized with the shipped controls and introduce Snaral’s split Sub/Mid workflow without requiring internet access.

## Help content
- Provide a local help view covering every shipped feature, parameter purpose, units, range, default, MIDI behavior, preset actions, supported formats, and known limitations.
- Include quick start, signal-flow explanation, troubleshooting entry, supported host/platform list, and product support section.
- Link to https://djshellshoxxx.github.io/circuitdriftlabs/ . Show official CDL logo only when an approved asset is supplied.
- Documentation is versioned with the application. Help must not describe deferred or unimplemented features as available.
- Tooltips can be disabled in Options; keyboard and screen-reader labels remain.

## Onboarding
Use the existing Onboarding Sound Tour spec, promoted from expansion to core release. It must introduce Weight, Growl, Bite, Motion, Space, preset recall, layer separation, and Advanced controls. It is local, skippable, repeatable from Options, and operates on disposable state.

## Acceptance
1. Every visible control has corresponding help with matching label, unit, range, and behavior.
2. Help opens without a network connection and its website link opens the browser only after user activation.
3. Onboarding appears only on first run unless requested again; cancel/skip restores the prior patch exactly.
4. Documentation build/check catches missing parameter IDs and stale format claims.
5. Accessibility labels and keyboard navigation remain available when visual tooltips are disabled.

## Implementation notes (UI kit)
- Code: `Source/UI/HelpContent.{h,cpp}` (static content), `Source/UI/HelpPanel.{h,cpp}` (searchable view), `Source/UI/OnboardingOverlay.{h,cpp}` (tour; see expansion-onboarding-tour.md).
- Topics are generated from `snaral::parameterTable` (name, unit, range, default) plus a hand-written purpose line per parameter, so the help cannot drift from the shipped controls. `HelpContent::undocumentedParameterIds()` lists parameters without a purpose line; a unit test requires it to be empty, which is the "check catches missing parameter IDs" gate.
- Contents: quick start, signal flow, the five macros (one topic each), one topic per Advanced tab, keyboard shortcuts, MIDI learn guide, presets, formats and known limitations, troubleshooting, support.
- Search matches titles, bodies, keywords, parameter ids and names. The website button uses an injectable `openUrl` and only runs on a click. Help needs no network.
- Visual tooltips are optional (Options); accessible names and descriptions are always set on every widget.
