#!/usr/bin/env bash
# Build the JUCE plugin (and JUCE-dependent tests) in the current worktree.
# Serialised across ALL workers with a lock because the box has 2 cores / 7 GB RAM.
# Usage: tools/build_juce.sh [target ...]   (default: all)   Env: SNARAL_BUILD_TYPE (default Release)
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
build="$root/build-juce"
exec 9>/tmp/snaral-juce-build.lock
echo "waiting for build lock..." >&2
flock 9
# ci-speed: ccache launcher when installed; SNARAL_ENABLE_LTO=ON re-enables LTO (default OFF for dev builds)
launcher=()
if command -v ccache >/dev/null; then launcher=(-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache); fi
cmake -S "$root" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE="${SNARAL_BUILD_TYPE:-Release}" \
  -DSNARAL_ENABLE_LTO="${SNARAL_ENABLE_LTO:-OFF}" ${launcher[@]+"${launcher[@]}"} \
  -DSNARAL_JUCE_DIR=/home/claude/deps/juce -DSNARAL_CLAP_EXT_DIR=/home/claude/deps/clapext >/dev/null
targets=()
for t in "$@"; do targets+=(--target "$t"); done
cmake --build "$build" -j2 "${targets[@]}" 2>&1 | grep -E "error|FAILED|Error|warning:.*Snaral|Linking CXX" | tail -60
ctest --test-dir "$build" --output-on-failure 2>&1 | tail -25
