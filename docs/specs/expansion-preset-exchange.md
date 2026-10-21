# Feature Spec: Preset Exchange Bundles

**Category:** Product value  
**Status:** Post-beta (not in v0.2 beta).  
**Depends on:** preset-file-contract, expansion-preset-browser, expansion-preset-library, parameter-state

## Objective

Let users share or back up groups of patches in one portable file without executable code, binaries or an online account.

## Bundle contract

- Extension `.snaralpack`, a ZIP container (stored or deflate only; no encryption, no ZIP64, no multi-disk, no extra fields needed) holding exactly: `manifest.json` and files `presets/<name>.snaralpreset`. No other entry types, no directories other than `presets/`, no nested archives.
- Entry names are UTF-8, relative, use `/` only, and contain no `..`, leading `/`, drive letters, backslashes, control characters or case-insensitive duplicates. Maximum 500 presets, compressed size 50 MiB, expanded 100 MiB, each preset at most 1 MiB (the preset-file limit), and a compression ratio above 100:1 on any entry is rejected.
- `manifest.json` (UTF-8, at most 256 KiB, duplicate keys rejected, unknown fields rejected in version 1):

```
{ "format": "snaral-pack", "schema_version": 1, "pack_id": "...", "pack_version": "1.0.0",
  "min_state_version": "0.2.0", "name": "...", "author": "...",
  "license": "...", "description": "...", "created_utc": "YYYY-MM-DD",
  "presets": [ { "path": "presets/a.snaralpreset", "preset_id": "...", "sha256": "<64 hex>" } ] }
```

  `pack_id` (at most 64 characters, `a-z0-9._-`), `pack_version` (semantic version) and `min_state_version` are the fields the [Professional Preset Library](expansion-preset-library.md) needs, so a library pack is an exchange bundle with curated content; an imported pack can be removed as a unit by `pack_id` without touching separately saved presets. `name` at most 128 characters, `author` 128, `license` 256, `description` 2048. Every listed path must exist and every file must be listed; hashes must match.
- Each embedded preset must pass the full [preset file contract](preset-file-contract.md) validation.

## Import

1. The user picks a file. The importer opens it read-only and validates the central directory and manifest before reading any data (names, counts, sizes, ratios, symlink attributes, duplicates) and rejects the pack on any violation; nothing is written.
2. A preview shows name, author, license, count, total size and conflicts (same `preset_id`, or same name with different content). The user chooses Import, Skip or Replace per conflict, or a bulk choice.
3. Presets are validated into memory, then written to `Presets/Imported/<sanitised pack name>/` using temporary files and atomic rename. The final step publishes the library change as one unit; if any write fails, everything written by this import is removed and the library index is unchanged.
4. Factory presets are never overwritten. User files are replaced only on an explicit Replace choice.

## Export

Choose presets (or a folder or the whole user library), name, author and license. Output is deterministic where possible: entries sorted by path, fixed timestamp 1980-01-01 00:00:00, fixed deflate level, preset bytes identical to the stored files (their own export is already deterministic), manifest keys in fixed order. Export writes a temporary file and renames atomically.

## Edge cases

Pack with zero presets (rejected), invalid preset among valid ones (whole pack rejected with a list of offending entries, nothing imported), disk full, user cancels mid-import, a pack exported by a newer schema (rejected with a version message), very long names, case-insensitive file systems.

## Acceptance criteria

1. A valid pack exports and re-imports with semantically identical presets and manifest; exporting twice gives identical bytes.
2. Path traversal, absolute and backslash names, symlink attributes, oversized, high-ratio, nested archive, duplicate and unlisted entries, bad hashes and malformed manifests are rejected before any file is written.
3. A failed or cancelled import leaves the library and index byte-identical.
4. Conflict handling follows the explicit per-conflict choice and never touches factory content.
5. Everything works offline; the pack and importer contain no network reference.

## Test fixtures

Checked-in valid pack, one malicious pack per rejection class, a conflict pack, and a 500-preset size-limit pack generated in the test.
