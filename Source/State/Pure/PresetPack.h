#pragma once

// Preset pack manifest ("manifest.json" next to the .snaralpreset files of a pack).
// docs/specs/expansion-preset-library.md. JUCE-free.

#include <string>
#include <string_view>
#include <vector>

#include "SnapshotUtil.h"

namespace snaral
{
inline constexpr std::string_view packFormatId = "snaral-pack";
inline constexpr int packSchemaVersion = 1;
inline constexpr std::string_view packManifestFileName = "manifest.json";
inline constexpr std::size_t maxPackManifestBytes = 256 * 1024;
inline constexpr std::size_t maxPackPresets = 512;

struct PackManifest
{
    std::string packId;            // [a-z0-9._-]{1,64}, also the install folder name
    std::string name;              // <=128 chars
    std::string version;           // free text <=32, e.g. "1.0.0"
    std::string author;            // <=128 chars
    std::string license;           // required, <=256 chars (SPDX id or short statement)
    std::string minPluginVersion;  // minimum Snaral version able to load every preset (informational, <=32)
    std::vector<std::string> files; // preset file names, relative, unique, ".snaralpreset"
    int presetCount = 0;           // must equal files.size()
};

struct PackManifestResult
{
    bool ok = false;
    PackManifest manifest;
    std::vector<Issue> errors;
    std::vector<Issue> warnings;
};

PackManifestResult parsePackManifest (std::string_view jsonText);

/// Deterministic manifest writer (lexical keys, same style as presets). False if the manifest is invalid.
bool writePackManifest (const PackManifest& manifest, std::string& out, std::vector<Issue>* errors = nullptr);

bool validPackId (std::string_view id) noexcept;
/// A bare file name with the preset extension: no separators, no "..", no control characters.
bool validPackFileName (std::string_view name) noexcept;
} // namespace snaral
