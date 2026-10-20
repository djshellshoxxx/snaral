#pragma once

#include <array>
#include <cmath>
#include <string_view>

namespace snaral
{
struct ParameterSpec
{
    std::string_view id;
    std::string_view displayName;
    float minimum;
    float maximum;
    float defaultValue;

    float normalizedToDomain (float normalized) const noexcept
    {
        if (! std::isfinite (normalized))
            normalized = (defaultValue - minimum) / (maximum - minimum);
        const auto safe = normalized < 0.0f ? 0.0f : (normalized > 1.0f ? 1.0f : normalized);
        return minimum + safe * (maximum - minimum);
    }

    float domainToNormalized (float value) const noexcept
    {
        if (! std::isfinite (value))
            value = defaultValue;
        const auto safe = value < minimum ? minimum : (value > maximum ? maximum : value);
        return (safe - minimum) / (maximum - minimum);
    }

    float defaultNormalized() const noexcept
    {
        return domainToNormalized (defaultValue);
    }
};

inline constexpr std::array<ParameterSpec, 5> macroParameterSpecs {{
    { "weight", "Weight", 0.0f, 100.0f, 75.0f },
    { "growl", "Growl", 0.0f, 100.0f, 65.0f },
    { "bite", "Bite", 0.0f, 100.0f, 20.0f },
    { "motion", "Motion", 0.0f, 100.0f, 0.0f },
    { "space", "Space", 0.0f, 100.0f, 0.0f },
}};

inline const ParameterSpec* findMacroParameter (std::string_view id) noexcept
{
    for (const auto& spec : macroParameterSpecs)
        if (spec.id == id)
            return &spec;
    return nullptr;
}
} // namespace snaral
