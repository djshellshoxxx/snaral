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

## Real editor snapshots

`tests/juce_plugin/EditorSnapshotTests.cpp` (target `snaral_plugin_editorsnapshottests`) renders the REAL `SnaralAudioProcessorEditor`
headlessly, with live audio behind the scope and meter. Run it under a virtual display if the platform needs one:

`tools/build_juce.sh snaral_plugin_editorsnapshottests && xvfb-run -a build-juce/snaral_plugin_editorsnapshottests build-juce/editor-snapshots`

| Prefix | Content |
| --- | --- |
| `real_main_<theme>_<w>x<h>` | Main view, every theme at 720x480, 900x560, 1440x900 |
| `real_browser_<theme>_<w>x<h>` | Main view with the preset browser open (720 and 900) |
| `real_advanced_<theme>_<w>x<h>` | Advanced view (900x560 and 720x480) |
| `real_tab<0-7>_cdl_900x560` | Every Advanced tab |
| `real_options/help/midimap/export/learn/randomized/toast_cdl_900x560` | Overlays and states |
| `real_tour<0-9>_cdl_900x560` | Every onboarding step on the real editor |
