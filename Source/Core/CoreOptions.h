#pragma once

// Persisted UI options model (per installation, never in presets or host state). JUCE-free.
// Named CoreOptions so it cannot collide with a UI-kit type. Spec: docs/specs/cdl-theme-options.md.
//
// Text format (key=value lines, version 1):
//   snaral-options 1
//   theme=cdl
//   tooltips=1
//   animations=1
//   scale=1
//   onboarding_seen=0
// Unknown keys are ignored (forward compatible); each invalid value falls back to its default and is reported,
// the other values are still loaded.

#include <string>
#include <string_view>
#include <vector>

namespace snaral
{
struct CoreOptions
{
    static constexpr float minScale = 0.75f, maxScale = 2.0f;

    std::string themeId = "cdl";   // [a-z0-9_-]{1,32}; whether the id exists is the UI kit's concern
    bool tooltips = true;
    bool animations = true;
    float scale = 1.0f;            // UI scale 0.75 .. 2.0
    bool onboardingSeen = false;

    bool operator== (const CoreOptions&) const = default;

    static bool isValidThemeId (std::string_view id) noexcept;
    static bool isValidScale (float s) noexcept;
    /// Empty = valid, else a list of problems.
    std::vector<std::string> validate() const;
    /// Copy with every invalid field replaced by its default.
    CoreOptions sanitised() const;
};

struct OptionsParseResult
{
    CoreOptions options;                 // always usable
    std::vector<std::string> problems;   // invalid values / malformed lines that were replaced by defaults
    bool headerOk = false;
};

std::string toText (const CoreOptions& o);
OptionsParseResult optionsFromText (std::string_view text);
} // namespace snaral
