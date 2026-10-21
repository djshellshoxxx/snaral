#pragma once

// Snarl Trajectory: five-point body > peak > return curve evaluated as a smooth, bounded 0..1 control signal.
// JUCE-free, header-only. See docs/specs/expansion-snarl-trajectory.md for the exact formulas.

#include <algorithm>
#include <array>
#include <cmath>

#include "Dsp/DspUtil.h"
#include "Engine/EngineTypes.h"
#include "Parameters/ParameterTable.h"

namespace snaral
{
enum class TrajDest : int { Cutoff = 0, SourcePosition = 1, Both = 2 };

inline constexpr int trajNumPoints = 5;
inline constexpr float trajMinGap = 0.02f; // minimum x distance between neighbouring points (fraction of length)
inline constexpr double trajBeatsPerDivision[6] = { 0.25, 0.5, 1.0, 2.0, 4.0, 8.0 }; // 1/16 1/8 1/4 1/2 1/1 2/1 in quarter notes

struct TrajectoryParams
{
    bool enabled = false;
    bool sync = false;
    float lengthMs = 600.0f;
    int division = 3;
    TrajDest dest = TrajDest::Cutoff;
    float depth01 = 0.7f;
    float curve01 = 0.5f;
    bool loop = false;
    std::array<float, trajNumPoints> x { 0.0f, 0.15f, 0.40f, 0.70f, 1.0f };
    std::array<float, trajNumPoints> y { 0.20f, 0.90f, 0.50f, 0.30f, 0.20f };

    static TrajectoryParams from (const ParamSnapshot& s) noexcept
    {
        TrajectoryParams p;
        p.enabled = s.flag (P::traj_enabled);
        p.sync = s.choice (P::traj_time_mode) == 1;
        p.lengthMs = s[P::traj_length_ms];
        p.division = s.choice (P::traj_length_div);
        p.dest = static_cast<TrajDest> (std::clamp (s.choice (P::traj_dest), 0, 2));
        p.depth01 = s.percent (P::traj_depth);
        p.curve01 = s.percent (P::traj_curve);
        p.loop = s.flag (P::traj_loop);
        p.x = { 0.0f, s[P::traj_x2], s[P::traj_x3], s[P::traj_x4], 1.0f };
        p.y = { s[P::traj_y1], s[P::traj_y2], s[P::traj_y3], s[P::traj_y4], s[P::traj_y5] };
        return p;
    }
};

/// Pure curve: monotone-x safe, no overshoot outside the segment end values (hence inside 0..1).
class TrajectoryCurve
{
public:
    TrajectoryCurve() noexcept { rebuild(); }

    /// Clamps points to a valid layout: x1 = 0, x5 = 1 fixed; x2..x4 forced strictly increasing with
    /// at least trajMinGap between neighbours; y clamped to 0..1 (non-finite -> 0.5).
    static void sanitizePoints (std::array<float, trajNumPoints>& x, std::array<float, trajNumPoints>& y) noexcept
    {
        for (auto& v : y)
            v = std::isfinite (v) ? clampf (v, 0.0f, 1.0f) : 0.5f;
        for (auto& v : x)
            v = std::isfinite (v) ? v : 0.0f;
        const float g = trajMinGap;
        x[0] = 0.0f;
        x[4] = 1.0f;
        x[1] = clampf (x[1], g, 1.0f - 3.0f * g);
        x[2] = clampf (x[2], x[1] + g, 1.0f - 2.0f * g);
        x[3] = clampf (x[3], x[2] + g, 1.0f - g);
    }

    void set (const std::array<float, trajNumPoints>& x, const std::array<float, trajNumPoints>& y, float curve01) noexcept
    {
        x_ = x;
        y_ = y;
        sanitizePoints (x_, y_);
        curve_ = clampf (sanitize (curve01), 0.0f, 1.0f);
        rebuild();
    }

    const std::array<float, trajNumPoints>& xs() const noexcept { return x_; }
    const std::array<float, trajNumPoints>& ys() const noexcept { return y_; }

    /// u in [0,1] (clamped) -> 0..1.
    /// value = (1 - c) * linear + c * hermite, c = traj_curve/100; hermite uses Fritsch-Carlson monotone tangents.
    float eval (float u) const noexcept
    {
        u = clampf (sanitize (u), 0.0f, 1.0f);
        int i = 0;
        while (i < trajNumPoints - 2 && u > x_[static_cast<std::size_t> (i + 1)])
            ++i;
        const auto k = static_cast<std::size_t> (i);
        const float h = x_[k + 1] - x_[k];
        const float t = clampf ((u - x_[k]) / h, 0.0f, 1.0f);
        const float lin = y_[k] + t * (y_[k + 1] - y_[k]);
        const float t2 = t * t, t3 = t2 * t;
        const float herm = (2.0f * t3 - 3.0f * t2 + 1.0f) * y_[k] + (t3 - 2.0f * t2 + t) * h * m_[k]
                           + (-2.0f * t3 + 3.0f * t2) * y_[k + 1] + (t3 - t2) * h * m_[k + 1];
        const float lo = std::min (y_[k], y_[k + 1]), hi = std::max (y_[k], y_[k + 1]);
        return clampf (lin + curve_ * (clampf (herm, lo, hi) - lin), 0.0f, 1.0f);
    }

private:
    void rebuild() noexcept
    {
        std::array<float, 4> d {}, hh {};
        for (std::size_t i = 0; i < 4; ++i)
        {
            hh[i] = x_[i + 1] - x_[i];
            d[i] = (y_[i + 1] - y_[i]) / hh[i];
        }
        m_[0] = d[0];
        m_[4] = d[3];
        for (std::size_t k = 1; k < 4; ++k)
        {
            if (d[k - 1] * d[k] <= 0.0f)
                m_[k] = 0.0f;
            else
            {
                const float w1 = 2.0f * hh[k] + hh[k - 1];
                const float w2 = hh[k] + 2.0f * hh[k - 1];
                m_[k] = (w1 + w2) / (w1 / d[k - 1] + w2 / d[k]);
            }
        }
    }

    std::array<float, trajNumPoints> x_ { 0.0f, 0.15f, 0.40f, 0.70f, 1.0f };
    std::array<float, trajNumPoints> y_ { 0.20f, 0.90f, 0.50f, 0.30f, 0.20f };
    std::array<float, trajNumPoints> m_ {};
    float curve_ = 0.5f;
};

/// Playback of the curve. Output 0..1 (depth, destination and Motion routing are in MotionRouting).
class Trajectory
{
public:
    static constexpr const char* fallbackMessage = "Host tempo unavailable - using ms length";

    void prepare (double sampleRate) noexcept
    {
        sr_ = std::isfinite (sampleRate) && sampleRate > 1000.0 ? sampleRate : 44100.0;
        declickSamples_ = std::max (1, static_cast<int> (std::lround (0.002 * sr_)));
        setParams (TrajectoryParams {});
        reset();
    }

    void setParams (const TrajectoryParams& p) noexcept
    {
        params_ = p;
        params_.lengthMs = clampf (sanitize (p.lengthMs), 50.0f, 4000.0f);
        params_.division = std::clamp (p.division, 0, 5);
        curve_.set (p.x, p.y, p.curve01);
        updateLength();
    }

    void setHostTiming (const HostTiming& t) noexcept
    {
        bpmValid_ = t.bpmValid && std::isfinite (t.bpm) && t.bpm > 0.0;
        bpm_ = bpmValid_ ? std::clamp (t.bpm, 10.0, 999.0) : 120.0;
        updateLength();
    }

    /// Deterministic reset: idle, output rests at the first point value.
    void reset() noexcept
    {
        u_ = 0.0;
        running_ = false;
        declickOffset_ = 0.0f;
        declickLeft_ = 0;
        last_ = curve_.eval (0.0f);
    }

    /// retrigger=false is the legato hook (slurred note while running: keep playing). A note-on from idle
    /// always starts. A retrigger restarts at u = 0 with a 2 ms click-safe offset decay.
    void noteOn (bool retrigger = true) noexcept
    {
        if (! retrigger && running_)
            return;
        const float first = curve_.eval (0.0f);
        declickOffset_ = running_ ? last_ - first : 0.0f;
        declickLeft_ = declickOffset_ != 0.0f ? declickSamples_ : 0;
        u_ = 0.0;
        running_ = true;
    }

    bool usingFallbackLength() const noexcept { return params_.sync && ! bpmValid_; }
    double lengthSamples() const noexcept { return lengthSamples_; }
    double lengthMsEffective() const noexcept { return lengthSamples_ * 1000.0 / sr_; }
    bool isRunning() const noexcept { return running_; }
    const TrajectoryCurve& curve() const noexcept { return curve_; }
    const TrajectoryParams& params() const noexcept { return params_; }

    /// One sample. Before the first note-on this returns the first point value.
    float process() noexcept
    {
        if (! running_)
            return last_;
        float v = curve_.eval (static_cast<float> (u_));
        if (declickLeft_ > 0)
        {
            v += declickOffset_ * static_cast<float> (declickLeft_) / static_cast<float> (declickSamples_);
            --declickLeft_;
        }
        v = clampf (v, 0.0f, 1.0f);
        last_ = v;
        u_ += 1.0 / lengthSamples_;
        if (u_ >= 1.0)
        {
            if (params_.loop)
            {
                u_ -= std::floor (u_);
                const float jump = v - curve_.eval (static_cast<float> (u_)); // click-safe wrap if y1 != y5
                if (std::fabs (jump) > 1.0e-6f)
                {
                    declickOffset_ = jump;
                    declickLeft_ = declickSamples_;
                }
            }
            else
                u_ = 1.0; // hold the return point
        }
        return v;
    }

private:
    void updateLength() noexcept
    {
        double ms = params_.lengthMs;
        if (params_.sync && bpmValid_)
            ms = trajBeatsPerDivision[params_.division] * 60000.0 / bpm_;
        lengthSamples_ = std::max (1.0, std::round (ms * 0.001 * sr_));
    }

    double sr_ = 44100.0, u_ = 0.0, lengthSamples_ = 26460.0, bpm_ = 120.0;
    bool bpmValid_ = false, running_ = false;
    int declickSamples_ = 88, declickLeft_ = 0;
    float declickOffset_ = 0.0f, last_ = 0.0f;
    TrajectoryParams params_;
    TrajectoryCurve curve_;
};
} // namespace snaral
