#pragma once

// Test rig for the full SnaralEngine (header-only; include AFTER EngineOutputTestUtil.h, which provides CHECK and the
// allocation counter). Mimics the processor: parameters applied once per host block, MIDI events applied at sample offsets.

#include "Engine/SnaralEngine.h"
#include "Dsp/Oversampler.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace rig
{
using namespace snaral;

inline float mapPercent (float percent, const ParamDef& d) { return d.clampToDomain (d.minimum + percent * (d.maximum - d.minimum)); }

struct Event
{
    int at; // sample position relative to the start of the run() call
    EngineMidiEvent e;
};

inline EngineMidiEvent noteOnEv (int note, int vel = 100)
{
    EngineMidiEvent e; e.type = EngineMidiEvent::Type::NoteOn; e.data1 = static_cast<std::uint8_t> (note); e.data2 = static_cast<std::uint8_t> (vel); return e;
}
inline EngineMidiEvent noteOffEv (int note)
{
    EngineMidiEvent e; e.type = EngineMidiEvent::Type::NoteOff; e.data1 = static_cast<std::uint8_t> (note); return e;
}
inline EngineMidiEvent ccEv (int cc, int value)
{
    EngineMidiEvent e; e.type = EngineMidiEvent::Type::ControlChange; e.data1 = static_cast<std::uint8_t> (cc); e.data2 = static_cast<std::uint8_t> (value); return e;
}
inline EngineMidiEvent bendEv (int bend14)
{
    EngineMidiEvent e; e.type = EngineMidiEvent::Type::PitchBend; e.bend14 = static_cast<std::int16_t> (bend14); return e;
}

struct Rig
{
    SnaralEngine eng;
    ParamSnapshot p;
    double sr;
    bool started_ = false;
    std::vector<float> L, R; // everything rendered so far

    explicit Rig (double sampleRate = 48000.0, int maxBlock = 256) : sr (sampleRate)
    {
        eng.prepare (sr, maxBlock);
        eng.setParameters (p);
    }

    void set (P::Index i, float v) { p[i] = v; }
    void setPercent (P::Index i, float percent01) { p[i] = mapPercent (percent01, paramDef (i)); }

    /// Layer isolation helpers (utility parameters).
    void soloSub() { p[P::layer_solo] = 1.0f; }
    void soloMid() { p[P::layer_solo] = 2.0f; }

    /// The first application after construction snaps every smoother to the parameters set so far (no start-up ramps).
    void apply()
    {
        if (! started_) { eng.reset(); started_ = true; }
        eng.setParameters (p);
    }
    void noteOn (int n, int v = 100) { apply(); eng.handleMidi (noteOnEv (n, v)); }
    void noteOff (int n) { apply(); eng.handleMidi (noteOffEv (n)); }

    /// Renders `n` samples in host blocks of `block`; `events` are applied at their positions (relative to this call).
    void run (int n, int block = 256, const std::vector<Event>& events = {})
    {
        std::size_t ei = 0;
        int pos = 0;
        std::vector<float> bl (static_cast<std::size_t> (block)), br (static_cast<std::size_t> (block));
        while (pos < n)
        {
            const int len = std::min (block, n - pos);
            apply();
            int off = 0;
            while (ei < events.size() && events[ei].at < pos + len)
            {
                const int evOff = std::max (off, events[ei].at - pos);
                if (evOff > off)
                {
                    eng.render (bl.data() + off, br.data() + off, evOff - off);
                    off = evOff;
                }
                eng.handleMidi (events[ei++].e);
            }
            if (off < len)
                eng.render (bl.data() + off, br.data() + off, len - off);
            L.insert (L.end(), bl.begin(), bl.begin() + len);
            R.insert (R.end(), br.begin(), br.begin() + len);
            pos += len;
        }
    }
    void clear() { L.clear(); R.clear(); }
};

inline double peak (const std::vector<float>& v, std::size_t from = 0, std::size_t to = SIZE_MAX)
{
    to = std::min(to, v.size());
    double m = 0.0;
    for (std::size_t i = from; i < to; ++i) m = std::max (m, std::fabs (static_cast<double> (v[i])));
    return m;
}
inline double rmsOf (const std::vector<float>& v, std::size_t from, std::size_t to)
{
    to = std::min (to, v.size());
    double a = 0.0;
    for (std::size_t i = from; i < to; ++i) a += static_cast<double> (v[i]) * v[i];
    return std::sqrt (a / static_cast<double> (std::max<std::size_t> (1, to - from)));
}
inline bool identical (const std::vector<float>& a, const std::vector<float>& b)
{
    return a.size() == b.size() && std::memcmp (a.data(), b.data(), a.size() * sizeof (float)) == 0;
}
inline double maxDiff (const std::vector<float>& a, const std::vector<float>& b)
{
    double m = 0.0;
    const auto n = std::min (a.size(), b.size());
    for (std::size_t i = 0; i < n; ++i) m = std::max (m, std::fabs (static_cast<double> (a[i]) - b[i]));
    return m;
}
inline double diffRmsRel (const std::vector<float>& a, const std::vector<float>& ref, std::size_t from = 0)
{
    double d = 0.0, r = 0.0;
    const auto n = std::min (a.size(), ref.size());
    for (std::size_t i = from; i < n; ++i)
    {
        const double e = static_cast<double> (a[i]) - ref[i];
        d += e * e;
        r += static_cast<double> (ref[i]) * ref[i];
    }
    return std::sqrt (d / std::max (r, 1.0e-30));
}
inline bool finite (const std::vector<float>& v)
{
    for (const float x : v) if (! std::isfinite (x)) return false;
    return true;
}
/// Frequency from upward zero crossings (linear interpolation), over [from, to).
inline double crossingFrequency (const std::vector<float>& v, std::size_t from, std::size_t to, double sr)
{
    to = std::min (to, v.size());
    double first = -1.0, last = -1.0;
    int count = 0;
    for (std::size_t i = from + 1; i < to; ++i)
        if (v[i - 1] < 0.0f && v[i] >= 0.0f)
        {
            const double t = static_cast<double> (i - 1) + static_cast<double> (-v[i - 1]) / (static_cast<double> (v[i]) - v[i - 1]);
            if (count == 0) first = t;
            last = t;
            ++count;
        }
    return count > 1 ? (count - 1) * sr / (last - first) : 0.0;
}
} // namespace rig
