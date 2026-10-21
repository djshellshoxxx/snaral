#!/usr/bin/env bash
# Light QA over the factory library: renders every preset offline through the real engine (Core OfflineRenderer) and checks
# for silence, NaN/Inf, clipping, DC, loudness sanity, a silent tail and a centred sub. Prints one table row per preset.
# Usage: tools/preset_qa.sh        (builds only the checker; exit code 1 on any failure)
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
build="$root/build-tests"
cmake -S "$root" -B "$build" -G Ninja -DSNARAL_BUILD_PLUGIN=OFF -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$build" -j2 --target snaral_factoryrenderqatests 2>&1 | grep -E "error|FAILED" || true
exec "$build/snaral_factoryrenderqatests"
