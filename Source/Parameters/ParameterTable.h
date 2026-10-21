#pragma once

// JUCE-independent master parameter table, generated from ParameterList.def.
// DSP, UI, presets, tests and host glue all index parameters through snaral::P::Index.

#include <array>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <string_view>

namespace snaral
{
enum class ParamType : std::uint8_t { Float, Choice, Bool };
enum class ParamScale : std::uint8_t { Linear, Log };

enum ParamFlags : unsigned
{
    pfNone = 0,
    pfNoPreset = 1,      // persisted in project state, excluded from preset JSON
    pfNoAutomation = 2,  // not exposed as a host-automatable parameter
    pfInteger = 4        // float parameter quantised to whole units
};

inline constexpr int maxChoices = 24;

struct ParamDef
{
    std::string_view id;
    std::string_view name;
    std::string_view group;
    std::string_view unit;
    ParamType type = ParamType::Float;
    ParamScale scale = ParamScale::Linear;
    float minimum = 0.0f;
    float maximum = 1.0f;
    float defaultValue = 0.0f;
    unsigned flags = pfNone;
    int numChoices = 0;
    std::array<std::string_view, maxChoices> choices {};

    constexpr bool inPreset() const noexcept { return (flags & pfNoPreset) == 0; }
    constexpr bool automatable() const noexcept { return (flags & pfNoAutomation) == 0; }
    constexpr bool isInteger() const noexcept { return type != ParamType::Float || (flags & pfInteger) != 0; }

    /// Domain value (e.g. Hz, %, choice index) -> host-normalised [0,1].
    float toNormalized (float value) const noexcept
    {
        if (! std::isfinite (value))
            value = defaultValue;
        value = value < minimum ? minimum : (value > maximum ? maximum : value);
        if (maximum <= minimum)
            return 0.0f;
        if (scale == ParamScale::Log && minimum > 0.0f)
            return std::log (value / minimum) / std::log (maximum / minimum);
        return (value - minimum) / (maximum - minimum);
    }

    /// Host-normalised [0,1] -> domain value (quantised for integer/choice/bool types).
    float fromNormalized (float normalized) const noexcept
    {
        if (! std::isfinite (normalized))
            return defaultValue;
        normalized = normalized < 0.0f ? 0.0f : (normalized > 1.0f ? 1.0f : normalized);
        float value;
        if (scale == ParamScale::Log && minimum > 0.0f)
            value = minimum * std::exp (normalized * std::log (maximum / minimum));
        else
            value = minimum + normalized * (maximum - minimum);
        value = value < minimum ? minimum : (value > maximum ? maximum : value);
        return isInteger() ? std::round (value) : value;
    }

    float clampToDomain (float value) const noexcept
    {
        if (! std::isfinite (value))
            return defaultValue;
        value = value < minimum ? minimum : (value > maximum ? maximum : value);
        return isInteger() ? std::round (value) : value;
    }
};

namespace detail
{
constexpr ParamDef makeFloat (std::string_view id, std::string_view name, std::string_view group,
                              float mn, float mx, float def, std::string_view unit,
                              ParamScale scale, unsigned flags) noexcept
{
    ParamDef d;
    d.id = id; d.name = name; d.group = group; d.unit = unit;
    d.type = ParamType::Float; d.scale = scale;
    d.minimum = mn; d.maximum = mx; d.defaultValue = def; d.flags = flags;
    return d;
}

constexpr ParamDef makeBool (std::string_view id, std::string_view name, std::string_view group,
                             bool def, unsigned flags) noexcept
{
    ParamDef d;
    d.id = id; d.name = name; d.group = group;
    d.type = ParamType::Bool; d.minimum = 0.0f; d.maximum = 1.0f;
    d.defaultValue = def ? 1.0f : 0.0f; d.flags = flags;
    return d;
}

constexpr ParamDef makeChoice (std::string_view id, std::string_view name, std::string_view group,
                               int defIndex, unsigned flags,
                               std::initializer_list<std::string_view> list) noexcept
{
    ParamDef d;
    d.id = id; d.name = name; d.group = group;
    d.type = ParamType::Choice;
    int n = 0;
    for (const auto c : list)
        if (n < maxChoices)
            d.choices[static_cast<std::size_t> (n++)] = c;
    d.numChoices = n;
    d.minimum = 0.0f; d.maximum = static_cast<float> (n > 0 ? n - 1 : 0);
    d.defaultValue = static_cast<float> (defIndex); d.flags = flags;
    return d;
}
} // namespace detail

namespace P
{
#define SNARAL_FLOAT(id, ...) id,
#define SNARAL_BOOL(id, ...) id,
#define SNARAL_CHOICE(id, ...) id,
enum Index : int
{
#include "ParameterList.def"
    count
};
#undef SNARAL_FLOAT
#undef SNARAL_BOOL
#undef SNARAL_CHOICE
} // namespace P

inline constexpr int parameterCount = static_cast<int> (P::count);

#define SNARAL_FLOAT(id, name, group, mn, mx, def, unit, scale, flags) \
    detail::makeFloat (#id, name, group, mn, mx, def, unit, ParamScale::scale, flags),
#define SNARAL_BOOL(id, name, group, def, flags) detail::makeBool (#id, name, group, def, flags),
#define SNARAL_CHOICE(id, name, group, def, flags, ...) \
    detail::makeChoice (#id, name, group, def, flags, { __VA_ARGS__ }),
inline constexpr std::array<ParamDef, static_cast<std::size_t> (parameterCount)> parameterTable {{
#include "ParameterList.def"
}};
#undef SNARAL_FLOAT
#undef SNARAL_BOOL
#undef SNARAL_CHOICE

inline constexpr const ParamDef& paramDef (P::Index i) noexcept
{
    return parameterTable[static_cast<std::size_t> (i)];
}

/// Returns the index of the parameter with this ID, or -1.
inline int findParameterIndex (std::string_view id) noexcept
{
    for (int i = 0; i < parameterCount; ++i)
        if (parameterTable[static_cast<std::size_t> (i)].id == id)
            return i;
    return -1;
}

/// Coherent set of all parameter values in DOMAIN units (Hz, %, dB, choice index as float).
/// This is the single DSP-facing contract: the processor fills one per block, DSP never touches APVTS.
struct ParamSnapshot
{
    std::array<float, static_cast<std::size_t> (parameterCount)> v {};

    constexpr ParamSnapshot() noexcept
    {
        for (int i = 0; i < parameterCount; ++i)
            v[static_cast<std::size_t> (i)] = parameterTable[static_cast<std::size_t> (i)].defaultValue;
    }

    constexpr float operator[] (P::Index i) const noexcept { return v[static_cast<std::size_t> (i)]; }
    constexpr float& operator[] (P::Index i) noexcept { return v[static_cast<std::size_t> (i)]; }
    constexpr int choice (P::Index i) const noexcept { return static_cast<int> ((*this)[i] + 0.5f); }
    constexpr bool flag (P::Index i) const noexcept { return (*this)[i] >= 0.5f; }
    constexpr float percent (P::Index i) const noexcept { return (*this)[i] * 0.01f; }
};
} // namespace snaral
