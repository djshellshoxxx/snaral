# Feature Spec: Preset Exchange Bundles

**Category:** Product value  
**Depends on:** preset-file-contract, preset-browser, parameter-state

## Objective
Let users share or back up groups of patches in one portable file without bundling binaries or requiring an online account.

## Bundle contract
- Extension .snaralpack; ZIP container with manifest.json and a presets/ directory of .snaralpreset files.
- Bundle contains no executable code, samples, absolute paths, symlinks, or nested archives.
- Maximum compressed size 50 MiB; maximum 500 presets; maximum expanded size 100 MiB.
- Validate entry paths against traversal, duplicate paths, compression bombs, malformed manifests, and per-preset size limits before extraction.
- Import previews bundle name, author/license, count, and conflicts. User chooses import/skip/replace per conflict.
- Export is deterministic where possible and preserves each preset’s identity and metadata.

## Acceptance
1. Valid bundle export/import preserves preset data semantically.
2. Path traversal, symlink, oversized, nested archive, and malformed entries are rejected before writing.
3. Failed import leaves library unchanged.
4. Duplicate handling follows explicit user selection.
5. Offline operation works and bundle contains no network dependency.
