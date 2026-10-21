#pragma once

// Self-contained RIFF/WAVE writer (no external dependencies, no RF64) with atomic temp+rename commit.
// 16/24-bit integer PCM (tag 1), 32-bit float (tag 3 + fact), mono..8 channels, any sample rate.
// JUCE-free; never call from the audio thread. Spec: docs/specs/cdl-offline-wav-export.md.
//
// File layout written (all little endian): RIFF, WAVE, 'fmt ' (16 bytes), ['fact' for float],
// 'LIST'/'INFO' (ISFT software, INAM title, ICRD date; omitted fields skipped), 'data'.

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace snaral::wav
{
enum class SampleFormat { Pcm16, Pcm24, Float32 };

inline constexpr int bitsOf (SampleFormat f) noexcept { return f == SampleFormat::Pcm16 ? 16 : (f == SampleFormat::Pcm24 ? 24 : 32); }

struct Format
{
    int sampleRate = 48000;     // 1 .. 768000
    int channels = 2;           // 1 .. 8
    SampleFormat sample = SampleFormat::Pcm24;
};

struct Metadata
{
    std::string software;       // "Snaral 0.2.0"
    std::string title;          // preset name
    std::string dateUtc;        // "YYYY-MM-DD" (UTC date only)
};

enum class Status
{
    Ok, InvalidFormat, TooLarge, PermissionDenied, DiskFull, WriteError, RenameFailed, NotOpen
};

struct Result
{
    Status status = Status::Ok;
    std::string message;
    explicit operator bool() const noexcept { return status == Status::Ok; }
    static Result ok() { return {}; }
};

/// Largest data payload we write (classic RIFF limit with headroom).
inline constexpr std::uint64_t maxDataBytes = 0xFFFFFFFFull - 4096;

struct WriterHooks
{
    /// Test seam: called before each file write with the bytes about to be written and the total written so far.
    /// Return false to simulate a failed write (reported as DiskFull).
    std::function<bool (std::uint64_t bytesWrittenSoFar, std::size_t nextChunk)> beforeWrite;
    /// Test seam: called after the temp file is complete and flushed, before the rename. false = simulate failure.
    std::function<bool()> beforeCommit;
};

struct WriteStats
{
    std::uint64_t frames = 0;
    std::uint64_t clippedSamples = 0;   // integer conversion clamped (float output never clips)
    std::uint64_t nonFiniteSamples = 0; // NaN/Inf written as 0
    float peak = 0.0f;                  // max |sample| of the input (linear)
};

/// Streaming writer. Typical use: open(), write() repeatedly, finish(). Destruction without finish() aborts
/// and removes the temp file; an existing destination is never touched before finish() succeeds.
class WavFileWriter
{
public:
    WavFileWriter();
    ~WavFileWriter();
    WavFileWriter (const WavFileWriter&) = delete;
    WavFileWriter& operator= (const WavFileWriter&) = delete;

    /// Creates parent directories as needed and a uniquely named temp file next to `destination`.
    Result open (const std::filesystem::path& destination, const Format& format, const Metadata& meta = {},
                 const WriterHooks* hooks = nullptr);

    /// Planar input, `channels` pointers of `numFrames` floats. 16-bit output applies seeded TPDF dither
    /// (1 LSB peak, fixed seed, so output is repeatable); 24-bit and float do not dither.
    Result write (const float* const* planar, int numFrames);

    /// Patches the header sizes, flushes to disk and renames over the destination.
    Result finish();
    /// Closes and deletes the temp file. Safe to call repeatedly.
    void abort() noexcept;

    bool isOpen() const noexcept;
    const WriteStats& stats() const noexcept;
    const std::filesystem::path& tempPath() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/// Convenience: whole-buffer write (planar). Atomic.
Result writeWavFile (const std::filesystem::path& destination, const Format& format, const float* const* planar,
                     std::uint64_t numFrames, const Metadata& meta = {}, WriteStats* stats = nullptr,
                     const WriterHooks* hooks = nullptr);

/// Total file size in bytes for planning/space checks (header + data, padded).
std::uint64_t estimatedFileBytes (const Format& format, std::uint64_t frames, const Metadata& meta = {});

/// Pure conversion used by the writer (exposed for tests): round(x*2^(b-1)) clamped; sets `clipped`.
std::int32_t toInt (float x, int bits, bool& clipped) noexcept;

// ---- naming helpers (spec "File naming and destination")
std::string presetSlug (std::string_view presetName);                     // lower-case a-z0-9 runs -> '-', <=48, "init" if empty
std::string defaultExportFileName (std::string_view presetName, int sampleRate, SampleFormat f); // snaral_<slug>_<rate>_<16|24|32f>.wav
/// First free path: `path`, else stem_2.ext, stem_3.ext, ... (never overwrites).
std::filesystem::path uniqueFilePath (const std::filesystem::path& path);
} // namespace snaral::wav
