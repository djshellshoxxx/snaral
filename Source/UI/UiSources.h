#pragma once

// Read-only diagnostic interfaces through which the UI looks at the audio engine.
// The integration worker implements them on top of the engine's lock-free diagnostics; the UI never reaches into
// the engine otherwise. All methods are called on the MESSAGE thread only, must not block, allocate or lock, and
// must be safe to call while the audio thread runs.
//
// Optional ready-made lock-free helpers (ScopeRingBuffer, AtomicMeterSource, AtomicCpuSource) are provided at the
// bottom so the engine can simply own one and forward the interface to it.

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

namespace snaral::ui
{
// =================================================================================================================
// Scope
// =================================================================================================================
/// Which signal the scope shows (cdl-signal-visualization.md: post-limiter stereo, mono/sub view, mid-only view).
enum class ScopeTap { Output, Sub, Mid };

class ScopeSource
{
public:
    virtual ~ScopeSource() = default;

    /// True when this tap is currently available (the Output tap must always be).
    virtual bool hasTap (ScopeTap tap) const noexcept { return tap == ScopeTap::Output; }

    /// Copies the most recent `numSamples` samples of `channel` (0 = left, 1 = right; mono taps fill both) into dest,
    /// oldest first. Returns the number of samples actually written (0 when no data is available yet; the scope then
    /// shows "No signal data" rather than inventing a trace).
    virtual int pullLatest (ScopeTap tap, int channel, float* dest, int numSamples) noexcept = 0;

    virtual double getSampleRate() const noexcept = 0;
};

// =================================================================================================================
// Meter
// =================================================================================================================
struct MeterReading
{
    float peak[2] { 0.0f, 0.0f };   ///< linear amplitude: maximum |sample| since the previous read
    float rms[2] { 0.0f, 0.0f };    ///< linear amplitude: RMS of the most recent block
    bool valid = false;             ///< false = no data (UI shows dashes, never a stale clip state)
};

class MeterSource
{
public:
    virtual ~MeterSource() = default;
    /// Returns the peaks accumulated since the last call and resets them.
    virtual MeterReading readAndReset() noexcept = 0;
};

// =================================================================================================================
// CPU
// =================================================================================================================
struct CpuReading
{
    float load = 0.0f;              ///< callback time / block deadline, 0..1 (may exceed 1 on overrun) for ONE audio core
    bool overrunSinceLastRead = false; ///< true only if a measured callback exceeded its deadline since the last read
    bool valid = false;
};

class CpuSource
{
public:
    virtual ~CpuSource() = default;
    virtual CpuReading read() noexcept = 0;
};

// =================================================================================================================
// Optional lock-free helpers
// =================================================================================================================

/// Single-producer (audio thread) / single-consumer (UI thread) sample ring. Reading while the writer wraps can tear a
/// few of the oldest samples of one display frame; that is acceptable for a scope and never touches audio.
class ScopeRingBuffer
{
public:
    /// Allocates (call from prepare(), not from the audio thread). Capacity is rounded up to a power of two.
    void prepare (int capacitySamples, int numChannels = 2)
    {
        int cap = 256;
        while (cap < capacitySamples)
            cap <<= 1;
        capacity = cap;
        channels = std::max (1, numChannels);
        data.assign (static_cast<std::size_t> (cap) * static_cast<std::size_t> (channels), 0.0f);
        writePos.store (0, std::memory_order_release);
    }

    /// Audio thread. `input` has `numChannels` pointers (missing channels repeat channel 0).
    void push (const float* const* input, int numInputChannels, int numSamples) noexcept
    {
        if (capacity == 0 || numInputChannels <= 0)
            return;
        auto w = writePos.load (std::memory_order_relaxed);
        for (int i = 0; i < numSamples; ++i)
        {
            const auto idx = static_cast<std::size_t> ((w + static_cast<std::uint64_t> (i)) & static_cast<std::uint64_t> (capacity - 1));
            for (int c = 0; c < channels; ++c)
                data[idx * static_cast<std::size_t> (channels) + static_cast<std::size_t> (c)] = input[c < numInputChannels ? c : 0][i];
        }
        writePos.store (w + static_cast<std::uint64_t> (numSamples), std::memory_order_release);
    }

    /// UI thread.
    int pullLatest (int channel, float* dest, int numSamples) const noexcept
    {
        if (capacity == 0 || numSamples <= 0)
            return 0;
        const auto w = writePos.load (std::memory_order_acquire);
        const int available = static_cast<int> (std::min<std::uint64_t> (w, static_cast<std::uint64_t> (capacity)));
        const int n = std::min (numSamples, available);
        const int c = std::min (channel, channels - 1);
        for (int i = 0; i < n; ++i)
        {
            const auto idx = static_cast<std::size_t> ((w - static_cast<std::uint64_t> (n) + static_cast<std::uint64_t> (i))
                                                       & static_cast<std::uint64_t> (capacity - 1));
            dest[i] = data[idx * static_cast<std::size_t> (channels) + static_cast<std::size_t> (c)];
        }
        return n;
    }

    int getCapacity() const noexcept { return capacity; }

private:
    std::vector<float> data;
    std::atomic<std::uint64_t> writePos { 0 };
    int capacity = 0;
    int channels = 2;
};

/// Audio thread calls update() per block; UI calls readAndReset().
class AtomicMeterSource : public MeterSource
{
public:
    void update (const float* left, const float* right, int numSamples) noexcept
    {
        float pl = 0.0f, pr = 0.0f;
        double sl = 0.0, sr = 0.0;
        const float* r = right != nullptr ? right : left;
        for (int i = 0; i < numSamples; ++i)
        {
            const float a = left[i], b = r[i];
            pl = std::max (pl, std::abs (a));
            pr = std::max (pr, std::abs (b));
            sl += static_cast<double> (a) * a;
            sr += static_cast<double> (b) * b;
        }
        raiseTo (peakL, pl);
        raiseTo (peakR, pr);
        if (numSamples > 0)
        {
            rmsL.store (static_cast<float> (std::sqrt (sl / numSamples)), std::memory_order_relaxed);
            rmsR.store (static_cast<float> (std::sqrt (sr / numSamples)), std::memory_order_relaxed);
        }
        hasData.store (true, std::memory_order_release);
    }

    MeterReading readAndReset() noexcept override
    {
        MeterReading m;
        m.valid = hasData.load (std::memory_order_acquire);
        m.peak[0] = peakL.exchange (0.0f, std::memory_order_relaxed);
        m.peak[1] = peakR.exchange (0.0f, std::memory_order_relaxed);
        m.rms[0] = rmsL.load (std::memory_order_relaxed);
        m.rms[1] = rmsR.load (std::memory_order_relaxed);
        return m;
    }

private:
    static void raiseTo (std::atomic<float>& a, float v) noexcept
    {
        float cur = a.load (std::memory_order_relaxed);
        while (v > cur && ! a.compare_exchange_weak (cur, v, std::memory_order_relaxed)) {}
    }

    std::atomic<float> peakL { 0.0f }, peakR { 0.0f }, rmsL { 0.0f }, rmsR { 0.0f };
    std::atomic<bool> hasData { false };
};

/// Audio thread calls reportBlock() with measured callback seconds; UI calls read().
class AtomicCpuSource : public CpuSource
{
public:
    void reportBlock (double usedSeconds, double deadlineSeconds) noexcept
    {
        if (deadlineSeconds <= 0.0)
            return;
        const auto l = static_cast<float> (usedSeconds / deadlineSeconds);
        load.store (l, std::memory_order_relaxed);
        if (l > 1.0f)
            overrun.store (true, std::memory_order_relaxed);
        hasData.store (true, std::memory_order_release);
    }

    CpuReading read() noexcept override
    {
        CpuReading r;
        r.valid = hasData.load (std::memory_order_acquire);
        r.load = load.load (std::memory_order_relaxed);
        r.overrunSinceLastRead = overrun.exchange (false, std::memory_order_relaxed);
        return r;
    }

private:
    std::atomic<float> load { 0.0f };
    std::atomic<bool> overrun { false };
    std::atomic<bool> hasData { false };
};
} // namespace snaral::ui
