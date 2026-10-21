#!/usr/bin/env bash
# Build + run JUCE-free unit tests in the current worktree. Usage: tools/build_tests.sh [ctest -R regex]
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
build="$root/build-tests"
cmake -S "$root" -B "$build" -G Ninja -DSNARAL_BUILD_PLUGIN=OFF -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$build" -j2 2>&1 | grep -E "error|FAILED|warning: unused" || true
ctest --test-dir "$build" --output-on-failure ${1:+-R "$1"} 2>&1 | tail -25
