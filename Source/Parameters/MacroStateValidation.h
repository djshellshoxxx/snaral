#pragma once

#include "ParameterRegistry.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <string_view>

namespace snaral
{
struct MacroStateCandidate
{
    std::array<float, macroParameterSpecs.size()> values {};
    std::array<bool, macroParameterSpecs.size()> assigned {};

    bool assign (std::string_view id, float value) noexcept
    {
        if (! std::isfinite (value))
            return false;

        for (std::size_t i = 0; i < macroParameterSpecs.size(); ++i)
        {
            const auto& spec = macroParameterSpecs[i];
            if (spec.id != id)
                continue;

            if (assigned[i] || value < spec.minimum || value > spec.maximum)
                return false;

            values[i] = value;
            assigned[i] = true;
            return true;
        }

        return false;
    }

    bool isComplete() const noexcept
    {
        for (const auto isAssigned : assigned)
            if (! isAssigned)
                return false;
        return true;
    }
};
} // namespace snaral
