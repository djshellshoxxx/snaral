# Feature Spec: CDL Theme and Options

**Requirement source:** [Copied approved baseline](../../standards/AUDIO_APP_BASELINE.md)
**Applies to:** Snaral VST3 and standalone

## Objective
Apply the shared CDL visual language consistently and provide a complete Options surface without exposing host-owned settings inside a plug-in.

## Themes
- Default theme is CDL, using the exact baseline tokens: Background #0E1116, Panel #171B22, Border #2A303A, Primary #E8532A, Secondary #4FB6C4, Text #E6E8EC, Muted #8A929E, Success #7BC96F, Warning #F2C14E, Shadow rgba(0,0,0,0.55).
- Provide at least four selectable themes: CDL default, a dark variant, high contrast, and two derived variants. Derived variants may alter token mappings, but must not be presented as official CDL palette colors.
- Use the official logo only if a supplied asset exists. Until then use the approved 2 px burnt-orange diagonal notch; do not invent a replacement logo.
- Use Inter/system sans for labels and JetBrains Mono/IBM Plex Mono/system monospace for numeric values, preserving baseline sizes and fallback behavior.
- Theme changes apply to every UI panel and persist per user installation, outside host automation and preset sound state.

## Options
Expose a gear/menu entry available from the main window. Include:
- Theme selection, tooltips on/off, global animation on/off, and rerun onboarding.
- In standalone only: audio driver/device, input/output routing, sample rate, buffer size, MIDI input device, MIDI mapping.
- In plug-in: show that audio device/rate/buffer are host-controlled; do not offer ineffective device settings.
- Standalone only: 440 Hz sine reference tone at −12 dBFS with obvious stop. Never emit it automatically or from the plug-in without an explicit user action.
- Persist settings safely; device failure retains the last valid selection and reports the failure.

## Acceptance
1. CDL theme token values match the approved baseline exactly.
2. Four themes render every view, dialog, tooltip and meter with readable contrast.
3. Theme, tooltip and animation preferences persist after restart and do not enter preset/host state.
4. Plug-in Options identify host-owned audio settings; standalone settings enumerate and recover from device loss.
5. Reference tone is manually started/stopped, measures −12 dBFS within 0.1 dBFS, and is absent from plug-in output unless explicitly requested by a future approved feature.

## Implementation notes (UI kit)
- Code: `Source/UI/Theme.{h,cpp}` (tokens, `ThemeCatalog`, `SnaralLookAndFeel`, `ThemeManager`, `SnaralTooltipWindow`), `Source/UI/UiOptions.{h,cpp}` (`OptionsModel` ValueTree, `SettingsFile`), `Source/UI/OptionsPanel.{h,cpp}`.
- Themes: `cdl` (official palette, exact baseline hex values), `midnight` (darker neutral variant), `highcontrast` (white borders, thicker strokes, 3 px focus ring), `tealdrift` and `daylight` (derived variants, flagged `officialCdlPalette = false` and described as derived in the Options panel).
- Derived tokens not in the baseline (`panelRaised`, `outline`, `accentText`, `onAccent`, `error`, `focusRing`) are defined per theme so every pair in `contrastRequirements()` meets WCAG AA (4.5:1 text, 3:1 graphics). `tests/juce/UiKitTests.cpp` asserts this for every theme and asserts the CDL tokens equal the baseline values.
- State is never colour-only: severity uses distinct icon shapes plus a word, toggles show a check mark, tabs an underline, MIDI learn a text badge, focus a ring.
- Fonts: nothing is bundled. Sans resolves Inter, then Space Grotesk, then the JUCE default sans-serif; mono resolves JetBrains Mono, then IBM Plex Mono, then the JUCE default monospace. Baseline sizes are kept (labels 11 px upper case, values 13.5 px, version 9 px). Adding a licensed font later only needs the family to be installed or embedded; no code change.
- Preferences: theme, tooltips, animations (reduced motion), UI scale (0.75 to 2.0), scope and CPU gauge visibility, first-run and tour-prompt flags. They live in a plain ValueTree (`SnaralUi`) stored in a per-user file (`<userApplicationData>/Circuit Drift Labs/Snaral/ui-settings.xml`, path injectable) and never enter APVTS, presets or host state. A malformed file is ignored and reported.
- The standalone device selector and the 440 Hz tone button are injected through `OptionsPanel::Config` callbacks and only appear when `standalone` is true. The panel never generates audio; the editor owns the tone (acceptance 5 is for the integration worker to verify).
