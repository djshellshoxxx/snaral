# Feature Spec: Professional Preset Library

**Category:** Product value  
**Depends on:** factory-presets, preset-file-contract, preset-browser

## Objective
Offer curated, production-oriented preset packs that expand the initial factory library while preserving provenance, consistent metadata, and reliable import.

## Behavior
- Packs contain only Snaral preset JSON and a manifest; no external audio, executable scripts, or paths.
- Each patch is tagged by sound role, motion, sub/mid balance, texture, and intended performance mode.
- Pack metadata includes pack ID/version, author, license, compatible minimum plug-in state version, and preset count.
- Pack import validates every file before adding any; duplicate IDs prompt skip/replace/cancel.
- No store, payment, licensing, or activation system is part of this feature spec.

## Pack format and import (`PresetPack`, `PresetLibrary::importPack`)

**Layout.** A pack is a folder with `manifest.json` and the `.snaralpreset` files it lists. Other files are ignored and never copied.

**Manifest** (`"format": "snaral-pack"`, `schema_version` 1, lexical keys, 256 KiB cap, at most 512 presets):
- `pack_id`: `[a-z0-9._-]{1,64}`, not starting with a dot; also the install folder name.
- `name` and `version` (required), `author`, `license` (required, SPDX id or short statement, at most 256 chars), `min_plugin_version` (informational).
- `files`: unique bare file names ending `.snaralpreset`, no separators, no `..`, no control characters.
- `preset_count`: must equal the number of `files`.
Unknown manifest keys are ignored with a warning.

**Import.** Every file is parsed and validated (strict parser, `requireComplete`) before anything is written. If any file is invalid the whole pack is rejected and each problem is reported with its file name. Files are written atomically into a hidden staging folder next to `Packs/`, then swapped in with renames, keeping the old copy until the swap succeeds. A failure leaves the previously installed pack untouched.

**Duplicates.** Conflicts are detected on the pack id (already installed) and on each file's `preset_id` (used by a factory preset, a user preset or another pack). `Cancel` (default) changes nothing and reports every conflict. `Skip` leaves an already installed pack alone, or imports the non-conflicting presets and reports the skipped ones. `Replace` replaces the pack of the same id and its presets. Factory presets are never replaced. The result lists each item as imported, skipped, invalid or cancelled.

**Removal.** `removePack(packId)` deletes only `Packs/<packId>`. Presets the user saved in the user folder are never touched. Favourites of removed presets are kept.

**Visibility.** Pack entries carry pack id, so the browser details panel can show the pack name, version, author and licence from the installed manifest. Embedded factory presets use the summary in `Resources/presets/PROVENANCE.md`.

**Security.** No scripts, audio or absolute paths are accepted. File names are sanitised and compared case-insensitively.

## Acceptance
1. Pack import is atomic and reports each skipped/duplicate/invalid item.
2. All presets meet factory note, level, peak, and centered-sub review requirements.
3. Pack manifest and preset count match actual contents.
4. User can remove an imported pack without deleting separately saved presets.
5. Pack authorship/license data is visible in browser details.
