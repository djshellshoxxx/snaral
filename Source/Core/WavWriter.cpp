#include "Core/WavWriter.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <system_error>
#include <vector>

#include "State/Pure/AtomicFile.h"

#if defined(_WIN32)
 #include <io.h>
#else
 #include <unistd.h>
#endif

namespace snaral::wav
{
namespace fs = std::filesystem;

std::int32_t toInt (float x, int bits, bool& clipped) noexcept
{
    const double scale = static_cast<double> (1u << (bits - 1));
    const double hi = scale - 1.0, lo = -scale;
    double v = std::round (static_cast<double> (x) * scale);
    clipped = v > hi || v < lo;
    v = v > hi ? hi : (v < lo ? lo : v);
    return static_cast<std::int32_t> (v);
}

namespace
{
void put16 (std::vector<std::uint8_t>& b, std::uint32_t v) { b.push_back (v & 255); b.push_back ((v >> 8) & 255); }
void put32 (std::vector<std::uint8_t>& b, std::uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back ((v >> (8 * i)) & 255); }
void putTag (std::vector<std::uint8_t>& b, const char* t) { for (int i = 0; i < 4; ++i) b.push_back (static_cast<std::uint8_t> (t[i])); }

void putInfoChunk (std::vector<std::uint8_t>& b, const char* id, const std::string& s)
{
    if (s.empty()) return;
    putTag (b, id);
    const std::uint32_t len = static_cast<std::uint32_t> (s.size() + 1); // NUL terminated
    put32 (b, len);
    b.insert (b.end(), s.begin(), s.end());
    b.push_back (0);
    if (len & 1) b.push_back (0);
}

std::vector<std::uint8_t> infoList (const Metadata& m)
{
    std::vector<std::uint8_t> body;
    putInfoChunk (body, "ISFT", m.software);
    putInfoChunk (body, "INAM", m.title);
    putInfoChunk (body, "ICRD", m.dateUtc);
    std::vector<std::uint8_t> out;
    if (body.empty()) return out;
    putTag (out, "LIST");
    put32 (out, static_cast<std::uint32_t> (body.size() + 4));
    putTag (out, "INFO");
    out.insert (out.end(), body.begin(), body.end());
    return out;
}

bool validFormat (const Format& f)
{
    return f.sampleRate >= 1 && f.sampleRate <= 768000 && f.channels >= 1 && f.channels <= 8;
}

struct HeaderLayout { std::vector<std::uint8_t> bytes; std::size_t riffSizeOffset = 4, factFramesOffset = 0, dataSizeOffset = 0; };

HeaderLayout makeHeader (const Format& f, const Metadata& meta)
{
    HeaderLayout h;
    auto& b = h.bytes;
    const bool isFloat = f.sample == SampleFormat::Float32;
    const int bits = bitsOf (f.sample);
    putTag (b, "RIFF"); put32 (b, 0); putTag (b, "WAVE");
    putTag (b, "fmt "); put32 (b, 16);
    put16 (b, isFloat ? 3 : 1);
    put16 (b, static_cast<std::uint32_t> (f.channels));
    put32 (b, static_cast<std::uint32_t> (f.sampleRate));
    const std::uint32_t blockAlign = static_cast<std::uint32_t> (f.channels * bits / 8);
    put32 (b, blockAlign * static_cast<std::uint32_t> (f.sampleRate));
    put16 (b, blockAlign);
    put16 (b, static_cast<std::uint32_t> (bits));
    if (isFloat)
    {
        putTag (b, "fact"); put32 (b, 4);
        h.factFramesOffset = b.size();
        put32 (b, 0);
    }
    const auto info = infoList (meta);
    b.insert (b.end(), info.begin(), info.end());
    putTag (b, "data");
    h.dataSizeOffset = b.size();
    put32 (b, 0);
    return h;
}

Status classify (int err)
{
    if (err == ENOSPC || err == EFBIG
#ifdef EDQUOT
        || err == EDQUOT
#endif
        ) return Status::DiskFull;
    if (err == EACCES || err == EPERM || err == EROFS) return Status::PermissionDenied;
    return Status::WriteError;
}
Result makeErr (Status s, const std::string& what, int err)
{
    return { s, what + (err != 0 ? ": " + std::generic_category().message (err) : std::string()) };
}

std::string uniqueTempName (const fs::path& dest)
{
    static std::atomic<unsigned> counter { 0 };
    const auto t = std::chrono::steady_clock::now().time_since_epoch().count();
    return fileio::pathToUtf8 (dest.filename()) + ".snaral-" + std::to_string (t) + "-" + std::to_string (counter++) + ".tmp";
}

struct Dither
{
    std::uint64_t s = 0x1234ABCD9876EF01ull; // fixed seed: repeatable
    double next() noexcept // uniform [0,1)
    {
        std::uint64_t z = (s += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        z ^= z >> 31;
        return static_cast<double> (z >> 11) * (1.0 / 9007199254740992.0);
    }
    double tpdf() noexcept { return next() - next(); } // triangular in (-1,1) LSB
};
} // namespace

struct WavFileWriter::Impl
{
    Format format;
    Metadata meta;
    WriterHooks hooks;
    fs::path dest, temp;
    std::FILE* file = nullptr;
    HeaderLayout header;
    std::uint64_t dataBytes = 0, written = 0;
    WriteStats stats;
    Dither dither;
    std::vector<std::uint8_t> buffer;
    bool finished = false;

    void closeFile() noexcept { if (file) { std::fclose (file); file = nullptr; } }
    void removeTemp() noexcept { std::error_code ec; if (! temp.empty()) fs::remove (temp, ec); }

    Result rawWrite (const void* p, std::size_t n)
    {
        if (hooks.beforeWrite && ! hooks.beforeWrite (written, n))
            return { Status::DiskFull, "Simulated write failure (disk full)" };
        errno = 0;
        if (std::fwrite (p, 1, n, file) != n)
        {
            const int e = errno;
            return makeErr (classify (e), "Write failed", e);
        }
        written += n;
        return Result::ok();
    }
};

WavFileWriter::WavFileWriter() : impl_ (std::make_unique<Impl>()) {}
WavFileWriter::~WavFileWriter() { abort(); }
bool WavFileWriter::isOpen() const noexcept { return impl_->file != nullptr; }
const WriteStats& WavFileWriter::stats() const noexcept { return impl_->stats; }
const fs::path& WavFileWriter::tempPath() const noexcept { return impl_->temp; }

Result WavFileWriter::open (const fs::path& destination, const Format& format, const Metadata& meta, const WriterHooks* hooks)
{
    abort();
    auto& m = *impl_;
    m = Impl();
    if (! validFormat (format))
        return { Status::InvalidFormat, "Unsupported sample rate or channel count" };
    if (fs::is_directory (destination))
        return { Status::WriteError, "Destination is a directory" };
    m.format = format;
    m.meta = meta;
    if (hooks) m.hooks = *hooks;
    m.dest = destination;

    std::error_code ec;
    const auto parent = destination.has_parent_path() ? destination.parent_path() : fs::path (".");
    fs::create_directories (parent, ec);
    if (ec)
        return makeErr (classify (ec.value()), "Cannot create destination folder", ec.value());

    m.temp = parent / fileio::pathFromUtf8 (uniqueTempName (destination));
    errno = 0;
#if defined(_WIN32)
    m.file = _wfopen (m.temp.c_str(), L"wb");
#else
    m.file = std::fopen (m.temp.c_str(), "wb");
#endif
    if (! m.file)
    {
        const int e = errno;
        m.temp.clear();
        return makeErr (classify (e), "Cannot create file in destination folder", e);
    }
    m.header = makeHeader (format, meta);
    if (auto r = m.rawWrite (m.header.bytes.data(), m.header.bytes.size()); ! r)
    {
        m.closeFile(); m.removeTemp();
        return r;
    }
    return Result::ok();
}

Result WavFileWriter::write (const float* const* planar, int numFrames)
{
    auto& m = *impl_;
    if (! m.file)
        return { Status::NotOpen, "Writer is not open" };
    if (numFrames <= 0)
        return Result::ok();
    const int ch = m.format.channels;
    const int bytesPer = bitsOf (m.format.sample) / 8;
    const std::uint64_t add = static_cast<std::uint64_t> (numFrames) * static_cast<std::uint64_t> (ch * bytesPer);
    if (m.dataBytes + add > maxDataBytes)
        return { Status::TooLarge, "File would exceed the 4 GB WAV limit" };

    m.buffer.resize (static_cast<std::size_t> (add));
    std::uint8_t* p = m.buffer.data();
    for (int i = 0; i < numFrames; ++i)
    {
        for (int c = 0; c < ch; ++c)
        {
            float x = planar[c][i];
            if (! std::isfinite (x))
            {
                ++m.stats.nonFiniteSamples;
                x = 0.0f;
            }
            m.stats.peak = std::max (m.stats.peak, std::abs (x));
            switch (m.format.sample)
            {
                case SampleFormat::Float32:
                {
                    std::uint32_t u;
                    std::memcpy (&u, &x, 4);
                    for (int k = 0; k < 4; ++k) *p++ = static_cast<std::uint8_t> (u >> (8 * k));
                    break;
                }
                case SampleFormat::Pcm24:
                {
                    bool clip;
                    const auto v = static_cast<std::uint32_t> (toInt (x, 24, clip));
                    m.stats.clippedSamples += clip ? 1 : 0;
                    *p++ = static_cast<std::uint8_t> (v); *p++ = static_cast<std::uint8_t> (v >> 8); *p++ = static_cast<std::uint8_t> (v >> 16);
                    break;
                }
                case SampleFormat::Pcm16:
                {
                    bool clip;
                    const double d = m.dither.tpdf();
                    // Dither is added in LSB units before rounding; clipping is judged on the undithered value.
                    toInt (x, 16, clip);
                    double v = std::round (static_cast<double> (x) * 32768.0 + d);
                    v = v > 32767.0 ? 32767.0 : (v < -32768.0 ? -32768.0 : v);
                    m.stats.clippedSamples += clip ? 1 : 0;
                    const auto u = static_cast<std::uint16_t> (static_cast<std::int16_t> (v));
                    *p++ = static_cast<std::uint8_t> (u); *p++ = static_cast<std::uint8_t> (u >> 8);
                    break;
                }
            }
        }
    }
    if (auto r = m.rawWrite (m.buffer.data(), m.buffer.size()); ! r)
        return r;
    m.dataBytes += add;
    m.stats.frames += static_cast<std::uint64_t> (numFrames);
    return Result::ok();
}

Result WavFileWriter::finish()
{
    auto& m = *impl_;
    if (! m.file)
        return { Status::NotOpen, "Writer is not open" };
    auto fail = [&m] (Result r) { m.closeFile(); m.removeTemp(); m.temp.clear(); return r; };

    if (m.dataBytes & 1)
        if (auto r = m.rawWrite ("\0", 1); ! r) return fail (r);
    const std::uint64_t riffSize = m.header.bytes.size() - 8 + m.dataBytes + (m.dataBytes & 1);
    auto patch = [&m] (std::size_t off, std::uint32_t v) -> bool
    {
        std::uint8_t b[4] = { static_cast<std::uint8_t> (v), static_cast<std::uint8_t> (v >> 8), static_cast<std::uint8_t> (v >> 16), static_cast<std::uint8_t> (v >> 24) };
        return std::fseek (m.file, static_cast<long> (off), SEEK_SET) == 0 && std::fwrite (b, 1, 4, m.file) == 4;
    };
    bool ok = patch (m.header.riffSizeOffset, static_cast<std::uint32_t> (riffSize))
              && patch (m.header.dataSizeOffset, static_cast<std::uint32_t> (m.dataBytes));
    if (ok && m.header.factFramesOffset != 0)
        ok = patch (m.header.factFramesOffset, static_cast<std::uint32_t> (m.stats.frames));
    errno = 0;
    ok = ok && std::fflush (m.file) == 0;
    if (! ok)
    {
        const int e = errno;
        return fail (makeErr (classify (e), "Could not finalise header", e));
    }
#if defined(_WIN32)
    _commit (_fileno (m.file));
#else
    fsync (fileno (m.file));
#endif
    errno = 0;
    if (std::fclose (m.file) != 0)
    {
        const int e = errno;
        m.file = nullptr;
        return fail (makeErr (classify (e), "Could not close file", e));
    }
    m.file = nullptr;

    if (m.hooks.beforeCommit && ! m.hooks.beforeCommit())
        return fail ({ Status::WriteError, "Simulated failure before commit" });

    std::error_code ec;
    fs::rename (m.temp, m.dest, ec);
    if (ec)
    {
        // Windows cannot rename over an existing file with some filesystems; fall back to replace semantics.
        std::error_code ec2;
        fs::remove (m.dest, ec2);
        fs::rename (m.temp, m.dest, ec2);
        if (ec2)
        {
            m.removeTemp(); m.temp.clear();
            return makeErr (Status::RenameFailed, "Could not move finished file into place", ec2.value());
        }
    }
    m.finished = true;
    m.temp.clear();
    return Result::ok();
}

void WavFileWriter::abort() noexcept
{
    if (! impl_) return;
    impl_->closeFile();
    if (! impl_->finished)
        impl_->removeTemp();
    impl_->temp.clear();
}

Result writeWavFile (const fs::path& destination, const Format& format, const float* const* planar, std::uint64_t numFrames,
                     const Metadata& meta, WriteStats* stats, const WriterHooks* hooks)
{
    WavFileWriter w;
    if (auto r = w.open (destination, format, meta, hooks); ! r) return r;
    std::vector<const float*> ptrs (static_cast<std::size_t> (format.channels));
    std::uint64_t done = 0;
    while (done < numFrames)
    {
        const int n = static_cast<int> (std::min<std::uint64_t> (numFrames - done, 65536));
        for (int c = 0; c < format.channels; ++c) ptrs[static_cast<std::size_t> (c)] = planar[c] + done;
        if (auto r = w.write (ptrs.data(), n); ! r) return r;
        done += static_cast<std::uint64_t> (n);
    }
    auto r = w.finish();
    if (stats) *stats = w.stats();
    return r;
}

std::uint64_t estimatedFileBytes (const Format& f, std::uint64_t frames, const Metadata& meta)
{
    const auto h = makeHeader (f, meta);
    const std::uint64_t data = frames * static_cast<std::uint64_t> (f.channels * bitsOf (f.sample) / 8);
    return h.bytes.size() + data + (data & 1);
}

std::string presetSlug (std::string_view name)
{
    std::string out;
    bool pendingDash = false;
    for (char c : name)
    {
        const char l = (c >= 'A' && c <= 'Z') ? static_cast<char> (c - 'A' + 'a') : c;
        if ((l >= 'a' && l <= 'z') || (l >= '0' && l <= '9'))
        {
            if (pendingDash && ! out.empty()) out.push_back ('-');
            pendingDash = false;
            out.push_back (l);
        }
        else
            pendingDash = true;
    }
    if (out.size() > 48) out.resize (48);
    while (! out.empty() && out.back() == '-') out.pop_back();
    return out.empty() ? "init" : out;
}

std::string defaultExportFileName (std::string_view presetName, int sampleRate, SampleFormat f)
{
    const char* bits = f == SampleFormat::Pcm16 ? "16" : (f == SampleFormat::Pcm24 ? "24" : "32f");
    return "snaral_" + presetSlug (presetName) + "_" + std::to_string (sampleRate) + "_" + bits + ".wav";
}

fs::path uniqueFilePath (const fs::path& path)
{
    std::error_code ec;
    if (! fs::exists (path, ec))
        return path;
    const auto stem = path.stem().string();
    const auto ext = path.extension().string();
    for (int i = 2; i < 100000; ++i)
    {
        auto candidate = path.parent_path() / (stem + "_" + std::to_string (i) + ext);
        if (! fs::exists (candidate, ec))
            return candidate;
    }
    return path;
}
} // namespace snaral::wav
