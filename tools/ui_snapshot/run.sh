#!/usr/bin/env bash
# Renders the UI kit snapshots (PNG) headlessly.
# Usage: tools/ui_snapshot/run.sh [output-dir]      default: build-juce/ui-snapshots
# Builds snaral_juce_uisnapshottests through tools/build_juce.sh (serialised across workers), then runs it.
# The renderer paints into juce::Images with the software renderer and needs no display; if a platform ever does,
# the script falls back to xvfb-run when it is installed.
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
out="${1:-$root/build-juce/ui-snapshots}"
"$root/tools/build_juce.sh" snaral_juce_uisnapshottests snaral_juce_uikittests >/dev/null
exe="$root/build-juce/snaral_juce_uisnapshottests"
if ! "$exe" "$out"; then
  if command -v xvfb-run >/dev/null 2>&1; then
    xvfb-run -a "$exe" "$out"
  else
    exit 1
  fi
fi
echo "Open the PNGs in $out (editor_*, advanced_*, widgets_*, displays_*, trajectory_*, options_*, help_*, tour_*, toasts_*)."
