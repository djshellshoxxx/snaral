#pragma once

// MIDI-learn hook between the message thread and the audio thread (docs/specs/expansion-midi-cc-learn.md).
// JUCE-free, header-only, real-time safe (fixed storage, atomics only). The mapping MODEL (conflict rules, UI flow,
// persistence) belongs to the CDL services worker; this header is only the lock-free transport:
//
//  message thread                                   audio thread
//  ---------------                                  ------------
//  CcMapPublisher::publish (maps)  ---seqlock--->   CcRouter::refresh (publisher)      once per block
//  CcRouter::setLearnActive ------ atomic flag --->  CcRouter::handleCc (...)          per CC event
//  CcSeenQueue::pop  <---------- SPSC ring --------  (non-reserved CC pushed while learning)
//  CcAppliedQueue::pop <-------- SPSC ring --------  (mapped CC -> target + normalised value)
//                                                   CcRouter::applyOverrides (snapshot) once per block
//
// A mapped CC changes the NEXT block's snapshot (override), is mirrored to the host/UI parameter by the message thread
// draining CcAppliedQueue, and the override disappears as soon as the parameter itself has caught up or has been moved by
// anyone else (UI, automation). The audio thread never allocates, locks or notifies the host.

#include "Parameters/ParameterTable.h"

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string_view>
#include <algorithm>

namespace snaral
{
inline constexpr int maxCcMappings = 16;

/// Messages that always keep their MIDI meaning: bank select (0, 32), data entry (6, 38), sustain (64), (N)RPN (98..101)
/// and all channel-mode messages (120..127). They are never consumed by a mapping and never captured by learn.
inline constexpr bool isReservedCc (int cc) noexcept
{
    return cc == 0 || cc == 32 || cc == 6 || cc == 38 || cc == 64 || (cc >= 98 && cc <= 101) || (cc >= 120 && cc <= 127);
}

/// Registry classifier from the spec: Float or Bool, automatable, not integer, not on the exclusion list.
inline bool isCcLearnable (int paramIndex) noexcept
{
    if (paramIndex < 0 || paramIndex >= parameterCount)
        return false;
    const auto& d = parameterTable[static_cast<std::size_t> (paramIndex)];
    if (d.type == ParamType::Choice || ! d.automatable() || (d.type == ParamType::Float && d.isInteger()))
        return false;
    for (const auto excluded : { std::string_view ("master_tune_hz"), std::string_view ("limiter_enabled"),
                                 std::string_view ("limiter_ceiling_db"), std::string_view ("bypass"),
                                 std::string_view ("sub_mute"), std::string_view ("mid_mute") })
        if (d.id == excluded)
            return false;
    return true;
}

struct CcMapping
{
    std::int16_t paramIndex = -1;  // snaral::P::Index
    std::uint8_t cc = 0;           // 0..127
    std::uint8_t channel = 0;      // 0 = omni, 1..16
    float minNorm = 0.0f, maxNorm = 1.0f; // normalised target range, min <= max
    bool invert = false;
};

struct CcMapTable
{
    std::array<CcMapping, maxCcMappings> maps {};
    int count = 0;
};

/// Seqlock over atomic words: one writer thread (message thread; calls are serialised by an internal mutex), the audio
/// thread reads without waiting. A torn read is detected and simply retried on the next block.
class CcMapPublisher
{
public:
    /// Validates and publishes the whole table. Returns false (nothing published) on: more than 16 entries, bad parameter
    /// or non-learnable target, reserved/out-of-range cc, channel > 16, min > max or non-finite range, duplicate target,
    /// or a duplicate (cc, channel) pair (omni overlaps every channel).
    bool publish (const CcMapping* maps, int count)
    {
        if (count < 0 || count > maxCcMappings || (count > 0 && maps == nullptr))
            return false;
        for (int i = 0; i < count; ++i)
        {
            const auto& m = maps[i];
            if (! isCcLearnable (m.paramIndex) || m.cc > 127 || isReservedCc (m.cc) || m.channel > 16
                || ! std::isfinite (m.minNorm) || ! std::isfinite (m.maxNorm) || m.minNorm < 0.0f || m.maxNorm > 1.0f
                || m.minNorm > m.maxNorm)
                return false;
            for (int j = 0; j < i; ++j)
            {
                const auto& o = maps[j];
                if (o.paramIndex == m.paramIndex)
                    return false;
                if (o.cc == m.cc && (o.channel == m.channel || o.channel == 0 || m.channel == 0))
                    return false;
            }
        }
        const std::lock_guard<std::mutex> lock (writeMutex_);
        seq_.fetch_add (1, std::memory_order_acq_rel); // odd: write in progress
        count_.store (static_cast<std::uint32_t> (count), std::memory_order_relaxed);
        for (int i = 0; i < maxCcMappings; ++i)
        {
            std::uint32_t w0 = 0, w1 = 0, w2 = 0;
            if (i < count)
            {
                const auto& m = maps[i];
                w0 = 0x80000000u | (static_cast<std::uint32_t> (m.paramIndex) & 0xFFFFu) | (static_cast<std::uint32_t> (m.cc) << 16)
                     | (static_cast<std::uint32_t> (m.channel) << 23) | (m.invert ? 0x40000000u : 0u);
                std::memcpy (&w1, &m.minNorm, sizeof w1);
                std::memcpy (&w2, &m.maxNorm, sizeof w2);
            }
            words_[static_cast<std::size_t> (i * 3)].store (w0, std::memory_order_relaxed);
            words_[static_cast<std::size_t> (i * 3 + 1)].store (w1, std::memory_order_relaxed);
            words_[static_cast<std::size_t> (i * 3 + 2)].store (w2, std::memory_order_relaxed);
        }
        seq_.fetch_add (1, std::memory_order_release); // even: stable
        return true;
    }

    bool publish (const CcMapTable& t) { return publish (t.maps.data(), t.count); }

    /// Audio thread: copies the table into `out` when it changed since `lastSeq`. Wait-free. Returns true if `out` was updated.
    bool fetch (CcMapTable& out, std::uint32_t& lastSeq) const noexcept
    {
        const std::uint32_t s1 = seq_.load (std::memory_order_acquire);
        if ((s1 & 1u) != 0u || s1 == lastSeq)
            return false;
        CcMapTable tmp;
        const std::uint32_t c = std::min<std::uint32_t> (count_.load (std::memory_order_relaxed), maxCcMappings);
        tmp.count = static_cast<int> (c);
        for (int i = 0; i < maxCcMappings; ++i)
        {
            const std::uint32_t w0 = words_[static_cast<std::size_t> (i * 3)].load (std::memory_order_relaxed);
            const std::uint32_t w1 = words_[static_cast<std::size_t> (i * 3 + 1)].load (std::memory_order_relaxed);
            const std::uint32_t w2 = words_[static_cast<std::size_t> (i * 3 + 2)].load (std::memory_order_relaxed);
            auto& m = tmp.maps[static_cast<std::size_t> (i)];
            if ((w0 & 0x80000000u) != 0u)
            {
                m.paramIndex = static_cast<std::int16_t> (w0 & 0xFFFFu);
                m.cc = static_cast<std::uint8_t> ((w0 >> 16) & 0x7Fu);
                m.channel = static_cast<std::uint8_t> ((w0 >> 23) & 0x1Fu);
                m.invert = (w0 & 0x40000000u) != 0u;
                std::memcpy (&m.minNorm, &w1, sizeof w1);
                std::memcpy (&m.maxNorm, &w2, sizeof w2);
            }
        }
        std::atomic_thread_fence (std::memory_order_acquire);
        if (seq_.load (std::memory_order_relaxed) != s1)
            return false; // torn: keep the previous table, retry next block
        out = tmp;
        lastSeq = s1;
        return true;
    }

    std::uint32_t sequence() const noexcept { return seq_.load (std::memory_order_acquire); }

private:
    std::mutex writeMutex_; // serialises writers only; never touched by the audio thread
    std::atomic<std::uint32_t> seq_ { 0 };
    std::atomic<std::uint32_t> count_ { 0 };
    std::array<std::atomic<std::uint32_t>, static_cast<std::size_t> (maxCcMappings * 3)> words_ {};
};

/// Bounded wait-free single-producer / single-consumer ring (drops when full; the producer counts drops).
template <class T, std::size_t Capacity>
class SpscRing
{
    static_assert ((Capacity & (Capacity - 1)) == 0, "capacity must be a power of two");

public:
    bool push (const T& v) noexcept
    {
        const std::size_t h = head_.load (std::memory_order_relaxed);
        if (h - tail_.load (std::memory_order_acquire) >= Capacity)
        {
            dropped_.fetch_add (1, std::memory_order_relaxed);
            return false;
        }
        items_[h & (Capacity - 1)] = v;
        head_.store (h + 1, std::memory_order_release);
        return true;
    }
    bool pop (T& out) noexcept
    {
        const std::size_t t = tail_.load (std::memory_order_relaxed);
        if (t == head_.load (std::memory_order_acquire))
            return false;
        out = items_[t & (Capacity - 1)];
        tail_.store (t + 1, std::memory_order_release);
        return true;
    }
    std::uint32_t dropped() const noexcept { return dropped_.load (std::memory_order_relaxed); }

private:
    std::array<T, Capacity> items_ {};
    std::atomic<std::size_t> head_ { 0 }, tail_ { 0 };
    std::atomic<std::uint32_t> dropped_ { 0 };
};

struct CcSeen
{
    std::uint8_t channel = 0; // 0..15
    std::uint8_t cc = 0;
    std::uint8_t value = 0;
};

struct CcApplied
{
    std::int16_t paramIndex = -1;
    float normalised = 0.0f; // host-normalised target value
};

/// CC value (0..127) -> normalised target per the spec: t = v/127, invert flips, n = min + (max - min) * t.
inline float ccToNormalised (int value, const CcMapping& m) noexcept
{
    float t = static_cast<float> (value < 0 ? 0 : (value > 127 ? 127 : value)) * (1.0f / 127.0f);
    if (value >= 127) t = 1.0f;
    if (m.invert) t = 1.0f - t;
    return m.minNorm + (m.maxNorm - m.minNorm) * t;
}

/// Audio-thread side of the hook. One instance per processor.
class CcRouter
{
public:
    void reset() noexcept
    {
        table_ = CcMapTable {};
        seq_ = 0;
        for (std::size_t i = 0; i < active_.size(); ++i) { active_[i] = false; needSeen_[i] = false; }
    }

    /// Message thread: while true, every non-reserved CC seen is pushed to seenQueue().
    void setLearnActive (bool on) noexcept { learnActive_.store (on, std::memory_order_relaxed); }
    bool learnActive() const noexcept { return learnActive_.load (std::memory_order_relaxed); }

    /// Audio thread, once per block.
    void refresh (const CcMapPublisher& pub) noexcept
    {
        if (pub.fetch (table_, seq_))
            for (std::size_t i = 0; i < active_.size(); ++i) { active_[i] = false; needSeen_[i] = false; }
    }

    /// Audio thread, per CC event. Returns true when a mapping consumed the message. `channel0` is 0..15.
    bool handleCc (int channel0, int cc, int value) noexcept
    {
        if (cc < 0 || cc > 127 || isReservedCc (cc))
            return false;
        if (learnActive_.load (std::memory_order_relaxed))
            seen_.push ({ static_cast<std::uint8_t> (channel0 & 15), static_cast<std::uint8_t> (cc), static_cast<std::uint8_t> (value & 127) });
        for (int i = 0; i < table_.count; ++i)
        {
            const auto& m = table_.maps[static_cast<std::size_t> (i)];
            if (m.cc != cc || (m.channel != 0 && m.channel != channel0 + 1))
                continue;
            const auto idx = static_cast<std::size_t> (m.paramIndex);
            const float n = ccToNormalised (value, m);
            overrideValue_[idx] = parameterTable[idx].fromNormalized (n);
            active_[idx] = true;
            needSeen_[idx] = true;
            applied_.push ({ m.paramIndex, n });
            return true;
        }
        return false;
    }

    /// Audio thread, once per block after the snapshot was read: replaces the values of CC-driven parameters.
    void applyOverrides (ParamSnapshot& s) noexcept
    {
        for (int i = 0; i < table_.count; ++i)
        {
            const auto idx = static_cast<std::size_t> (table_.maps[static_cast<std::size_t> (i)].paramIndex);
            if (! active_[idx])
                continue;
            const auto& d = parameterTable[idx];
            const float v = s.v[idx];
            if (needSeen_[idx])
            {
                seenValue_[idx] = v;
                needSeen_[idx] = false;
            }
            const float tol = 1.0e-4f * (d.maximum - d.minimum);
            if (std::fabs (v - overrideValue_[idx]) <= tol)
                active_[idx] = false;                    // the parameter itself caught up
            else if (v == seenValue_[idx])
                s.v[idx] = overrideValue_[idx];          // parameter unchanged since the CC: CC wins
            else
                active_[idx] = false;                    // moved by UI / automation meanwhile: they win
        }
    }

    SpscRing<CcSeen, 256>& seenQueue() noexcept { return seen_; }
    SpscRing<CcApplied, 512>& appliedQueue() noexcept { return applied_; }

private:
    CcMapTable table_;
    std::uint32_t seq_ = 0;
    std::atomic<bool> learnActive_ { false };
    std::array<float, static_cast<std::size_t> (parameterCount)> overrideValue_ {}, seenValue_ {};
    std::array<bool, static_cast<std::size_t> (parameterCount)> active_ {}, needSeen_ {};
    SpscRing<CcSeen, 256> seen_;
    SpscRing<CcApplied, 512> applied_;
};
} // namespace snaral
