# Feature Spec: Beta Release Packaging

**Module ID:** release-packaging  
**Depends on:** expansion-format-support, verification, qa-plan, audit-checklist  
**Owner of implementation:** release-eng (`.github/workflows/*`, `tools/package*`, `docs/release/*`)

## Objective

Define exactly which files a Snaral beta pre-release ships, how they are named, how they are (not) signed, and the procedure that produces and publishes them, so a tester can install a build without guessing and no artefact is claimed that was not validated.

## Artifact matrix

| Archive | Contents |
|---|---|
| `Snaral-<ver>-windows-x64.zip` | `Snaral.vst3`, `Snaral.clap`, `Snaral.exe` (Standalone), `INSTALL.txt`, `LICENSES/` |
| `Snaral-<ver>-macos-arm64.zip` | `Snaral.vst3`, `Snaral.component` (AU), `Snaral.clap`, `Snaral.app`, `INSTALL.txt`, `LICENSES/` |
| `Snaral-<ver>-macos-x86_64.zip` | same as arm64, built with `CMAKE_OSX_ARCHITECTURES=x86_64` |
| `Snaral-<ver>-linux-x64.tar.gz` | `Snaral.vst3`, `Snaral.clap`, `Snaral.lv2` (experimental), `Snaral` (Standalone), `INSTALL.txt`, `LICENSES/` |
| `SHA256SUMS.txt` | SHA-256 of every archive |

- `<ver>` is the tag without the leading `v`, for example `0.2.0-beta.1`. Tags are `v0.2.0-beta.N`; a hyphen makes the GitHub release a pre-release (the existing workflow already does this).
- Linux uses `.tar.gz` so the standalone keeps its executable bit. macOS archives are created with `ditto -c -k --keepParent` so bundle metadata survives; Windows uses `zip`.
- macOS builds are separate arm64 and x86_64 archives, not universal binaries. Windows ARM, Linux ARM and 32-bit are not built.
- Build output lives under `build/Snaral_artefacts/<config>/<Format>/`; packaging locates artefacts by glob and fails the job if any expected one is missing (never ship a partial archive).
- Current CI names artefacts `snaral-<OS>-<arch>.zip` and packages only VST3 and Standalone; it must be brought in line with this table before the first beta tag.

## Version and naming rules

- CMake `project(Snaral VERSION 0.2.0)` supplies the plug-in version everywhere; macOS bundle versions must stay numeric, so the `-beta.N` suffix and the short git SHA are supplied separately and shown in the editor footer and the About text (requirement for the integration owner).
- Plug-in, bundle and archive names always use `Snaral`. Company text is "Circuit Drift Labs".

## Signing and trust (beta)

- **Windows:** unsigned. SmartScreen may show "Windows protected your PC"; INSTALL.txt explains More info, then Run anyway.
- **macOS:** every bundle (`.vst3`, `.component`, `.clap`, `.app`) is ad-hoc signed in CI with `codesign --force --sign - <bundle>` (arm64 code must carry at least an ad-hoc signature to run). No Developer ID, no notarisation. Downloaded archives are quarantined; INSTALL.txt documents Control-click Open for the app and `xattr -dr com.apple.quarantine <path>` for plug-ins.
- **Linux:** unsigned; INSTALL.txt documents `chmod +x` where needed.
- Release notes state "unsigned or ad-hoc signed beta" in the first paragraph.

## Install locations documented in INSTALL.txt

VST3: Windows `C:\Program Files\Common Files\VST3`, macOS `~/Library/Audio/Plug-Ins/VST3`, Linux `~/.vst3`. CLAP: Windows `C:\Program Files\Common Files\CLAP`, macOS `~/Library/Audio/Plug-Ins/CLAP`, Linux `~/.clap`. AU: `~/Library/Audio/Plug-Ins/Components`. LV2: `~/.lv2`. Standalone: run in place or copy to Applications. Uninstall = delete these files plus the per-user data folder `Circuit Drift Labs/Snaral`.

## Pre-release procedure

1. Merge all feature branches into `work/beta`; CI is green for Windows, macOS arm64, macOS x86_64 cross-build and Linux, including `tools/build_tests.sh`-equivalent unit tests and sanitizer job.
2. Audit: complete [audit-checklist](audit-checklist.md) with zero unwired parameters; save to `docs/release/AUDIT.md`.
3. QA: execute [qa-plan](qa-plan.md); save to `docs/release/QA_REPORT.md`. Validators required: pluginval level 5 (VST3, AU, and any format it supports), clap-validator, auval. Fix blocking bugs and rerun the affected checks.
4. Licence gate: confirm the licence position (JUCE is dual-licensed AGPLv3 or commercial; the repository has no LICENSE file yet), include third-party notices (JUCE, clap-juce-extensions, CLAP headers) in `LICENSES/`, confirm provenance records for presets and assets. Do not tag without this.
5. Bump `project(... VERSION)` only for a new minor; tag `v0.2.0-beta.N` on the verified commit and push the tag.
6. The release workflow builds, packages per the matrix, signs macOS bundles ad hoc, writes `SHA256SUMS.txt`, and publishes a GitHub pre-release.
7. Smoke test on clean machines: download from the release, verify checksums, install per INSTALL.txt, load in one host per OS, play a note, save and reopen a project. Record results; anything not run is UNVERIFIED.
8. Release notes: scope, per-format status (Supported, Experimental, Excluded), hosts tested and UNVERIFIED list, signing note, known issues, install and uninstall pointers, checksums, feedback route. Record only measurements actually run.

## Acceptance criteria

1. The CI release job produces exactly the archives above; a missing artefact or format fails the job.
2. Each archive unpacks to the listed contents with no extra files, and checksums match `SHA256SUMS.txt`.
3. The macOS bundles verify with `codesign --verify --verbose` (ad hoc) and load on the matching architecture.
4. INSTALL.txt paths were followed on each OS by someone other than the packager, and the result is recorded.
5. Tag, archive names and in-app version string agree.
6. The release is marked pre-release and its notes contain the signing note and the per-format status table.

## Test fixtures

Packaging script dry-run against a fake artefact tree (missing-format failure case), checksum verification script, and the INSTALL.txt template under version control.
