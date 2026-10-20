#pragma once

#include <algorithm>
#include <cmath>

namespace snaral
{
inline float normalizedMacroValue (float value) noexcept
{
    if (! std::isfinite (value))
        return 0.0f;
    return std::clamp (value, 0.0f, 1.0f);
}

inline float weightSubMultiplier (float weight) noexcept
{
    const auto x = normalizedMacroValue (weight);
    return x * x;
}

inline float growlMidMultiplier (float growl) noexcept
{
    const auto x = normalizedMacroValue (growl);
    return x * x;
}

inline float growlSourceBBias (float growl) noexcept
{
    return 0.20f * normalizedMacroValue (growl);
}

inline float growlSourceBlend (float advancedBlend, float growl) noexcept
{
    const auto blend = normalizedMacroValue (advancedBlend);
    return std::clamp (blend + growlSourceBBias (growl), 0.0f, 1.0f);
}

inline float biteFilterPeakOffsetSemitones (float bite) noexcept
{
    return 18.0f * normalizedMacroValue (bite);
}

inline float biteDriveOffsetDb (float bite) noexcept
{
    return 12.0f * normalizedMacroValue (bite);
}

inline float motionDepthMultiplier (float motion) noexcept
{
    return normalizedMacroValue (motion);
}

inline float spaceWidthPercent (float space) noexcept
{
    return 120.0f * normalizedMacroValue (space);
}
} // namespace snaral
