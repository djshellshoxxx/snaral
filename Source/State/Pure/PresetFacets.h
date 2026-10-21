#pragma once

// Browser facets derived from a preset's parameters (docs/specs/expansion-preset-browser.md):
// layer balance, motion type and texture. Pure functions of the snapshot, computed once at index time.

#include <string_view>

#include "SnapshotUtil.h"

namespace snaral
{
enum class LayerBalance : int { SubForward = 0, Balanced = 1, MidForward = 2 };

enum MotionBits : unsigned
{
    motionStatic = 1u,   // no active modulation
    motionContour = 2u,  // note contour (filter envelope) and/or Snarl Trajectory active
    motionSynced = 4u,   // LFO active and tempo-synced
    motionFree = 8u      // LFO active and free-running
};

enum TextureBits : unsigned
{
    textureClean = 1u, // neither noise nor metal
    textureNoise = 2u,
    textureMetal = 4u
};

struct PresetFacets
{
    LayerBalance layer = LayerBalance::Balanced;
    unsigned motion = motionStatic;
    unsigned texture = textureClean;

    bool operator== (const PresetFacets&) const = default;
};

inline PresetFacets deriveFacets (const ParamSnapshot& p) noexcept
{
    PresetFacets f;

    const float sub = p.percent (P::weight) * p.percent (P::sub_level);
    const float mid = p.percent (P::growl) * p.percent (P::mid_level);
    if (sub >= 1.5f * mid && sub > 0.001f)
        f.layer = LayerBalance::SubForward;
    else if (mid >= 1.5f * sub && mid > 0.001f)
        f.layer = LayerBalance::MidForward;
    else
        f.layer = LayerBalance::Balanced;

    const float macro = p.percent (P::motion);
    const bool contour = (macro > 0.0f && p[P::contour_depth] > 0.0f)
                         || (p.flag (P::traj_enabled) && p[P::traj_depth] > 0.0f);
    const bool lfo = macro > 0.0f && p[P::lfo_depth] > 0.0f;
    f.motion = 0;
    if (contour)
        f.motion |= motionContour;
    if (lfo)
        f.motion |= p.choice (P::lfo_mode) == 1 ? motionSynced : motionFree;
    if (f.motion == 0)
        f.motion = motionStatic;

    f.texture = 0;
    if (p[P::noise_level] > 0.0f)
        f.texture |= textureNoise;
    if (p[P::metal_level] > 0.0f)
        f.texture |= textureMetal;
    if (f.texture == 0)
        f.texture = textureClean;
    return f;
}

inline std::string_view toString (LayerBalance b) noexcept
{
    switch (b)
    {
        case LayerBalance::SubForward: return "sub-forward";
        case LayerBalance::Balanced:   return "balanced";
        case LayerBalance::MidForward: return "mid-forward";
    }
    return "balanced";
}
} // namespace snaral
