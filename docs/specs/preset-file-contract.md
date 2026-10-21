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

## Schema v1 normative details

Implemented by `Source/State/Pure/PresetFormat.h/.cpp` (writer, strict parser, migration chain) on top of the strict JSON reader in `Json.h`. The parser is pure: it returns a `PresetData` plus errors/warnings and never touches live state.

### Document

```json
{
  "format": "snaral-preset",
  "parameters": { "<id>": <value>, "...": "..." },
  "plugin": { "name": "Snaral", "state_version": "0.2.0" },
  "preset": { "author": "", "description": "", "name": "Example Bass", "preset_id": "user.example", "tags": ["sub", "static"] },
  "schema_version": 1
}
```

- Keys are written in lexical (byte order) order at every level, so the file starts with `format` and ends with `schema_version`. Readers accept any order.
- **Parameter values are DOMAIN units**, the same units as the registry and `ParamSnapshot`: `weight` 0 to 100, `filter_cutoff_hz` in Hz, `contour_decay_ms` in ms, `output_trim_db` in dB. They are not host-normalised 0 to 1 values. (The illustrative example in the initial developer spec shows `"weight": 0.72`; that example is not normative. Under this contract `0.72` would mean 0.72 percent. The spec example is superseded by the domain-unit examples in `tests/fixtures/`.)
- Choice parameters are the registry choice name as a string (`"saw_stack"`, `"1/8t"`). Numeric indices are rejected. Bool parameters are JSON `true` / `false` (numbers are rejected). Float and integer parameters are JSON numbers; integer-typed parameters (`pfInteger`) must be whole numbers (`36` or `36.0`, not `36.5`).
- Only sound parameters (`ParamDef::inPreset()`) are serialized. Utility parameters (`bypass`, `layer_solo`, `sub_mute`, `mid_mute`) are never written and, if present in a hand-edited file, are ignored with a warning so a preset can never change them.
- `plugin.name` is a string (a different name only warns). `plugin.state_version` is a string, informational ("version of the plug-in that wrote the file"), at most 32 characters, never used for validation.
- `preset.name` is required (1 to 128 characters, not blank). `author` (128), `description` (2048, may contain `\n` and `\t`), `tags` (at most 32, each 1 to 64 characters, case-insensitively unique) and `preset_id` (`[A-Za-z0-9._-]`, 1 to 64 characters) are optional on read: absent `author`/`description` read as empty and absent `tags` as an empty list. The writer always emits `author`, `description`, `name` and `tags`, and emits `preset_id` only when set. Lengths count Unicode code points; text must be valid UTF-8; control characters are rejected (names, authors, tags and ids may not contain any, including tab and newline).
- By convention the first tag is the browser category (sound role). Further tags describe movement (`static`, `contour`, `synced`, `free`), layer balance (`sub-forward`, `balanced`, `mid-forward`), texture (`clean`, `noise`, `metal`) and performance mode (`mono`, `poly`, `legato`). See factory-presets.md.

### Numbers

- Writer: shortest decimal text that round-trips the stored `float` exactly, plain decimal notation (never an exponent), `.` as separator regardless of the C locale, integral values without a fractional part (`100`, `-24`, `0.02`, `0.00001`), `-0` written as `0`.
- Reader: strict RFC 8259 number grammar (no leading zeros, no `+`, no hex, no `NaN`/`Infinity`, no comments). The token is converted with `strtof` after locale normalisation, so values keep float rounding. Overflow to infinity (for example `1e999`) is rejected as non-finite. A value outside `[min, max]` rejects the file; it is never clamped.

### Reader behaviour

| Condition | Result |
|---|---|
| Empty file, not JSON, trailing content, truncated, invalid UTF-8, unescaped control character, lone surrogate | Error (not valid JSON) |
| File larger than 1 MiB, nesting deeper than 8 | Error |
| Duplicate object key at any level | Error naming the key |
| Root not an object, `format` missing or not `snaral-preset` | Error |
| `schema_version` missing, not an integer, below 1 | Error |
| `schema_version` above the highest known (1) | Error "unsupported future schema_version" |
| Unknown top-level field (schema v1) | Error |
| Unknown field inside `plugin` or `preset` | Warning, ignored |
| Unknown parameter id | Warning, ignored, listed in `ignoredParameters` |
| Retired id | Warning, ignored. Renamed id: warning, mapped to the new id |
| Utility parameter id | Warning, ignored |
| Wrong JSON type, bad choice name, non-finite, out of range, non-integral integer parameter | Error naming `parameters.<id>` |
| Sound parameter absent | Registry default is used, warning (one summary), listed in `defaultedParameters`. With `requireComplete` (factory and pack content) this is an error |
| No known sound parameter at all | Error |
| One leading UTF-8 BOM | Tolerated |

All errors found are reported (not only the first). On any error nothing is returned for application.

### Defaults for parameters added after v1

"Missing fields receive version-specific documented defaults only": for schema v1 the default of a sound parameter that a file does not contain is the registry default (`ParameterList.def`) of the build reading the file. That is the documented default. Because registry growth is append-only, a v1 file written by an earlier build loads in a later build with the new parameters at their defaults, and the next export contains them.

### Migration chain

`parsePreset` reads `schema_version`, then runs `detail::runPresetMigrations` which applies pure steps `PresetMigration { fromVersion, apply(json::Value&, error) }` in order until the current schema is reached (an absent step or a failing step rejects the file). The shipped table is empty because v1 is the first and current schema. When v2 is introduced: add the step `1 -> 2`, add a frozen v1 fixture test next to `tests/fixtures/preset_v1_*.snaralpreset`, bump `presetSchemaVersion`. The chain itself is covered with injected steps in `PresetFormatTests`.

### Library and pack files

Packs reuse this format; see expansion-preset-library.md for the manifest. Browser state (favourites, history) is a separate small file and is not a preset.

### Safe file operations

- `fileio::writeFileAtomic`: unique hidden temporary sibling (`.<name>.<stamp>.tmp`) created with exclusive mode, data flushed and `fsync`ed, then renamed over the destination (the directory is synced on POSIX). Any failure removes the temporary file and leaves the existing destination byte-for-byte untouched. A directory in the way, a missing parent that cannot be created and a failed rename are all reported. Tests inject a failure between "temp complete" and "rename".
- File names for user presets come from `sanitiseFileName(name)`: path separators and `<>:"|?*` become `_`, control characters are dropped, leading and trailing dots/spaces are trimmed, Windows device names get a `_` prefix, the name is cut to 96 bytes on a UTF-8 boundary and an empty result becomes `Untitled`. Collisions are detected ASCII-case-insensitively (default Windows and macOS volumes) and require explicit overwrite confirmation, in which case the existing file name is kept.
- Reading a candidate checks the size limit before reading it all.

## Acceptance criteria

1. Valid v1 example imports and exports with identical semantic values.
2. Duplicate keys, oversized file, malformed JSON, unsupported future schema, invalid enum, and out-of-range numeric values all fail safely.
3. Failed import leaves active patch, preset name, dirty state, and variations unchanged.
4. Existing destination is not overwritten without explicit confirmation.
5. Export is deterministic and survives an interrupted write without leaving a partial final file.
6. Every shipped schema version has a migration fixture and regression test.
