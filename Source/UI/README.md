# Snaral UI kit (`snaral::ui`)

Reusable JUCE widgets, theme and help/onboarding for the Snaral editor. Everything is in `Source/UI/` (every `.cpp` is auto-compiled into the plug-in). `Source/UI/Views/` and `PluginEditor.*` belong to the integration worker and compose these parts. The kit has no audio-thread code and never includes the generated `JuceHeader.h`.

## Public types

| Header | Types | Purpose |
| --- | --- | --- |
| `UiCommon.h` | `metrics::*`, `Severity`, `PreferredHeightProvider`, `toJuce`, `textWidth` | Baseline sizes (window 720x480 min, 900x560 default, 32 px header, 28 px buttons, 32 px hit targets), shared helpers |
| `UiOptions.h` | `OptionsModel`, `SettingsFile` | User preferences as a plain ValueTree (`SnaralUi`): theme, tooltips, animations (reduced motion), UI scale, scope/CPU visibility, first-run flags. `SettingsFile::save/load` with an injectable path; `defaultFile()` is under the user app-data dir. Not part of presets or host state |
| `Theme.h` | `ThemeTokens`, `ThemeCatalog`, `contrastRatio`, `contrastRequirements`, `fonts::*`, `SnaralLookAndFeel`, `SnaralTooltipWindow`, `ThemeManager` | CDL default plus four more themes, WCAG-checked token pairs, font fallback (nothing bundled), tooltips on/off, UI scale |
| `ParamFormat.h` | `findParamDef`, `formatParamValue`, `parseParamValue`, `describeParamRange`, `prettyChoiceName`, `midiNoteName` | Value text and click-to-edit parsing from `snaral::parameterTable`; out-of-range input is rejected |
| `ParamContext.h` | `ContextAction`, `ContextHooks`, `ContextMenuSpec`, `buildContextMenu`, `Clipboard` | Context menu: Reset, Copy, Paste, Enter value, MIDI learn, Clear learn, Randomize this (the last three only when the hook is set) |
| `MacroKnob.h` | `MacroKnob` | Rotary control with label, unit readout, learn badge (`setLearnState`), double-click reset, click-to-edit, keyboard control. `bind(apvts, id)` |
| `ParamControls.h` | `ParamSlider`, `ParamCombo`, `ParamToggle` | APVTS-bound controls with accessible names, readouts, reset and context hooks |
| `SectionPanel.h` | `SectionPanel` | Collapsible section; `getPreferredHeight`, `onLayoutChanged` |
| `AdvancedTabs.h` | `AdvancedTab`, `advancedSections`, `AdvancedTabContent`, `TabbedAdvanced` | Tabs Voice, Sub, Sources, Filter & Motion, Drive, Texture, Effects, Output; scrollable with per-tab scroll memory. `buildDefaultTabs(apvts)` generates every control from the table; parameters edited elsewhere (macros, trajectory points) are skipped |
| `UiSources.h` | `ScopeSource`, `MeterSource`, `CpuSource`, `ScopeRingBuffer`, `AtomicMeterSource`, `AtomicCpuSource` | Abstract read-only interfaces for audio diagnostics, plus optional lock-free helpers the engine can own |
| `Displays.h` | `ScopeView`, `LevelMeter`, `MeterBallistics`, `CpuGauge` | Scope (30 Hz, 2 Hz with reduced motion, timer only while showing), peak/RMS meter with hold and clip, "DSP CPU" gauge |
| `StatusBar.h` | `StatusBar`, `ToastHost` | Status line with chips and version; toasts. Always icon plus text, announced to screen readers |
| `TrajectoryEditor.h` | `TrajectoryBinding`, `ApvtsTrajectoryBinding`, `TrajectoryGeometry`, `TrajectoryEditor` | Five-point editor bound to `traj_x2..x4` and `traj_y1..y5` with begin/end gestures, keyboard nudging, optional snap, curve preview through `setCurveEvaluator`, length in ms or division |
| `HelpContent.h`, `HelpPanel.h` | `HelpContent::*`, `HelpPanel` | Searchable local help generated from the table plus purpose lines; topic per macro and Advanced tab, shortcuts, MIDI learn |
| `OptionsPanel.h` | `OptionsPanel` | Theme, tooltips, animations, scale, displays, rerun tour, about; standalone-only device selector and 440 Hz tone button via `Config` callbacks |
| `OnboardingOverlay.h` | `OnboardingStep`, `OnboardingOverlay` | Skippable, re-runnable tour whose steps point at registered components |

## Composing the editor

1. Own an `OptionsModel` (load it with `SettingsFile::load(options, SettingsFile::defaultFile())`, save on change) and a `ThemeManager themes { options }`.
2. Build the children, then `themes.applyTo (*this)`. Call `ThemeManager::applyUiScale` for the scale option. Own a `SnaralTooltipWindow` and call `setTipsEnabled` from a `ThemeManager::Listener::preferencesChanged`.
3. Macros: five `MacroKnob`s, `bind (apvts, "weight")` (and growl, bite, motion, space). Fill `hooks` for MIDI learn and drive `setLearnState`.
4. Put `TabbedAdvanced::buildDefaultTabs (apvts, visitor)` in the Advanced view. The visitor receives every generated control and its `ContextHooks` to wire learn and randomize. Use `setTabContent` or `AdvancedTabContent::addCustomSection` to add a `TrajectoryEditor` (`std::make_unique<ApvtsTrajectoryBinding>(apvts)`) in Filter & Motion.
5. Diagnostics: implement `ScopeSource`, `MeterSource` and `CpuSource` on top of the engine (or own the `Atomic*` helpers) and pass them to `ScopeView`, `LevelMeter` and `CpuGauge`. A null source or no data shows honest "no data" text. Call `setDisplayEnabled` from the Options visibility toggles.
6. Add `StatusBar` at the bottom, `ToastHost` over the content, and `HelpPanel`, `OptionsPanel` and `OnboardingOverlay` as top-most children. Register the tour targets (`macro.weight` and so on, see `OnboardingOverlay.h`) and restore the user's patch in `onFinished`.
7. Resize handling: the kit adapts at 720x480 (for example the scope header collapses to one tap button). See `tools/ui_snapshot` for stand-in compositions at 720x480 and 900x560.

## Fonts

Nothing is bundled or licensed here. Sans: Inter, then Space Grotesk, then the JUCE default sans-serif. Mono: JetBrains Mono, then IBM Plex Mono, then the JUCE default monospace. `fonts::resetFamilyCache()` re-resolves after installing a font.

## Notes

- A `juce::Slider` creates its value box when the LookAndFeel changes. `ParamSlider` refreshes it when it joins a themed hierarchy, so the order of `applyTo` and `addAndMakeVisible` does not matter for it.
- `juce::TextEditor` change callbacks are asynchronous; the kit does not rely on them in tests.
- Tests: `tests/juce/UiKitTests.cpp` (uses `tests/juce/UiKitUnity.h`, which includes the UI `.cpp` files because the shared JUCE test library does not yet compile `Source/UI`; remove the includes once CMake adds `Source/UI/*.cpp` to it) and `tests/juce/UiSnapshotTests.cpp`.
