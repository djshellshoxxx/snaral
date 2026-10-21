# ui_snapshot

Headless rendering of the Snaral UI kit to PNG for visual review.

- Source: `tests/juce/UiSnapshotTests.cpp` (CMake target `snaral_juce_uisnapshottests`, picked up by the `tests/juce/*Tests.cpp` glob and listed in ctest).
- Run: `tools/ui_snapshot/run.sh [output-dir]` (default `build-juce/ui-snapshots`), or run the built binary directly with an output directory argument.
- No display is needed: components are painted with `createComponentSnapshot` into a `juce::Image`. The script falls back to `xvfb-run` if that ever fails.

Output files (every theme, 720x480 and 900x560 where relevant):

| Prefix | Content |
| --- | --- |
| `editor_<theme>_<w>x<h>` | A stand-in editor composing header, five macros, scope, meter, CPU gauge, Advanced tabs and status bar for all five themes |
| `advanced_<n>_<tab>_<w>x<h>_top` / `_bottom` | Each Advanced tab at both scroll extremes |
| `widgets_<theme>_720x480` | Knob sizes and learn states, sliders, combo, toggles, section panels, status bar |
| `toasts_<theme>_720x300` | Info / Success / Warning / Error toasts |
| `displays_cdl_720x300` | Scope (with and without source), meters, CPU gauge normal / overrun / no data |
| `trajectory_*` | Trajectory editor |
| `options_*`, `help_*` | Options (plug-in and standalone, scrolled) and Help (topic, search, no match) |
| `tour_step<NN>_<w>x<h>` | Every onboarding step over the stand-in editor |

The stand-in editor only exists in the test; the real editor is composed by the integration worker.
