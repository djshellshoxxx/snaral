#pragma once

// Lock-free read-only diagnostics published by the engine (audio thread = single writer) and read by the editor
// (message thread = single reader per accessor). JUCE-free, header-only, real-time safe: only relaxed/acquire/release
// atomics on fixed storage, no allocation, no locks.
//
//  * peak / RMS: per publish window (about 512 samples), per channel, linear 0..; the UI applies its own ballistics.
//  * scope tap: a SPSC ring of the final mono mix ((L+R)/2 after the limiter). The writer never blocks; the reader
//    copies the most recent N samples with copyLatest(). Elements are atomics, so a reader that is overtaken by the
//    writer sees a mixture of old and new samples (acceptable for drawing) but never a data race.
//  * sub / mid scope taps: the same kind of ring for the centred sub layer (after delay alignment) and the mono sum of
//    the mid layer (after Width), both before trim and limiter, for the scope's Sub and Mid views (diagnosis only).
//  * active voice count, CPU load estimate (render time / audio time, 0..1+), limiter gain reduction, reported latency,
//    tempo flags, event counters (dropped commands / MIDI events).

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace snaral
{
class EngineDiagnostics
{
public:
    static constexpr std::size_t scopeCapacity = 16384; // power of two

    // ---- reader (UI thread) ----
    float peak (int channel) const noexcept { return peak_[channel & 1].load (std::memory_order_relaxed); }
    float rms (int channel) const noexcept { return rms_[channel & 1].load (std::memory_order_relaxed); }
    int activeVoices() const noexcept { return activeVoices_.load (std::memory_order_relaxed); }
    float cpuLoad() const noexcept { return cpuLoad_.load (std::memory_order_relaxed); }
    float limiterReductionDb() const noexcept { return limiterReductionDb_.load (std::memory_order_relaxed); }
    int latencySamples() const noexcept { return latency_.load (std::memory_order_relaxed); }

    /// True when the host tempo is currently missing or invalid (the UI status line shows "tempo unavailable").
    bool tempoUnavailable() const noexcept { return tempoUnavailable_.load (std::memory_order_relaxed); }
    /// True when LFO or Trajectory sync is selected and no valid tempo has ever been seen (free rate / ms length in use).
    bool syncFallbackActive() const noexcept { return syncFallback_.load (std::memory_order_relaxed); }

    /// Number of render calls (of at least 32 samples) that took longer than the audio they produced (measured, not inferred).
    std::uint32_t overruns() const noexcept { return overruns_.load (std::memory_order_relaxed); }
    std::uint32_t droppedCommands() const noexcept { return droppedCommands_.load (std::memory_order_relaxed); }
    std::uint32_t droppedMidiEvents() const noexcept { return droppedMidi_.load (std::memory_order_relaxed); }

    /// Total number of scope samples ever written (monotonic).
    std::uint64_t scopeWritten() const noexcept { return out_.write.load (std::memory_order_acquire); }

    /// Copies the most recent `count` scope samples (oldest first) into dst; returns how many were available.
    std::size_t copyLatest (float* dst, std::size_t count) const noexcept { return out_.copyLatest (dst, count); }
    /// Same for the sub tap (0) and the mid tap (1).
    std::size_t copyLatestLayer (int layer, float* dst, std::size_t count) const noexcept { return (layer == 0 ? sub_ : mid_).copyLatest (dst, count); }

    // ---- writer (audio thread) ----
    void pushScope (float sample) noexcept { out_.push (sample); }
    void pushSubScope (float sample) noexcept { sub_.push (sample); }
    void pushMidScope (float sample) noexcept { mid_.push (sample); }
    void publishLevels (float peakL, float peakR, float rmsL, float rmsR) noexcept
    {
        peak_[0].store (peakL, std::memory_order_relaxed);
        peak_[1].store (peakR, std::memory_order_relaxed);
        rms_[0].store (rmsL, std::memory_order_relaxed);
        rms_[1].store (rmsR, std::memory_order_relaxed);
    }
    void publishActiveVoices (int n) noexcept { activeVoices_.store (n, std::memory_order_relaxed); }
    void publishCpuLoad (float load) noexcept { cpuLoad_.store (load, std::memory_order_relaxed); }
    void publishLimiterReductionDb (float db) noexcept { limiterReductionDb_.store (db, std::memory_order_relaxed); }
    void publishLatency (int samples) noexcept { latency_.store (samples, std::memory_order_relaxed); }
    void publishTempo (bool unavailable, bool fallback) noexcept
    {
        tempoUnavailable_.store (unavailable, std::memory_order_relaxed);
        syncFallback_.store (fallback, std::memory_order_relaxed);
    }
    void addOverrun() noexcept { overruns_.fetch_add (1, std::memory_order_relaxed); }
    void addDroppedCommands (std::uint32_t n) noexcept { droppedCommands_.fetch_add (n, std::memory_order_relaxed); }
    void addDroppedMidiEvents (std::uint32_t n) noexcept { droppedMidi_.fetch_add (n, std::memory_order_relaxed); }

    void clearScope() noexcept
    {
        out_.clear();
        sub_.clear();
        mid_.clear();
    }

private:
    struct Ring
    {
        void push (float sample) noexcept
        {
            const std::uint64_t w = write.load (std::memory_order_relaxed);
            data[static_cast<std::size_t> (w & (scopeCapacity - 1))].store (sample, std::memory_order_relaxed);
            write.store (w + 1, std::memory_order_release);
        }
        std::size_t copyLatest (float* dst, std::size_t count) const noexcept
        {
            const std::uint64_t w = write.load (std::memory_order_acquire);
            if (count > scopeCapacity) count = scopeCapacity;
            if (count > w) count = static_cast<std::size_t> (w);
            const std::uint64_t start = w - count;
            for (std::size_t i = 0; i < count; ++i)
                dst[i] = data[static_cast<std::size_t> ((start + i) & (scopeCapacity - 1))].load (std::memory_order_relaxed);
            return count;
        }
        void clear() noexcept
        {
            for (auto& x : data) x.store (0.0f, std::memory_order_relaxed);
            write.store (0, std::memory_order_release);
        }
        std::atomic<std::uint64_t> write { 0 };
        std::array<std::atomic<float>, scopeCapacity> data {};
    };

    std::array<std::atomic<float>, 2> peak_ {}, rms_ {};
    std::atomic<int> activeVoices_ { 0 };
    std::atomic<float> cpuLoad_ { 0.0f };
    std::atomic<float> limiterReductionDb_ { 0.0f };
    std::atomic<int> latency_ { 0 };
    std::atomic<bool> tempoUnavailable_ { false };
    std::atomic<bool> syncFallback_ { false };
    std::atomic<std::uint32_t> droppedCommands_ { 0 }, droppedMidi_ { 0 }, overruns_ { 0 };
    Ring out_, sub_, mid_;
};
} // namespace snaral
