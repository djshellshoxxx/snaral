#pragma once

// Locale-independent number <-> text helpers used by every Snaral text format
// (preset JSON, browser-state JSON, project-state value strings).
// JUCE-free; std only.

#include <string>
#include <string_view>

namespace snaral::text
{
/// Shortest decimal text that parses back to exactly the same float. Plain decimal notation (never an
/// exponent), '.' as decimal separator regardless of the C locale, "-0" is written as "0".
/// Integral values print without a fractional part ("100"). The input must be finite (otherwise "0").
std::string formatFloat (float value);

/// Parses a decimal/JSON number token (no surrounding whitespace). Locale independent.
/// Returns false when the token is empty, has trailing garbage, or the result is not finite.
bool parseFloat (std::string_view token, float& out) noexcept;
bool parseDouble (std::string_view token, double& out) noexcept;

/// Parses a base-10 integer token (optional leading '-'); false on overflow or garbage.
bool parseInt (std::string_view token, long long& out) noexcept;
} // namespace snaral::text
