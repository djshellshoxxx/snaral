Snaral @VERSION@ (@PLATFORM@)
Circuit Drift Labs - drum and bass bass synth

BETA SOFTWARE. Not for production use. Expect bugs; save your work often.

Folders in this package (only formats your platform supports are present):
  Standalone/  stand-alone application
  VST3/        Snaral.vst3
  CLAP/        Snaral.clap
  AU/          Snaral.component   (macOS)
  LV2/         Snaral.lv2         (Linux, best effort)

Install locations
  Windows  VST3: C:\Program Files\Common Files\VST3\
           CLAP: C:\Program Files\Common Files\CLAP\
  macOS    VST3: /Library/Audio/Plug-Ins/VST3/   (or ~/Library/Audio/Plug-Ins/VST3/)
           AU:   /Library/Audio/Plug-Ins/Components/   (or ~/Library/...)
           CLAP: /Library/Audio/Plug-Ins/CLAP/   (or ~/Library/...)
  Linux    VST3: ~/.vst3/
           CLAP: ~/.clap/
           LV2:  ~/.lv2/

These builds are NOT code-signed by a trusted publisher.
  Windows: SmartScreen may say "Windows protected your PC". Click "More info", then "Run anyway".
           If the zip was downloaded, right-click it, Properties, tick "Unblock" before extracting.
  macOS:   binaries are only ad-hoc signed. If macOS says the item is damaged or from an unidentified
           developer, open Terminal and run (adjust the path):
             xattr -dr com.apple.quarantine ~/Downloads/snaral-@VERSION@-macos-*
           or use System Settings > Privacy & Security > "Open Anyway". Restart your DAW and rescan plugins.
  Linux:   mark the standalone executable runnable: chmod +x Standalone/Snaral

Verify the download against SHA256SUMS.txt from the release page.
Please report bugs with your OS, DAW, plugin format and steps to reproduce.
