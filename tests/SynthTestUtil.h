#pragma once

// Shared helpers for the engine-synth tests (Oscillators, SvfFilter, Lfo, Contour, Trajectory).
// Include from exactly one translation unit per test executable: it replaces global operator new/delete
// to count heap allocations so tests can assert that render paths allocate nothing.

#include <cstdio>
#include <cstdlib>
#include <new>

namespace synthtest
{
inline long g_allocCount = 0;
inline bool g_tracking = false;

struct NoAllocScope
{
    NoAllocScope() { g_allocCount = 0; g_tracking = true; }
    ~NoAllocScope() { g_tracking = false; }
    long count() const { return g_allocCount; }
};
} // namespace synthtest

void* operator new (std::size_t n)
{
    if (synthtest::g_tracking)
        ++synthtest::g_allocCount;
    if (void* p = std::malloc (n ? n : 1))
        return p;
    throw std::bad_alloc();
}
void* operator new[] (std::size_t n) { return operator new (n); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }

#define CHECK(cond)                                                                                   \
    do {                                                                                              \
        if (! (cond))                                                                                 \
        {                                                                                             \
            std::fprintf (stderr, "CHECK FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond);            \
            std::exit (1);                                                                            \
        }                                                                                             \
    } while (0)

#define CHECK_NEAR(a, b, tol)                                                                         \
    do {                                                                                              \
        const double va_ = (a), vb_ = (b);                                                            \
        if (! (std::fabs (va_ - vb_) <= (tol)))                                                       \
        {                                                                                             \
            std::fprintf (stderr, "CHECK_NEAR FAILED %s:%d: %s=%.9g vs %s=%.9g (tol %g)\n", __FILE__, \
                          __LINE__, #a, va_, #b, vb_, static_cast<double> (tol));                     \
            std::exit (1);                                                                            \
        }                                                                                             \
    } while (0)
