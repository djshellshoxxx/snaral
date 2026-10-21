#pragma once

// Snaral preset file format (".snaralpreset", schema v1): deterministic writer, strict validating parser,
// migration chain. JUCE-free. Normative description: docs/specs/preset-file-contract.md.

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "Json.h"
#include "SnapshotUtil.h"

namespace snaral
{
inline constexpr int presetSchemaVersion = 1;
inline constexpr std::string_view presetFormatId = "snaral-preset";
inline constexpr std::string_view presetPluginName = "Snaral";
/// Informational "plugin version that wrote the file"; never used for validation.
inline constexpr std::string_view presetWriterStateVersion = "0.2.0";
inline constexpr std::string_view presetFileExtension = ".snaralpreset";

inline constexpr std::size_t maxPresetFileBytes = 1024 * 1024;
inline constexpr std::size_t maxPresetNameChars = 128;
inline constexpr std::size_t maxPresetAuthorChars = 128;
inline constexpr std::size_t maxPresetDescriptionChars = 2048;
inline constexpr std::size_t maxPresetTagChars = 64;
inline constexpr std::size_t maxPresetTags = 32;
inline constexpr std::size_t maxPresetIdChars = 64;
inline constexpr std::size_t maxStateVersionChars = 32;

/// One preset: metadata plus the value of every parameter. Only sound parameters (inPreset()) are
/// serialised; utility parameters in `params` are ignored by the writer and stay at their defaults after
/// parsing. Apply with copySoundParams()/withSoundParams() so utility state is never overwritten.
struct PresetData
{
    std::string name;
    std::string author;
    std::string description;
    std::string presetId; // optional stable id, [A-Za-z0-9._-]{1,64}; empty = none
    std::vector<std::string> tags;
    ParamSnapshot params;
};

struct PresetParseOptions
{
    /// Reject files that omit any sound parameter (used for factory/pack content). User imports leave
    /// this false: absent parameters take their registry defaults and are listed in the result.
    bool requireComplete = false;
};

struct PresetParseResult
{
    bool ok = false;
    PresetData data;                              // meaningful only when ok
    int sourceSchemaVersion = 0;                  // schema_version found in the file (0 if unreadable)
    bool migrated = false;                        // true when a migration step ran
    std::string pluginVersion;                    // plugin.state_version of the writer (informational)
    std::vector<Issue> errors;                    // non-empty iff !ok
    std::vector<Issue> warnings;                  // ignored/unknown/defaulted items; never fatal
    std::vector<std::string> defaultedParameters; // sound parameters absent from the file (registry default used)
    std::vector<std::string> ignoredParameters;   // unknown, retired or non-preset ids that were skipped

    std::string firstError() const { return errors.empty() ? std::string() : describe (errors.front()); }
};

/// Strict parse + validation of a complete candidate. Pure: never touches any state, so a failure leaves the
/// caller's current patch untouched by construction. Accepts at most maxPresetFileBytes.
PresetParseResult parsePreset (std::string_view jsonText, const PresetParseOptions& options = {});

/// Deterministic export (schema v1): lexically ordered keys at every level, 2-space indentation, one
/// parameter per line, shortest round-trip numbers, '\n' line endings and a trailing newline.
/// Returns false (and fills `errors`) when the metadata would not pass the reader's own validation.
bool writePreset (const PresetData& preset, std::string& out, std::vector<Issue>* errors = nullptr);

/// Convenience: writePreset() that returns an empty string on failure.
std::string writePresetOrEmpty (const PresetData& preset);

/// Checks name/author/description/tags/presetId limits and character rules (shared by reader and writer).
bool validatePresetMetadata (const PresetData& preset, std::vector<Issue>& errors);

/// Serialised JSON text of a single parameter value (number / true|false / quoted choice name).
std::string parameterValueToJson (const ParamDef& def, float value);

// ---------------------------------------------------------------------------------------------------------
// Migration chain
// ---------------------------------------------------------------------------------------------------------

/// Pure step that rewrites a parsed JSON document of schema `fromVersion` into `fromVersion + 1`
/// (including updating "schema_version"). Returns false and fills `error` to reject the file.
struct PresetMigration
{
    int fromVersion = 0;
    bool (*apply) (json::Value& root, std::string& error) = nullptr;
};

namespace detail
{
/// Runs steps from `fromVersion` up to `toVersion`. Fails if a step is missing or reports an error.
bool runPresetMigrations (json::Value& root, int fromVersion, int toVersion,
                          std::span<const PresetMigration> steps, std::string& error);
}

/// IDs that were renamed or retired by a registry change (empty for the first release). Renamed ids are
/// mapped with a warning; retired ids are dropped with a warning; neither is ever reused for a new meaning.
struct RenamedParameter { std::string_view from; std::string_view to; };
std::span<const RenamedParameter> renamedParameterIds() noexcept;
std::span<const std::string_view> retiredParameterIds() noexcept;
} // namespace snaral
