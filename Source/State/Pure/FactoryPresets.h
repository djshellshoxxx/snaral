#pragma once

// Factory presets embedded at build time. JUCE-free.
//
// Source of truth: Resources/presets/*.json (written by tools/gen_presets.py). CMake turns those files
// into the generated translation unit <build>/generated/FactoryPresetBlobs.cpp (see the delimited block
// in CMakeLists.txt) which defines the symbols declared below. Nothing here touches the file system.

#include <cstddef>
#include <string>
#include <vector>

#include "PresetFormat.h"

namespace snaral::embedded
{
struct PresetBlob
{
    const char* fileName;        // e.g. "01_foundation.json"
    const unsigned char* data;   // exact file bytes
    std::size_t size;
};

/// Defined by the CMake-generated translation unit (library snaral_state_pure).
extern const PresetBlob factoryPresetBlobs[];
extern const int factoryPresetBlobCount;
} // namespace snaral::embedded

namespace snaral
{
struct FactoryPreset
{
    std::string fileName;
    PresetData data;
    std::vector<Issue> warnings;
};

struct FactoryPresetSet
{
    std::vector<FactoryPreset> presets;  // successfully parsed, in file-name order
    std::vector<Issue> problems;         // files that failed validation (path = file name); must be empty in a release build
};

/// Parses (strictly, requireComplete) and caches the embedded set on first use. Thread-safe.
const FactoryPresetSet& factoryPresets();

/// Id used by the library/browser for a factory preset: "factory:" + preset_id.
std::string factoryEntryId (const PresetData& preset);
} // namespace snaral
