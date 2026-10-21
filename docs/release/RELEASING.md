# Releasing a Snaral beta

Beta releases are always GitHub **pre-releases**, built by `.github/workflows/build.yml` when a tag matching `v*-beta*` is pushed.

## Platforms and assets

| Asset | Built on | Formats |
|---|---|---|
| `snaral-<version>-windows-x64.zip` | windows-2022 | Standalone, VST3, CLAP |
| `snaral-<version>-macos-arm64.zip` | macos-15 (arm64) | Standalone, VST3, AU, CLAP |
| `snaral-<version>-macos-x86_64.zip` | macos-15, cross-compiled (`CMAKE_OSX_ARCHITECTURES=x86_64`) | Standalone, VST3, AU, CLAP |
| `snaral-<version>-linux-x64.zip` | ubuntu-24.04 | Standalone, VST3, CLAP, LV2 |
| `SHA256SUMS.txt` | release job | checksums of the four zips |

`<version>` is the tag without the leading `v`, e.g. `0.2.0-beta.1`. Each zip contains `snaral-<version>-<os>-<arch>/` with one subfolder per format and `README_beta.txt` (from `docs/release/README_beta.txt`). Release notes come from `docs/release/BETA_NOTES.md` (`@VERSION@`, `@TAG@` are substituted; checksums are appended).

macOS bundles are ad-hoc signed only (`codesign --force --deep -s -`), not notarized. Windows binaries are unsigned.

## Procedure

1. Merge everything into `work/beta`; wait for CI (push to `work/**`) to be green on all four platforms.
2. Bump the version in `CMakeLists.txt`: `project(Snaral VERSION X.Y.Z ...)`. CMake versions cannot carry a `-beta.N` suffix, so the suffix lives only in the tag. Commit.
3. Review `docs/release/BETA_NOTES.md` (known limitations) and push the commit.
4. Tag and push:
   ```
   git tag -a v0.2.0-beta.1 -m "Snaral 0.2.0-beta.1"
   git push origin v0.2.0-beta.1
   ```
5. Watch the "Build, test and release" run. The `release` job only runs when every platform build succeeded.

### Manual build without a tag

Actions > "Build, test and release" > Run workflow. Enter `version` (e.g. `0.2.0-beta.2`, no leading `v`). Leave `publish` off to only get workflow artifacts (`pkg-*`, 14 days). Turn `publish` on to also create the pre-release `v<version>` from the selected commit. Versions without a `-suffix` are refused for publishing.

## Retagging a failed build

If a platform failed because of a CI or code problem, fix it, then pick one:

- Keep the tag name (nothing published yet, since the release job was skipped):
  ```
  git tag -fa v0.2.0-beta.1 -m "Snaral 0.2.0-beta.1"
  git push --force origin v0.2.0-beta.1
  ```
  If a pre-release was already created, delete it first (`gh release delete v0.2.0-beta.1 --yes`), otherwise it is updated in place with the new assets.
- Or (preferred once anyone may have downloaded it) bump to `v0.2.0-beta.2`.

Never move a tag whose assets users already downloaded.

## Re-running

- Flaky network step: open the run, "Re-run failed jobs". The release job re-runs once the build jobs pass. Re-run uses the same commit.
- Same tag after a code fix: retag as above (a re-run does not pick up new commits).
- Publish only (builds are fine, release step failed): "Re-run failed jobs" on the run.

## Verifying the release

```
gh release view v0.2.0-beta.1 --json isPrerelease,assets --jq '.isPrerelease, (.assets[].name)'
mkdir verify && cd verify && gh release download v0.2.0-beta.1
sha256sum -c SHA256SUMS.txt
```

Expect `true`, the four zips and `SHA256SUMS.txt`. Then, per platform: unzip, check the format folders listed above, check `README_beta.txt`, load the VST3/CLAP in a DAW. On macOS also `codesign --verify --deep --strict <bundle>` and `lipo -archs <binary inside Contents/MacOS>` (`arm64` or `x86_64`). The Linux pluginval log is attached to the run as `pluginval-log-linux-x64` (advisory only).

## Build dependencies

- **Linux (Ubuntu 24.04):** `build-essential cmake ninja-build git ccache zip unzip curl xvfb libasound2-dev libjack-jackd2-dev libfreetype-dev libfontconfig1-dev libx11-dev libxcomposite-dev libxcursor-dev libxext-dev libxinerama-dev libxi-dev libxrandr-dev libxrender-dev libglu1-mesa-dev mesa-common-dev libegl1-mesa-dev`. This follows JUCE's "Linux Dependencies" doc; curl and WebKit are not needed (`JUCE_USE_CURL=0`, `JUCE_WEB_BROWSER=0`).
- **macOS:** Xcode command line tools, CMake, Ninja, ccache (Homebrew). Deployment target 11.0 is set in `CMakeLists.txt`.
- **Windows:** Visual Studio 2022 (MSVC, `-A x64`), CMake, 7-Zip (preinstalled on the runner).
- JUCE (tag from `CMakeLists.txt`) and clap-juce-extensions (`main`, so CLAP builds are not byte-reproducible over time) are cloned by `tools/ci_fetch_deps.sh` and passed via `SNARAL_JUCE_DIR` / `SNARAL_CLAP_EXT_DIR`. JUCE is cached keyed on `CMakeLists.txt`.

## Local packaging

After a build into `build/`: `tools/package.sh 0.2.0-beta.1 linux x64 build dist` then `tools/smoke.sh linux x64 dist/snaral-0.2.0-beta.1-linux-x64`.
