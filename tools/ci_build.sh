#!/usr/bin/env bash
# ci-speed: build every shipped format of ONE platform in a single cmake --build call (one ninja graph, the
# JUCE shared-code library is compiled once and reused by all format targets).
# Usage: tools/ci_build.sh <os: windows|macos|linux> <build-dir> [--with-tests]
#   --with-tests also builds the JUCE-dependent tests (tests/juce), so ctest -R snaral_juce_ can run afterwards.
# Env: CMAKE_BUILD_TYPE (default Release), BUILD_JOBS (default: all cores)
set -euo pipefail
os="${1:?os}"; build="${2:?build dir}"; with_tests=0
[[ "${3:-}" == "--with-tests" ]] && with_tests=1
cfg="${CMAKE_BUILD_TYPE:-Release}"
jobs="${BUILD_JOBS:-$(nproc 2>/dev/null || sysctl -n hw.logicalcpu 2>/dev/null || echo 2)}"

targets=(Snaral_Standalone Snaral_VST3 Snaral_CLAP)
case "$os" in
  macos) targets+=(Snaral_AU) ;;
  linux) targets+=(Snaral_LV2) ;;
  windows) ;;
  *) echo "Unknown os: $os" >&2; exit 1 ;;
esac

args=()
for t in "${targets[@]}"; do args+=(--target "$t"); done
if (( with_tests )); then
  # JUCE test executables are named snaral_juce_<name>; list them from the generated build system.
  while IFS= read -r t; do args+=(--target "$t"); done \
    < <(ctest --test-dir "$build" -N -C "$cfg" | sed -n 's/^ *Test *#[0-9]*: *\(snaral_juce_[A-Za-z0-9_]*\).*/\1/p')
fi
echo "Building ${targets[*]} (tests=$with_tests, jobs=$jobs)"
log="$(mktemp)"
set +e
cmake --build "$build" --config "$cfg" --parallel "$jobs" "${args[@]}" 2>&1 | tee "$log"
rc=${PIPESTATUS[0]}
set -e
if (( rc != 0 )); then
  # Surface compiler/linker errors as workflow annotations (job logs are not readable via the unauthenticated API).
  grep -E "error:|error LNK|undefined reference|Undefined symbols|ld: " "$log" | sort -u | head -15 \
    | while IFS= read -r line; do echo "::error title=build error::${line:0:400}"; done
fi
exit "$rc"
