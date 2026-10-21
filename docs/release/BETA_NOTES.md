## Snaral @VERSION@ (beta)

**This is a beta pre-release. Not for production use.** Sounds, presets, parameters and saved projects may change between beta builds.

### Downloads

| Platform | Asset | Formats |
|---|---|---|
| Windows x64 | `snaral-@VERSION@-windows-x64.zip` | Standalone, VST3, CLAP |
| macOS Apple Silicon | `snaral-@VERSION@-macos-arm64.zip` | Standalone, VST3, AU, CLAP |
| macOS Intel | `snaral-@VERSION@-macos-x86_64.zip` | Standalone, VST3, AU, CLAP |
| Linux x64 (Ubuntu 24.04 build) | `snaral-@VERSION@-linux-x64.zip` | Standalone, VST3, CLAP, LV2 (best effort) |

Each zip has one subfolder per format and a `README_beta.txt` with install locations. Verify downloads with `SHA256SUMS.txt`.

### Known limitations

- **Unsigned binaries.** Windows builds are not code-signed: SmartScreen will warn ("More info" then "Run anyway"). macOS builds are only ad-hoc signed and not notarized: Gatekeeper will block them until you run `xattr -dr com.apple.quarantine <folder>` or use System Settings > Privacy & Security > "Open Anyway".
- The macOS Intel build is cross-compiled on Apple Silicon and has had no automated runtime test.
- Linux binaries are built on Ubuntu 24.04 and need a similarly recent glibc. LV2 is best effort.
- Plugin validation (pluginval) results for Linux VST3 are advisory in CI.
- Not every DAW has been tested. Please report host-specific problems.

### Reporting problems

Include OS and version, DAW and version, plugin format, `@TAG@`, and steps to reproduce.
