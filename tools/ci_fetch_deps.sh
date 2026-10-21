#!/usr/bin/env bash
# Clone JUCE and clap-juce-extensions into <deps-dir> (with retries) so that CMake can be
# configured offline with -DSNARAL_JUCE_DIR / -DSNARAL_CLAP_EXT_DIR. A JUCE dir restored from the
# CI cache is reused if it is already at the right tag.
# Usage: tools/ci_fetch_deps.sh <deps-dir>
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=tools/ci_lib.sh
source "$here/ci_lib.sh"

deps="${1:?usage: ci_fetch_deps.sh <deps-dir>}"
juce_tag="$(sed -n 's/.*GIT_TAG[[:space:]]\{1,\}\([0-9][0-9.]*\).*/\1/p' "$here/../CMakeLists.txt" | head -n1)"
juce_tag="${juce_tag:?could not read the JUCE GIT_TAG from CMakeLists.txt}"
mkdir -p "$deps"

if [[ -f "$deps/JUCE/CMakeLists.txt" ]] && [[ "$(git -C "$deps/JUCE" describe --tags --exact-match 2>/dev/null || true)" == "$juce_tag" ]]; then
  echo "JUCE $juce_tag already present (cache hit)"
else
  rm -rf "$deps/JUCE"
  retry 4 10 git clone --depth 1 --branch "$juce_tag" https://github.com/juce-framework/JUCE.git "$deps/JUCE"
fi

# clap-juce-extensions tracks main in CMakeLists.txt; it is small, so always fetch fresh.
rm -rf "$deps/clap-juce-extensions"
retry 4 10 git clone --depth 1 --recurse-submodules --shallow-submodules \
  https://github.com/free-audio/clap-juce-extensions.git "$deps/clap-juce-extensions"
echo "clap-juce-extensions commit: $(git -C "$deps/clap-juce-extensions" rev-parse HEAD)"
echo "JUCE ($juce_tag) commit: $(git -C "$deps/JUCE" rev-parse HEAD)"
