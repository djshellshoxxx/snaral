#!/usr/bin/env bash
# Stage the built plugin formats of ONE platform into snaral-<version>-<os>-<arch>/ and zip it.
# Usage: tools/package.sh <version> <os: windows|macos|linux> <arch: x64|arm64|x86_64> <build-dir> <out-dir>
# Result: <out-dir>/snaral-<version>-<os>-<arch>.zip (the staged folder is left in <out-dir> for smoke checks).
# macOS bundles are ad-hoc signed (codesign -s -) and zipped with ditto so symlinks/permissions survive.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/.." && pwd)"

version="${1:?version}"; os="${2:?os}"; arch="${3:?arch}"; build="${4:?build dir}"; out="${5:?out dir}"
name="snaral-${version}-${os}-${arch}"
art="$build/Snaral_artefacts"
[[ -d "$art" ]] || { echo "No artefacts dir: $art" >&2; exit 1; }

mkdir -p "$out"
out="$(cd "$out" && pwd)"
stage="$out/$name"
rm -rf "$stage" "$out/$name.zip"
mkdir -p "$stage"

# find_one <format-dir> <name>: print the single artefact path (file or bundle dir), or fail.
find_one() {
  local fmt="$1" pat="$2" hit
  hit="$(find "$art" -maxdepth 4 -path "*/$fmt/*" -name "$pat" -not -path "*/Contents/*" | sort | head -n1)"
  if [[ -z "$hit" && "$fmt" == CLAP ]]; then
    # Unverified: the exact CLAP output folder is chosen by clap-juce-extensions; fall back to a name search.
    hit="$(find "$build" -maxdepth 5 -name "$pat" -not -path "*/Contents/*" -not -path "*/CMakeFiles/*" | sort | head -n1)"
  fi
  [[ -n "$hit" ]] || { echo "Missing $fmt artefact ($pat) under $art" >&2; return 1; }
  printf '%s\n' "$hit"
}

copy_in() { # copy_in <format-folder> <source>
  mkdir -p "$stage/$1"
  if [[ "$os" == macos ]]; then ditto "$2" "$stage/$1/$(basename "$2")"; else cp -a "$2" "$stage/$1/"; fi
}

case "$os" in
  windows) standalone="$(find_one Standalone 'Snaral.exe')" ;;
  macos)   standalone="$(find_one Standalone 'Snaral.app')" ;;
  linux)   standalone="$(find_one Standalone 'Snaral')" ;;
  *) echo "Unknown os: $os" >&2; exit 1 ;;
esac
copy_in Standalone "$standalone"
copy_in VST3 "$(find_one VST3 'Snaral.vst3')"
# clap-juce-extensions puts the .clap under <artefacts>/<config>/CLAP (found by name, not by assumed path).
copy_in CLAP "$(find_one CLAP 'Snaral.clap')"
if [[ "$os" == macos ]]; then copy_in AU "$(find_one AU 'Snaral.component')"; fi
if [[ "$os" == linux ]]; then copy_in LV2 "$(find_one LV2 'Snaral.lv2')"; fi

if [[ "$os" == macos ]]; then
  # Ad-hoc sign every bundle (no Developer ID: users still hit Gatekeeper, see README_beta.txt).
  while IFS= read -r -d '' bundle; do
    codesign --force --deep -s - "$bundle"
  done < <(find "$stage" -mindepth 2 -maxdepth 2 -type d \( -name '*.app' -o -name '*.vst3' -o -name '*.component' -o -name '*.clap' \) -print0)
fi

case "$os" in windows) display_os="Windows" ;; macos) display_os="macOS" ;; *) display_os="Linux" ;; esac
sed -e "s/@VERSION@/${version}/g" -e "s/@PLATFORM@/${display_os} ${arch}/g" \
  "$root/docs/release/README_beta.txt" > "$stage/README_beta.txt"

case "$os" in
  macos)   ditto -c -k --sequesterRsrc --keepParent "$stage" "$out/$name.zip" ;;
  linux)   (cd "$out" && zip -qry "$name.zip" "$name") ;;
  windows) (cd "$out" && 7z a -tzip -bso0 -bsp0 "$name.zip" "$name") ;;
esac
ls -l "$out/$name.zip"
