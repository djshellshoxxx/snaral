#!/usr/bin/env bash
# Collect per-platform zips, write SHA256SUMS.txt and the rendered release notes.
# Usage: tools/finalize_release.sh <version> <tag> <dir-with-zips> <out-dir>
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/.." && pwd)"
version="${1:?version}"; tag="${2:?tag}"; in="${3:?zip dir}"; out="${4:?out dir}"

mkdir -p "$out"
for p in windows-x64 macos-arm64 macos-x86_64 linux-x64; do
  z="$(find "$in" -name "snaral-${version}-${p}.zip" | head -n1)"
  [[ -n "$z" ]] || { echo "Missing package for ${p}" >&2; exit 1; }
  cp "$z" "$out/"
done
(cd "$out" && sha256sum snaral-*.zip > SHA256SUMS.txt && sha256sum -c SHA256SUMS.txt)

{
  sed -e "s/@VERSION@/${version}/g" -e "s/@TAG@/${tag}/g" "$root/docs/release/BETA_NOTES.md"
  printf '\n## SHA-256\n\n```\n'
  cat "$out/SHA256SUMS.txt"
  printf '```\n'
} > "$out/RELEASE_NOTES.md"
ls -l "$out"
