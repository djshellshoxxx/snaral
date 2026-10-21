#pragma once

// Helpers over ParamSnapshot shared by presets, variations, A/B and undo. JUCE-free, header-only.
//
// Vocabulary
//   "sound parameter"  = a parameter with ParamDef::inPreset() == true. These are the values stored in
//                        preset files, variation slots, A/B snapshots and undo deltas.
//   "utility parameter" = pfNoPreset (bypass, layer solo, mutes). Persisted only in host project state;
//                        never changed by preset load, variation recall, A/B switch or undo.

#include <cmath>
#include <string>
#include <vector>

#include "Parameters/ParameterTable.h"

namespace snaral
{
/// A problem found while validating external data. `path` locates it (e.g. "parameters.filter_cutoff_hz").
struct Issue
{
    std::string path;
    std::string message;
};

inline std::string describe (const Issue& issue)
{
    return issue.path.empty() ? issue.message : issue.path + ": " + issue.message;
}

inline constexpr float snapshotTolerance = 1e-6f;

/// Copies every sound parameter from `src` into `dst`; utility parameters in `dst` are untouched.
inline void copySoundParams (const ParamSnapshot& src, ParamSnapshot& dst) noexcept
{
    for (int i = 0; i < parameterCount; ++i)
        if (parameterTable[static_cast<std::size_t> (i)].inPreset())
            dst.v[static_cast<std::size_t> (i)] = src.v[static_cast<std::size_t> (i)];
}

/// `base` with sound parameters replaced by those of `sound`.
inline ParamSnapshot withSoundParams (const ParamSnapshot& base, const ParamSnapshot& sound) noexcept
{
    ParamSnapshot out = base;
    copySoundParams (sound, out);
    return out;
}

/// Equality of the sound parameters. Floats compare in normalised host units (|diff| <= tolerance, the
/// 1e-6 round-trip criterion of the specs); integer/choice/bool parameters compare exactly.
inline bool soundParamsEqual (const ParamSnapshot& a, const ParamSnapshot& b,
                              float tolerance = snapshotTolerance) noexcept
{
    for (int i = 0; i < parameterCount; ++i)
    {
        const auto& d = parameterTable[static_cast<std::size_t> (i)];
        if (! d.inPreset())
            continue;
        const float x = a.v[static_cast<std::size_t> (i)];
        const float y = b.v[static_cast<std::size_t> (i)];
        if (x == y)
            continue;
        if (d.isInteger() || std::abs (d.toNormalized (x) - d.toNormalized (y)) > tolerance)
            return false;
    }
    return true;
}

/// Bit-exact equality of every parameter (sound and utility).
inline bool snapshotsIdentical (const ParamSnapshot& a, const ParamSnapshot& b) noexcept
{
    for (std::size_t i = 0; i < a.v.size(); ++i)
        if (a.v[i] != b.v[i])
            return false;
    return true;
}

/// True when every value is finite and inside its registry range (and integral where required).
inline bool snapshotIsValid (const ParamSnapshot& s) noexcept
{
    for (int i = 0; i < parameterCount; ++i)
    {
        const auto& d = parameterTable[static_cast<std::size_t> (i)];
        const float x = s.v[static_cast<std::size_t> (i)];
        if (! std::isfinite (x) || x < d.minimum || x > d.maximum)
            return false;
        if (d.isInteger() && x != std::round (x))
            return false;
    }
    return true;
}

/// Clamps/quantises every value into its domain (non-finite -> default).
inline ParamSnapshot sanitised (const ParamSnapshot& s) noexcept
{
    ParamSnapshot out;
    for (int i = 0; i < parameterCount; ++i)
        out.v[static_cast<std::size_t> (i)] =
            parameterTable[static_cast<std::size_t> (i)].clampToDomain (s.v[static_cast<std::size_t> (i)]);
    return out;
}

/// Indices of sound parameters whose values differ (exactly) between the snapshots.
inline std::vector<int> differingSoundParams (const ParamSnapshot& a, const ParamSnapshot& b)
{
    std::vector<int> out;
    for (int i = 0; i < parameterCount; ++i)
        if (parameterTable[static_cast<std::size_t> (i)].inPreset()
            && a.v[static_cast<std::size_t> (i)] != b.v[static_cast<std::size_t> (i)])
            out.push_back (i);
    return out;
}
} // namespace snaral
