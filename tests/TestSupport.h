#pragma once

// Tiny shared helpers for the state tests (header only, JUCE-free).

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

#include "Parameters/ParameterTable.h"

#define CHECK(cond)                                                                                   \
    do                                                                                                \
    {                                                                                                 \
        if (! (cond))                                                                                 \
        {                                                                                             \
            std::fprintf (stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
            std::exit (1);                                                                            \
        }                                                                                             \
    } while (false)

#define CHECK_MSG(cond, msg)                                                                          \
    do                                                                                                \
    {                                                                                                 \
        if (! (cond))                                                                                 \
        {                                                                                             \
            std::fprintf (stderr, "CHECK failed at %s:%d: %s -- %s\n", __FILE__, __LINE__, #cond,     \
                          std::string (msg).c_str());                                                 \
            std::exit (1);                                                                            \
        }                                                                                             \
    } while (false)

namespace snaral::testing
{
/// Deterministic LCG so tests never depend on <random> distributions across standard libraries.
struct Rng
{
    std::uint64_t state;
    explicit Rng (std::uint64_t seed) : state (seed * 6364136223846793005ull + 1442695040888963407ull) {}
    std::uint32_t next()
    {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<std::uint32_t> (state >> 33);
    }
    float unit() { return static_cast<float> (next() & 0xFFFFFF) / 16777216.0f; }
    int below (int n) { return static_cast<int> (next() % static_cast<std::uint32_t> (n)); }
};

/// A valid pseudo-random snapshot (all parameters, utility included).
inline ParamSnapshot randomSnapshot (std::uint64_t seed)
{
    Rng rng (seed);
    ParamSnapshot s;
    for (int i = 0; i < parameterCount; ++i)
    {
        const auto& d = parameterTable[static_cast<std::size_t> (i)];
        float v = d.minimum + rng.unit() * (d.maximum - d.minimum);
        if (d.type == ParamType::Choice)
            v = static_cast<float> (rng.below (d.numChoices));
        else if (d.type == ParamType::Bool)
            v = rng.below (2) != 0 ? 1.0f : 0.0f;
        s.v[static_cast<std::size_t> (i)] = d.clampToDomain (v);
    }
    return s;
}

/// Unique scratch directory, removed (recursively) on destruction.
class TempDir
{
public:
    TempDir()
    {
        static int counter = 0;
        path_ = std::filesystem::temp_directory_path()
                / ("snaral_test_" + std::to_string (static_cast<long long> (std::chrono::steady_clock::now().time_since_epoch().count()))
                   + "_" + std::to_string (++counter));
        std::filesystem::remove_all (path_);
        std::filesystem::create_directories (path_);
    }
    ~TempDir()
    {
        std::error_code ec;
        std::filesystem::remove_all (path_, ec);
    }
    TempDir (const TempDir&) = delete;
    TempDir& operator= (const TempDir&) = delete;
    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

inline std::string sourceDir()
{
#ifdef SNARAL_SOURCE_DIR
    return SNARAL_SOURCE_DIR;
#else
    return ".";
#endif
}
} // namespace snaral::testing
