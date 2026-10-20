# Feature Spec: Preset File Contract

**Module:** preset-variation  
**Depends on:** parameter-state  
**Related:** parent developer spec and JSON example in preset-variation.md

## Objective

Make user preset files portable, safe to import, deterministic to export, and evolvable without silently corrupting older projects.

## File contract

- Extension: .snaralpreset
- Encoding: UTF-8 without executable content.
- Root object fields: format, schema_version, plugin, preset, parameters.
- format must equal snaral-preset.
- schema_version is an integer; initial version is 1.
- plugin contains name and state_version.
- preset contains name, author, description, tags, and optional stable preset_id.
- parameters maps stable parameter IDs to scalar numeric, boolean, or enum-string values. All registered parameters needed for a complete patch are serialized.
- JSON object keys are exported in stable lexical order; numbers use locale-independent decimal notation.
- Maximum file size is 1 MiB. Maximum string length for name is 128 characters, description 2048, each tag 64 characters, and at most 32 tags.
- Reject nonfinite numbers, control characters in names except ordinary whitespace, duplicate object keys, unsupported format, unsupported future schema, and invalid enums.
- Known numeric parameters are validated against the registry. Values outside allowed bounds reject the import; do not silently clamp user files.
- Unknown parameter IDs are ignored with a warning for forward compatibility. Unknown top-level fields are ignored only if schema version allows them; version 1 rejects unknown top-level fields.
- Parse into a temporary candidate. Only after all checks pass may one state transaction publish it.

## User file storage

Use the platform’s per-user application data directory under Circuit Drift Labs/Snaral/Presets. Do not use the current working directory or write into the plug-in install directory. Factory presets are read-only bundled assets. Saving a user preset never overwrites a factory preset. If a filename exists, require an explicit overwrite confirmation.

Export writes to a temporary sibling file, flushes it, and atomically renames it where supported. Failure preserves an existing file and leaves active state unchanged.

## Migration

Each schema version has a pure migration function from old JSON object to the next schema. Migrations are deterministic and tested against checked-in fixture files. Never reuse an ID for a new meaning. Missing fields receive version-specific documented defaults only.

## Acceptance criteria

1. Valid v1 example imports and exports with identical semantic values.
2. Duplicate keys, oversized file, malformed JSON, unsupported future schema, invalid enum, and out-of-range numeric values all fail safely.
3. Failed import leaves active patch, preset name, dirty state, and variations unchanged.
4. Existing destination is not overwritten without explicit confirmation.
5. Export is deterministic and survives an interrupted write without leaving a partial final file.
6. Every shipped schema version has a migration fixture and regression test.
