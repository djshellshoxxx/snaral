#!/usr/bin/env bash
# Post-build smoke check of a staged package folder. Exit code 1 only for hard failures
# (missing files, wrong architecture, bad signature, unresolved libraries).
# With --pluginval <log> (Linux) it instead runs the advisory pluginval validation of the VST3.
# Usage: tools/smoke.sh <os> <arch> <staged-dir> [--pluginval <log-file>]
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=tools/ci_lib.sh
source "$here/ci_lib.sh"

os="${1:?os}"; arch="${2:?arch}"; stage="${3:?staged dir}"; shift 3
want_pluginval=0; plog=""
if [[ "${1:-}" == "--pluginval" ]]; then want_pluginval=1; plog="${2:?log file}"; fi

if (( want_pluginval )); then
  : > "$plog"
  work="$(mktemp -d)"
  {
    retry 4 10 curl -fsSL -o "$work/pluginval.zip" https://github.com/Tracktion/pluginval/releases/latest/download/pluginval_Linux.zip
    unzip -q "$work/pluginval.zip" -d "$work"
    chmod +x "$work/pluginval"
    xvfb-run -a "$work/pluginval" --strictness-level 5 --skip-gui-tests --validate-in-process \
      --output-dir "$work/out" "$stage/VST3/Snaral.vst3"
  } 2>&1 | tee -a "$plog"
  exit "${PIPESTATUS[0]}"
fi

fail=0
need() { if [[ -e "$stage/$1" ]]; then echo "ok   $1"; else echo "MISSING $1" >&2; fail=1; fi; }

need README_beta.txt
need CLAP/Snaral.clap
need VST3/Snaral.vst3
case "$os" in
  windows) need Standalone/Snaral.exe ;;
  macos)   need Standalone/Snaral.app; need AU/Snaral.component ;;
  linux)   need Standalone/Snaral; need LV2/Snaral.lv2 ;;
esac
(( fail == 0 )) || exit 1

if [[ "$os" == macos ]]; then
  for b in Standalone/Snaral.app VST3/Snaral.vst3 AU/Snaral.component CLAP/Snaral.clap; do
    codesign --verify --deep --strict "$stage/$b" && echo "ok   codesign $b"
    exe="$(find "$stage/$b/Contents/MacOS" -type f | head -n1)"
    archs="$(lipo -archs "$exe")"
    if [[ " $archs " != *" $arch "* ]]; then echo "WRONG ARCH $b: have '$archs', want '$arch'" >&2; fail=1; else echo "ok   arch $b ($archs)"; fi
  done
fi
if [[ "$os" == linux ]]; then
  # Every plugin binary must resolve all its shared libraries.
  for so in "$stage/VST3/Snaral.vst3/Contents/x86_64-linux/Snaral.so" "$stage/CLAP/Snaral.clap" "$stage/Standalone/Snaral"; do
    [[ -f "$so" ]] || continue
    if ldd "$so" | grep -q 'not found'; then echo "UNRESOLVED libs in $so" >&2; ldd "$so" | grep 'not found' >&2; fail=1; else echo "ok   ldd $(basename "$so")"; fi
  done
fi
(( fail == 0 )) || exit 1
echo "smoke ok"
