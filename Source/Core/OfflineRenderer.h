#pragma once

// Deterministic block-based offline renderer: MIDI phrase -> any engine -> WAV file (or memory).
// JUCE-free, header-only template over the engine interface of docs/IMPLEMENTATION_PLAN.md:
//   prepare(double sr, int maxBlock) / reset() / setParameters(const ParamSnapshot&) / setHostTiming(const HostTiming&)
//   handleMidi(const EngineMidiEvent&) / render(float* l, float* r, int n) / panic()   [+ optional int latencySamples() const]
// Worker thread only. Spec: docs/specs/cdl-offline-wav-export.md.
//
// Determinism: for identical engine, snapshot, phrase and settings the audio is identical, regardless of the
// progress callback, wall clock or file settings. Blocks are `blockSize` (512) frames; the block is split at each
// MIDI event so the engine receives events with sampleOffset 0 at the exact sample.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "Core/MidiLearn.h"
#include "Core/WavWriter.h"
#include "Engine/EngineTypes.h"
#include "Parameters/ParameterTable.h"

namespace snaral::render
{
struct TimedNote
{
    double startBeats = 0.0;
    double lengthBeats = 1.0;
    std::uint8_t note = 60;
    std::uint8_t velocity = 100;
    std::uint8_t channel = 0;   // 0..15
};

struct TimedCc
{
    double beats = 0.0;
    std::uint8_t cc = 0;
    std::uint8_t value = 0;
    std::uint8_t channel = 0;   // 0..15
};

struct MidiPhrase
{
    std::vector<TimedNote> notes;
    std::vector<TimedCc> ccs;
    /// Phrase end in beats. < 0 (default) = time of the last event (latest note-off or CC).
    double endBeats = -1.0;

    double computedEndBeats() const;
};

struct ExportSettings
{
    double bpm = 174.0;                 // 20 .. 400
    int sampleRate = 48000;
    int channels = 2;                   // 1 (mono = (L+R)/2) or 2
    wav::SampleFormat sample = wav::SampleFormat::Pcm24;
    double tailSeconds = 2.0;           // 0 .. 30, fixed (no early stop)
    bool normalise = false;             // one linear gain after the render so the peak is -1.0 dBFS
    int blockSize = 512;
    double maxLengthSeconds = 600.0;
    wav::Metadata metadata;             // software/title/date for the LIST chunk
    const midi::MidiLearnTable* learnTable = nullptr; // optional: CC events in the phrase drive mapped parameters
    double progressIntervalMs = 100.0;  // minimum wall-clock gap between progress callbacks (0 = every block)
    const wav::WriterHooks* writerHooks = nullptr; // tests
};

struct Progress
{
    std::uint64_t renderedFrames = 0;
    std::uint64_t totalFrames = 0;      // engine frames including latency
    double elapsedSeconds = 0.0;
    float fraction() const noexcept { return totalFrames ? static_cast<float> (static_cast<double> (renderedFrames) / static_cast<double> (totalFrames)) : 1.0f; }
};

enum class ExportStatus
{
    Ok, Cancelled, InvalidSettings, InvalidPhrase, TooLong, InsufficientSpace, PermissionDenied, DiskFull, WriteError, EngineError
};

struct ExportResult
{
    ExportStatus status = ExportStatus::Ok;
    std::string message;                 // user-facing reason
    std::filesystem::path path;
    std::uint64_t frames = 0;            // frames in the file = round((phraseEnd + tail) * rate)
    double durationSeconds = 0.0;
    int latencyTrimmed = 0;
    float peak = 0.0f;                   // linear sample peak of the written audio (after normalisation gain)
    double peakDbfs = -200.0;
    std::uint64_t clippedSamples = 0;
    double normaliseGain = 1.0;
    double elapsedSeconds = 0.0;
    explicit operator bool() const noexcept { return status == ExportStatus::Ok; }
};

/// Rendered audio in memory (the "reference render driver" output).
struct RenderedAudio
{
    std::vector<float> left, right;
    int latencyTrimmed = 0;
    ExportStatus status = ExportStatus::Ok;
    std::string message;
};

struct Callbacks
{
    std::function<void (const Progress&)> progress;
    std::function<bool()> cancelled;     // polled once per block; true = stop
};

// ------------------------------------------------------------ non-template helpers (OfflineRenderCommon.cpp)

struct Timeline
{
    struct Item { std::int64_t sample = 0; int order = 0; bool isCc = false; EngineMidiEvent ev; TimedCc cc; };
    std::vector<Item> items;            // sorted by (sample, order)
    std::int64_t phraseEndSample = 0;
    std::int64_t totalSamples = 0;      // phraseEnd + tail
};

/// Validates settings and phrase; empty string = ok. Sets `status` on failure.
std::string validateExport (const ExportSettings& s, const MidiPhrase& phrase, ExportStatus& status);
/// Builds the sorted event list. At phrase end every still-held note gets a note-off and the pedal is released.
Timeline buildTimeline (const ExportSettings& s, const MidiPhrase& phrase);
/// Sample position of a beat position: llround(beats * 60 / bpm * rate).
std::int64_t beatsToSample (double beats, double bpm, int sampleRate) noexcept;
/// total frames for settings (no tail clamping): llround((phraseEndBeats*60/bpm + tail) * rate)
std::int64_t exportFrames (const ExportSettings& s, const MidiPhrase& phrase);

// ------------------------------------------------------------ renderer

template <class Engine>
class OfflineRenderer
{
public:
    explicit OfflineRenderer (Engine& engine) : engine_ (engine) {}

    /// Renders to memory (stereo, latency trimmed, no normalisation, no mono mixdown).
    RenderedAudio renderToMemory (const ParamSnapshot& patch, const MidiPhrase& phrase, const ExportSettings& s,
                                  const Callbacks& cb = {})
    {
        RenderedAudio out;
        ExportStatus st;
        if (auto e = validateExport (s, phrase, st); ! e.empty()) { out.status = st; out.message = e; return out; }
        const auto tl = buildTimeline (s, phrase);
        out.left.reserve (static_cast<std::size_t> (tl.totalSamples));
        out.right.reserve (static_cast<std::size_t> (tl.totalSamples));
        auto sink = [&out] (const float* l, const float* r, int n) {
            out.left.insert (out.left.end(), l, l + n);
            out.right.insert (out.right.end(), r, r + n);
            return true;
        };
        const auto run = runLoop (patch, s, tl, cb, sink);
        out.status = run.status; out.message = run.message; out.latencyTrimmed = run.latency;
        return out;
    }

    /// Renders to `destination` (atomic replace). Never touches an existing destination unless it succeeds.
    ExportResult renderToFile (const ParamSnapshot& patch, const MidiPhrase& phrase, const ExportSettings& s,
                               const std::filesystem::path& destination, const Callbacks& cb = {})
    {
        namespace fs = std::filesystem;
        const auto t0 = std::chrono::steady_clock::now();
        ExportResult res;
        res.path = destination;
        auto finish = [&] (ExportStatus st, std::string msg) {
            res.status = st; res.message = std::move (msg);
            res.elapsedSeconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
            return res;
        };

        ExportStatus st;
        if (auto e = validateExport (s, phrase, st); ! e.empty())
            return finish (st, e);
        const auto tl = buildTimeline (s, phrase);
        res.frames = static_cast<std::uint64_t> (tl.totalSamples);
        res.durationSeconds = static_cast<double> (tl.totalSamples) / s.sampleRate;

        wav::Format fmt { s.sampleRate, s.channels, s.sample };
        // Free space check: 2 x final size (the normalise path also holds a float scratch file).
        {
            std::error_code ec;
            auto dir = destination.has_parent_path() ? destination.parent_path() : fs::path (".");
            fs::create_directories (dir, ec);
            const auto info = fs::space (dir, ec);
            const auto need = 2 * wav::estimatedFileBytes (fmt, res.frames, s.metadata);
            if (! ec && info.available < need)
                return finish (ExportStatus::InsufficientSpace, "Not enough free disk space: about " + std::to_string (need / (1024 * 1024) + 1) + " MB needed.");
        }

        wav::WavFileWriter writer;
        std::FILE* scratch = nullptr;
        fs::path scratchPath;
        auto cleanup = [&] {
            writer.abort();
            if (scratch) { std::fclose (scratch); scratch = nullptr; }
            if (! scratchPath.empty()) { std::error_code ec; fs::remove (scratchPath, ec); scratchPath.clear(); }
        };

        float peak = 0.0f;
        std::uint64_t written = 0;
        std::string ioError;
        ExportStatus ioStatus = ExportStatus::Ok;
        std::vector<float> mono;
        auto failFromWav = [&] (const wav::Result& r) {
            ioStatus = r.status == wav::Status::DiskFull ? ExportStatus::DiskFull
                     : r.status == wav::Status::PermissionDenied ? ExportStatus::PermissionDenied : ExportStatus::WriteError;
            ioError = r.message;
        };

        if (s.normalise)
        {
            // Raw interleaved 32-bit float scratch next to the destination (native endianness, private).
            scratchPath = destination;
            scratchPath += ".snaral-render.tmp";
            scratch = std::fopen (scratchPath.string().c_str(), "wb");
            if (! scratch)
            {
                scratchPath.clear();
                return finish (ExportStatus::PermissionDenied, "Cannot create a temporary file in the destination folder.");
            }
        }
        else if (auto r = writer.open (destination, fmt, s.metadata, s.writerHooks); ! r)
        {
            failFromWav (r);
            return finish (ioStatus, ioError);
        }

        auto sink = [&] (const float* l, const float* r, int n) -> bool {
            const float* planar[2] = { l, r };
            if (s.channels == 1)
            {
                mono.resize (static_cast<std::size_t> (n));
                for (int i = 0; i < n; ++i) mono[static_cast<std::size_t> (i)] = 0.5f * (l[i] + r[i]);
                planar[0] = mono.data();
            }
            for (int c = 0; c < s.channels; ++c)
                for (int i = 0; i < n; ++i)
                    if (std::isfinite (planar[c][i])) peak = std::max (peak, std::abs (planar[c][i]));
            written += static_cast<std::uint64_t> (n);
            if (scratch)
            {
                std::vector<float> inter (static_cast<std::size_t> (n) * static_cast<std::size_t> (s.channels));
                for (int i = 0; i < n; ++i)
                    for (int c = 0; c < s.channels; ++c)
                        inter[static_cast<std::size_t> (i * s.channels + c)] = std::isfinite (planar[c][i]) ? planar[c][i] : 0.0f;
                if (std::fwrite (inter.data(), sizeof (float), inter.size(), scratch) != inter.size())
                {
                    ioStatus = ExportStatus::DiskFull; ioError = "Could not write the temporary render file (disk full?).";
                    return false;
                }
                return true;
            }
            if (auto w = writer.write (planar, n); ! w) { failFromWav (w); return false; }
            return true;
        };

        const auto run = runLoop (patch, s, tl, cb, sink);
        res.latencyTrimmed = run.latency;
        if (ioStatus != ExportStatus::Ok)
        {
            cleanup();
            return finish (ioStatus, ioError);
        }
        if (run.status != ExportStatus::Ok)
        {
            cleanup();
            return finish (run.status, run.message);
        }

        if (s.normalise)
        {
            std::fflush (scratch);
            std::fclose (scratch);
            scratch = std::fopen (scratchPath.string().c_str(), "rb");
            if (! scratch) { cleanup(); return finish (ExportStatus::WriteError, "Cannot re-open the temporary render file."); }
            const double target = std::pow (10.0, -1.0 / 20.0);
            res.normaliseGain = peak > 0.0f ? target / static_cast<double> (peak) : 1.0;
            if (auto r = writer.open (destination, fmt, s.metadata, s.writerHooks); ! r) { failFromWav (r); cleanup(); return finish (ioStatus, ioError); }
            const int chunk = 4096;
            std::vector<float> inter (static_cast<std::size_t> (chunk) * static_cast<std::size_t> (s.channels));
            std::vector<std::vector<float>> planar (static_cast<std::size_t> (s.channels), std::vector<float> (chunk));
            std::vector<const float*> ptrs (static_cast<std::size_t> (s.channels));
            std::uint64_t left = written;
            while (left > 0)
            {
                if (cb.cancelled && cb.cancelled()) { cleanup(); return finish (ExportStatus::Cancelled, "Export cancelled."); }
                const int n = static_cast<int> (std::min<std::uint64_t> (left, chunk));
                if (std::fread (inter.data(), sizeof (float), static_cast<std::size_t> (n) * static_cast<std::size_t> (s.channels), scratch)
                    != static_cast<std::size_t> (n) * static_cast<std::size_t> (s.channels))
                { cleanup(); return finish (ExportStatus::WriteError, "Could not read back the temporary render file."); }
                for (int c = 0; c < s.channels; ++c)
                {
                    for (int i = 0; i < n; ++i)
                        planar[static_cast<std::size_t> (c)][static_cast<std::size_t> (i)] =
                            static_cast<float> (static_cast<double> (inter[static_cast<std::size_t> (i * s.channels + c)]) * res.normaliseGain);
                    ptrs[static_cast<std::size_t> (c)] = planar[static_cast<std::size_t> (c)].data();
                }
                if (auto w = writer.write (ptrs.data(), n); ! w) { failFromWav (w); cleanup(); return finish (ioStatus, ioError); }
                left -= static_cast<std::uint64_t> (n);
            }
            peak = static_cast<float> (static_cast<double> (peak) * res.normaliseGain);
        }

        if (cb.cancelled && cb.cancelled()) { cleanup(); return finish (ExportStatus::Cancelled, "Export cancelled."); }
        if (auto r = writer.finish(); ! r) { failFromWav (r); cleanup(); return finish (ioStatus == ExportStatus::Ok ? ExportStatus::WriteError : ioStatus, ioError.empty() ? r.message : ioError); }

        res.clippedSamples = writer.stats().clippedSamples;
        cleanup();
        res.peak = peak;
        res.peakDbfs = peak > 0.0f ? 20.0 * std::log10 (static_cast<double> (peak)) : -200.0;
        return finish (ExportStatus::Ok, {});
    }

private:
    struct RunInfo { ExportStatus status = ExportStatus::Ok; std::string message; int latency = 0; };

    static int latencyOf (const Engine& e)
    {
        if constexpr (requires { e.latencySamples(); })
            return std::max (0, static_cast<int> (e.latencySamples()));
        else
            return 0;
    }

    /// Core loop. `sink(l, r, n)` receives latency-trimmed stereo blocks; returning false aborts (sink reports why).
    template <class Sink>
    RunInfo runLoop (const ParamSnapshot& patch, const ExportSettings& s, const Timeline& tl, const Callbacks& cb, Sink&& sink)
    {
        RunInfo info;
        try
        {
            const int block = std::max (16, s.blockSize);
            engine_.prepare (static_cast<double> (s.sampleRate), block);
            engine_.reset();
            const int latency = latencyOf (engine_);
            info.latency = latency;
            const std::int64_t total = tl.totalSamples + latency;

            ParamSnapshot params = patch;
            midi::MidiLearnRuntime learn;
            std::vector<float> l (static_cast<std::size_t> (block)), r (static_cast<std::size_t> (block));

            std::size_t cursor = 0;
            std::int64_t pos = 0;              // engine timeline
            std::int64_t skip = latency;
            const auto t0 = std::chrono::steady_clock::now();
            auto lastReport = t0;
            const std::int64_t maxPos = total;

            while (pos < maxPos)
            {
                if (cb.cancelled && cb.cancelled())
                {
                    info.status = ExportStatus::Cancelled; info.message = "Export cancelled.";
                    return info;
                }
                const int n = static_cast<int> (std::min<std::int64_t> (block, maxPos - pos));

                // Apply CC-mapped parameter changes that fall inside this block to the snapshot, then set once.
                std::size_t scan = cursor;
                while (scan < tl.items.size() && tl.items[scan].sample < pos + n)
                {
                    const auto& it = tl.items[scan++];
                    if (it.isCc && s.learnTable)
                    {
                        const auto a = learn.apply (*s.learnTable, it.cc.cc, it.cc.channel + 1, it.cc.value);
                        if (a.mapped && a.apply)
                            params.v[static_cast<std::size_t> (a.target)] = a.domain;
                    }
                }
                engine_.setParameters (params);

                HostTiming ht;
                ht.bpm = s.bpm; ht.bpmValid = true; ht.playing = true; ht.ppqValid = true;
                ht.ppqPosition = static_cast<double> (pos) / static_cast<double> (s.sampleRate) * s.bpm / 60.0;
                engine_.setHostTiming (ht);

                int done = 0;
                while (done < n)
                {
                    // dispatch every event at this exact sample, then render up to the next event / block end
                    while (cursor < tl.items.size() && tl.items[cursor].sample <= pos + done)
                    {
                        const auto& it = tl.items[cursor++];
                        if (it.isCc && s.learnTable && s.learnTable->find (it.cc.cc, it.cc.channel + 1) >= 0)
                            continue; // consumed by a mapping
                        EngineMidiEvent ev = it.ev;
                        ev.sampleOffset = 0;
                        engine_.handleMidi (ev);
                    }
                    int upto = n;
                    if (cursor < tl.items.size())
                        upto = static_cast<int> (std::min<std::int64_t> (n, tl.items[cursor].sample - pos));
                    upto = std::max (upto, done + 1);
                    engine_.render (l.data() + done, r.data() + done, upto - done);
                    done = upto;
                }

                // trim initial latency
                int from = 0;
                if (skip > 0)
                {
                    from = static_cast<int> (std::min<std::int64_t> (skip, n));
                    skip -= from;
                }
                if (from < n && ! sink (l.data() + from, r.data() + from, n - from))
                {
                    info.status = ExportStatus::WriteError; // sink recorded the precise cause
                    return info;
                }
                pos += n;

                if (cb.progress)
                {
                    const auto now = std::chrono::steady_clock::now();
                    const double sinceMs = std::chrono::duration<double, std::milli> (now - lastReport).count();
                    if (s.progressIntervalMs <= 0.0 || sinceMs >= s.progressIntervalMs || pos >= maxPos)
                    {
                        lastReport = now;
                        cb.progress ({ static_cast<std::uint64_t> (pos), static_cast<std::uint64_t> (total),
                                       std::chrono::duration<double> (now - t0).count() });
                    }
                }
            }
        }
        catch (const std::exception& e)
        {
            info.status = ExportStatus::EngineError;
            info.message = std::string ("Engine error during render: ") + e.what();
        }
        catch (...)
        {
            info.status = ExportStatus::EngineError;
            info.message = "Unknown engine error during render.";
        }
        return info;
    }

    Engine& engine_;
};
} // namespace snaral::render
