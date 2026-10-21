#include "Core/Randomizer.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <vector>

#include "State/Pure/NumberText.h"
#include "State/Pure/SnapshotUtil.h"

namespace snaral::rnd
{
namespace
{
enum class Kind : std::uint8_t { None, Uniform, LogUniform, Chance, Discrete };

struct Rule
{
    Kind kind = Kind::None;
    LockGroup group = lockNone;
    float lo = 0.0f, hi = 0.0f;   // Uniform/LogUniform/Chance: value window (Chance: value when "on")
    float p = 0.0f;               // Chance: probability of being non-zero/on
};

struct RuleTable
{
    std::array<Rule, static_cast<std::size_t> (parameterCount)> r {};

    void uni (P::Index i, LockGroup g, float lo, float hi) { r[static_cast<std::size_t> (i)] = { Kind::Uniform, g, lo, hi, 0 }; }
    void full (P::Index i, LockGroup g)
    {
        const auto& d = paramDef (i);
        r[static_cast<std::size_t> (i)] = { d.type == ParamType::Float ? Kind::Uniform : Kind::Discrete, g, d.minimum, d.maximum, 0 };
    }
    void logUni (P::Index i, LockGroup g, float lo, float hi) { r[static_cast<std::size_t> (i)] = { Kind::LogUniform, g, lo, hi, 0 }; }
    void chance (P::Index i, LockGroup g, float p, float hi) { r[static_cast<std::size_t> (i)] = { Kind::Chance, g, 0.0f, hi, p }; }

    RuleTable()
    {
        // Macros (never locked)
        uni (P::weight, lockNone, 30, 100); uni (P::growl, lockNone, 20, 100); uni (P::bite, lockNone, 0, 70);
        uni (P::motion, lockNone, 0, 100);  uni (P::space, lockNone, 0, 80);
        // Voice
        full (P::legato, lockVoice); uni (P::glide_ms, lockVoice, 0, 200);
        // Sub
        full (P::sub_waveform, lockSub); uni (P::sub_level, lockSub, 60, 100); uni (P::sub_amp_attack_ms, lockSub, 0, 20);
        full (P::sub_amp_decay_ms, lockSub); uni (P::sub_amp_sustain, lockSub, 60, 100); uni (P::sub_amp_release_ms, lockSub, 20, 600);
        // Mid sources
        full (P::mid_source_a, lockSources); full (P::mid_source_b, lockSources); full (P::mid_blend, lockSources);
        full (P::mid_table_position, lockSources); full (P::mid_pulse_width, lockSources);
        uni (P::mid_tune_cents, lockSources, -15, 15); uni (P::mid_level, lockSources, 50, 100);
        uni (P::mid_amp_attack_ms, lockSources, 0, 40); full (P::mid_amp_decay_ms, lockSources);
        uni (P::mid_amp_sustain, lockSources, 40, 100); uni (P::mid_amp_release_ms, lockSources, 20, 800);
        // Motion
        logUni (P::filter_cutoff_hz, lockMotion, 150, 6000); uni (P::filter_resonance, lockMotion, 0, 60);
        full (P::filter_type, lockMotion);
        for (auto i : { P::contour_attack_ms, P::contour_decay_ms, P::contour_sustain, P::contour_release_ms, P::contour_shape,
                        P::contour_depth, P::lfo_mode, P::lfo_rate_hz, P::lfo_sync_division, P::lfo_waveform, P::lfo_retrigger,
                        P::lfo_phase, P::lfo_depth, P::traj_time_mode, P::traj_length_ms, P::traj_length_div, P::traj_dest,
                        P::traj_depth, P::traj_curve, P::traj_loop, P::traj_x2, P::traj_x3, P::traj_x4,
                        P::traj_y1, P::traj_y2, P::traj_y3, P::traj_y4, P::traj_y5 })
            full (i, lockMotion);
        r[static_cast<std::size_t> (P::lfo_rate_hz)].kind = Kind::LogUniform;
        r[static_cast<std::size_t> (P::traj_length_ms)].kind = Kind::LogUniform;
        chance (P::traj_enabled, lockMotion, 0.25f, 1.0f);
        // Drive
        full (P::drive_type, lockDrive); uni (P::drive_amount, lockDrive, 0, 70); full (P::drive_tone, lockDrive);
        uni (P::drive_mix, lockDrive, 30, 100);
        // Texture
        chance (P::noise_level, lockTexture, 0.25f, 40); full (P::noise_tone, lockTexture);
        chance (P::metal_level, lockTexture, 0.25f, 40); full (P::metal_decay_ms, lockTexture); full (P::metal_tune, lockTexture);
        // Effects (decision v1: fracture_comp_db limited to -6..+3 dB, spec gave no window)
        chance (P::furnace_enabled, lockEffects, 0.30f, 1.0f);
        for (auto i : { P::furnace_vowel, P::furnace_spacing, P::furnace_resonance, P::furnace_env_amount, P::furnace_lfo_amount })
            full (i, lockEffects);
        uni (P::furnace_mix, lockEffects, 0, 70);
        chance (P::fracture_enabled, lockEffects, 0.30f, 1.0f);
        for (auto i : { P::fracture_crossover_hz, P::fracture_drive_low, P::fracture_drive_mid, P::fracture_drive_high,
                        P::fracture_symmetry, P::fracture_band_mix })
            full (i, lockEffects);
        r[static_cast<std::size_t> (P::fracture_crossover_hz)].kind = Kind::LogUniform;
        uni (P::fracture_comp_db, lockEffects, -6, 3);
        uni (P::fracture_mix, lockEffects, 0, 70);

        // Intersect windows with the registry range once.
        for (int i = 0; i < parameterCount; ++i)
        {
            auto& rule = r[static_cast<std::size_t> (i)];
            if (rule.kind == Kind::None) continue;
            const auto& d = parameterTable[static_cast<std::size_t> (i)];
            rule.lo = std::max (rule.lo, d.minimum);
            rule.hi = std::min (rule.hi, d.maximum);
        }
    }
};

const RuleTable& rules()
{
    static const RuleTable t;
    return t;
}

struct SplitMix64
{
    std::uint64_t s;
    std::uint64_t next() noexcept
    {
        std::uint64_t z = (s += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    /// uniform [0,1) from the top 53 bits
    double uniform() noexcept { return static_cast<double> (next() >> 11) * (1.0 / 9007199254740992.0); }
};

constexpr float trajGap = 0.05f;
} // namespace

LockGroup lockGroupOf (int i) noexcept
{
    return (i >= 0 && i < parameterCount) ? rules().r[static_cast<std::size_t> (i)].group : lockNone;
}

bool isRandomizable (int i) noexcept
{
    return i >= 0 && i < parameterCount && rules().r[static_cast<std::size_t> (i)].kind != Kind::None;
}

void safeWindow (int i, float& lo, float& hi) noexcept
{
    const auto& r = rules().r[static_cast<std::size_t> (i)];
    lo = r.lo;
    hi = r.kind == Kind::Chance ? std::max (r.hi, 0.0f) : r.hi;
}

RandomizeResult randomize (const ParamSnapshot& current, std::uint32_t seed, LockMask locks, float amount)
{
    RandomizeResult res;
    res.snapshot = current;
    if (! snapshotIsValid (current))
    {
        res.error = "Current patch is invalid; Randomize did nothing.";
        return res;
    }
    amount = std::isfinite (amount) ? std::clamp (amount, 0.0f, 1.0f) : 1.0f;

    if (amount <= 0.0f)
    {
        res.ok = true; // nothing to do: the source is returned bit-exactly
        return res;
    }

    SplitMix64 rng { seed };
    SplitMix64 gate { static_cast<std::uint64_t> (seed) ^ 0xA5A5A5A55A5A5A5Aull };
    const auto& rt = rules();
    ParamSnapshot out = current;

    for (int i = 0; i < parameterCount; ++i)
    {
        const auto& rule = rt.r[static_cast<std::size_t> (i)];
        if (rule.kind == Kind::None)
            continue;
        const double u = rng.uniform();     // always consumed
        const double g = gate.uniform();    // always consumed
        if (rule.group != lockNone && (locks & rule.group) != 0)
            continue;

        const auto& d = parameterTable[static_cast<std::size_t> (i)];
        const float cur = current.v[static_cast<std::size_t> (i)];
        float target = cur;
        bool discrete = false;
        switch (rule.kind)
        {
            case Kind::Uniform:
                target = rule.lo + static_cast<float> (u) * (rule.hi - rule.lo);
                break;
            case Kind::LogUniform:
                target = rule.lo * std::exp (static_cast<float> (u) * std::log (rule.hi / rule.lo));
                break;
            case Kind::Chance:
                discrete = d.type == ParamType::Bool;
                if (u < rule.p)
                {
                    const float k = static_cast<float> (u / rule.p); // re-use the same draw for the value
                    target = discrete ? 1.0f : std::max (rule.lo, k * rule.hi);
                }
                else
                    target = 0.0f;
                break;
            case Kind::Discrete:
                discrete = true;
                if (d.type == ParamType::Bool)
                    target = u < 0.5 ? 0.0f : 1.0f;
                else if (d.type == ParamType::Choice)
                    target = static_cast<float> (std::min (d.numChoices - 1, static_cast<int> (u * d.numChoices)));
                else
                    target = d.minimum + static_cast<float> (u) * (d.maximum - d.minimum);
                break;
            case Kind::None: break;
        }
        if (d.type == ParamType::Choice || d.type == ParamType::Bool)
            discrete = true;

        float v;
        if (amount >= 1.0f)
            v = target;
        else if (discrete)
            v = g < static_cast<double> (amount) ? target : cur;
        else
            v = d.fromNormalized (d.toNormalized (cur) + amount * (d.toNormalized (target) - d.toNormalized (cur)));
        out.v[static_cast<std::size_t> (i)] = d.clampToDomain (v);
    }

    // Trajectory: x2 < x3 < x4 strictly increasing with a minimum gap (only if Motion is unlocked).
    if ((locks & lockMotion) == 0)
    {
        float x[3] = { out[P::traj_x2], out[P::traj_x3], out[P::traj_x4] };
        std::sort (x, x + 3);
        x[0] = std::clamp (x[0], trajGap, 1.0f - 3 * trajGap);
        x[1] = std::clamp (x[1], x[0] + trajGap, 1.0f - 2 * trajGap);
        x[2] = std::clamp (x[2], x[1] + trajGap, 1.0f - trajGap);
        out[P::traj_x2] = x[0]; out[P::traj_x3] = x[1]; out[P::traj_x4] = x[2];
    }

    if (! snapshotIsValid (out))
    {
        res.error = "Randomize produced an invalid candidate; nothing applied.";
        return res;
    }
    for (int i = 0; i < parameterCount; ++i)
        res.changedCount += out.v[static_cast<std::size_t> (i)] != current.v[static_cast<std::size_t> (i)] ? 1 : 0;
    res.snapshot = out;
    res.ok = true;
    return res;
}

std::uint32_t seedFromClock()
{
    const auto t = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto w = std::chrono::system_clock::now().time_since_epoch().count();
    SplitMix64 m { static_cast<std::uint64_t> (t) ^ (static_cast<std::uint64_t> (w) * 0x9E3779B97F4A7C15ull) };
    return static_cast<std::uint32_t> (m.next() >> 32);
}

std::string seedToHex (std::uint32_t seed)
{
    static const char* digits = "0123456789abcdef";
    std::string s (8, '0');
    for (int i = 7; i >= 0; --i, seed >>= 4)
        s[static_cast<std::size_t> (i)] = digits[seed & 15u];
    return s;
}

bool seedFromHex (std::string_view t, std::uint32_t& seed)
{
    if (t.size() >= 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X'))
        t.remove_prefix (2);
    if (t.empty() || t.size() > 8)
        return false;
    std::uint32_t v = 0;
    for (char c : t)
    {
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else return false;
        v = (v << 4) | static_cast<std::uint32_t> (d);
    }
    seed = v;
    return true;
}

ParamSnapshot initializedPatch (const ParamSnapshot& current)
{
    ParamSnapshot out = current;
    const ParamSnapshot defaults;
    for (int i = 0; i < parameterCount; ++i)
        if (parameterTable[static_cast<std::size_t> (i)].inPreset())
            out.v[static_cast<std::size_t> (i)] = defaults.v[static_cast<std::size_t> (i)];
    return out;
}

// ------------------------------------------------------------------ context actions

std::string copyValueText (int paramIndex, float value)
{
    if (paramIndex < 0 || paramIndex >= parameterCount)
        return {};
    const auto& d = parameterTable[static_cast<std::size_t> (paramIndex)];
    return text::formatFloat (d.clampToDomain (value));
}

namespace
{
std::string lower (std::string_view s)
{
    std::string o (s);
    for (auto& c : o) c = static_cast<char> (std::tolower (static_cast<unsigned char> (c)));
    return o;
}
std::string trim (std::string_view s)
{
    while (! s.empty() && std::isspace (static_cast<unsigned char> (s.front()))) s.remove_prefix (1);
    while (! s.empty() && std::isspace (static_cast<unsigned char> (s.back()))) s.remove_suffix (1);
    return std::string (s);
}
std::string rangeText (const ParamDef& d)
{
    return text::formatFloat (d.minimum) + " to " + text::formatFloat (d.maximum) + (d.unit.empty() ? "" : " " + std::string (d.unit));
}
} // namespace

PasteResult parsePasteText (int paramIndex, std::string_view raw)
{
    PasteResult r;
    if (paramIndex < 0 || paramIndex >= parameterCount)
    {
        r.error = "Unknown parameter";
        return r;
    }
    const auto& d = parameterTable[static_cast<std::size_t> (paramIndex)];
    const std::string s = trim (raw);
    if (s.empty() || s.size() > 64)
    {
        r.error = "Not a valid value";
        return r;
    }
    const std::string ls = lower (s);

    if (d.type == ParamType::Choice)
        for (int i = 0; i < d.numChoices; ++i)
            if (ls == lower (d.choices[static_cast<std::size_t> (i)]))
            {
                r.ok = true; r.value = static_cast<float> (i);
                return r;
            }
    if (d.type == ParamType::Bool)
    {
        if (ls == "on" || ls == "true") { r.ok = true; r.value = 1.0f; return r; }
        if (ls == "off" || ls == "false") { r.ok = true; r.value = 0.0f; return r; }
    }

    // split number / suffix
    std::size_t n = 0;
    while (n < s.size() && (std::isdigit (static_cast<unsigned char> (s[n])) || s[n] == '+' || s[n] == '-' || s[n] == '.'
                            || ((s[n] == 'e' || s[n] == 'E') && n > 0 && n + 1 < s.size() && (std::isdigit (static_cast<unsigned char> (s[n + 1])) || s[n + 1] == '-' || s[n + 1] == '+'))))
        ++n;
    std::string numTok = s.substr (0, n);
    if (! numTok.empty() && numTok.front() == '+') numTok.erase (0, 1);
    const std::string suffix = lower (trim (std::string_view (s).substr (n)));

    float v = 0.0f;
    if (numTok.empty() || ! text::parseFloat (numTok, v))
    {
        r.error = "Not a valid number";
        return r;
    }
    if (! suffix.empty())
    {
        const std::string unit = lower (d.unit);
        if (unit == "hz" && (suffix == "k" || suffix == "khz"))
            v *= 1000.0f;
        else if (! unit.empty() && suffix == unit)
            ;
        else
        {
            r.error = "Unit '" + suffix + "' does not match" + (d.unit.empty() ? " (no unit)" : " " + std::string (d.unit));
            return r;
        }
    }
    if (! std::isfinite (v))
    {
        r.error = "Not a finite number";
        return r;
    }
    // Tolerate float parse noise at the range ends only (1e-6 relative); anything else is rejected, never clamped.
    const float slack = 1e-6f * std::max ({ std::abs (d.minimum), std::abs (d.maximum), 1.0f });
    if (v < d.minimum - slack || v > d.maximum + slack)
    {
        r.error = "Out of range (" + rangeText (d) + ")";
        return r;
    }
    v = std::clamp (v, d.minimum, d.maximum);
    if (d.isInteger() && std::abs (v - std::round (v)) > 1e-6f)
    {
        r.error = "Whole numbers only";
        return r;
    }
    r.ok = true;
    r.value = d.isInteger() ? std::round (v) : v;
    return r;
}

float resetParameterValue (int paramIndex) noexcept
{
    if (paramIndex < 0 || paramIndex >= parameterCount)
        return 0.0f;
    return parameterTable[static_cast<std::size_t> (paramIndex)].defaultValue;
}
} // namespace snaral::rnd
