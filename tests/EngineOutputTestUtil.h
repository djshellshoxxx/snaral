#pragma once

// Shared helpers for the engine-output tests (Oversampler / Drive / Texture / Width / Limiter).
// Include from exactly ONE translation unit per test executable (it replaces global operator new/delete
// to count heap allocations made while `allocCounterArmed` is true).

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <atomic>
#include <cassert>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <vector>

inline std::atomic<long> allocCount { 0 };
inline std::atomic<bool> allocCounterArmed { false };

void* operator new (std::size_t n)
{
    if (allocCounterArmed.load (std::memory_order_relaxed)) allocCount.fetch_add (1);
    if (void* p = std::malloc (n ? n : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[] (std::size_t n) { return operator new (n); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }

struct NoAlloc
{
    NoAlloc() { allocCount = 0; allocCounterArmed = true; }
    ~NoAlloc() { allocCounterArmed = false; }
    long count() const { return allocCount.load(); }
};

#define CHECK(cond) do { if (! (cond)) { std::fprintf (stderr, "CHECK FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond); std::abort(); } } while (0)

namespace tu
{
inline bool allFinite (const std::vector<float>& v)
{
    for (const float x : v) if (! std::isfinite (x)) return false;
    return true;
}
inline double rms (const std::vector<float>& v, std::size_t from, std::size_t to)
{
    double a = 0.0;
    for (std::size_t i = from; i < to; ++i) a += static_cast<double> (v[i]) * v[i];
    return std::sqrt (a / static_cast<double> (to - from));
}
inline double db (double x) { return 20.0 * std::log10 (std::max (x, 1.0e-12)); }

inline void fft (std::vector<std::complex<double>>& a)
{
    const std::size_t n = a.size();
    for (std::size_t i = 1, j = 0; i < n; ++i)
    {
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (a[i], a[j]);
    }
    for (std::size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = -2.0 * 3.14159265358979323846 / static_cast<double> (len);
        const std::complex<double> wl (std::cos (ang), std::sin (ang));
        for (std::size_t i = 0; i < n; i += len)
        {
            std::complex<double> w (1.0);
            for (std::size_t k = 0; k < len / 2; ++k)
            {
                const auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v; a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}
/// Power spectrum |X[k]|^2 / N^2 for k = 0..N/2 of x[0..N) (rectangular window; use coherent tones).
inline std::vector<double> powerSpectrum (const float* x, std::size_t n)
{
    std::vector<std::complex<double>> a (n);
    for (std::size_t i = 0; i < n; ++i) a[i] = x[i];
    fft (a);
    std::vector<double> p (n / 2 + 1);
    for (std::size_t k = 0; k <= n / 2; ++k) p[k] = std::norm (a[k]) / (static_cast<double> (n) * n);
    return p;
}
struct Lcg
{
    unsigned s = 12345u;
    float next() { s = s * 1664525u + 1013904223u; return static_cast<float> (static_cast<int> (s)) * (1.0f / 2147483648.0f); }
};
} // namespace tu
