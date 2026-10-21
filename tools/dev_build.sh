#!/usr/bin/env bash
# ci-speed: fast local iteration. Builds ONLY the Standalone target (no VST3/CLAP/LV2, no LTO, no tests),
# with Ninja, ccache when available and unity batches. Output: build-dev/Snaral_artefacts/Release/Standalone/
# Usage: tools/dev_build.sh [--run]      Env: SNARAL_JUCE_DIR / SNARAL_CLAP_EXT_DIR (default /home/claude/deps/* if present)
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
build="$root/build-dev"
juce="${SNARAL_JUCE_DIR:-}"; [[ -z "$juce" && -d /home/claude/deps/juce ]] && juce=/home/claude/deps/juce
args=(-DCMAKE_BUILD_TYPE=Release -DSNARAL_BUILD_CLAP=OFF -DSNARAL_ENABLE_LTO=OFF -DSNARAL_UNITY_BUILD=ON)
command -v ninja >/dev/null && args+=(-G Ninja)
command -v ccache >/dev/null && args+=(-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache)
[[ -n "$juce" ]] && args+=(-DSNARAL_JUCE_DIR="$juce")
cmake -S "$root" -B "$build" "${args[@]}" >/dev/null
cmake --build "$build" --parallel "$(nproc 2>/dev/null || sysctl -n hw.logicalcpu || echo 2)" --target Snaral_Standalone
if [[ "${1:-}" == "--run" ]]; then
  exe="$(find "$build/Snaral_artefacts" -path '*Standalone*' -type f -name Snaral -perm -u+x | head -n1)"
  exec "$exe"
fi
