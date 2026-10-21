#include "AtomicFile.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <system_error>

#include "Json.h"

#if defined(_WIN32)
 #include <io.h>
 #define SNARAL_FSYNC(fp) _commit (_fileno (fp))
#else
 #include <fcntl.h>
 #include <unistd.h>
 #define SNARAL_FSYNC(fp) fsync (fileno (fp))
#endif

namespace snaral::fileio
{
namespace fs = std::filesystem;

fs::path pathFromUtf8 (std::string_view utf8)
{
    const std::u8string u8 (reinterpret_cast<const char8_t*> (utf8.data()), utf8.size());
    return fs::path (u8);
}

std::string pathToUtf8 (const fs::path& p)
{
    const auto u8 = p.generic_u8string();
    return std::string (reinterpret_cast<const char*> (u8.data()), u8.size());
}

namespace
{
std::FILE* openExclusive (const fs::path& p)
{
#if defined(_WIN32)
    return _wfopen (p.c_str(), L"wbx");
#else
    return std::fopen (p.c_str(), "wbx");
#endif
}

std::FILE* openForRead (const fs::path& p)
{
#if defined(_WIN32)
    return _wfopen (p.c_str(), L"rb");
#else
    return std::fopen (p.c_str(), "rb");
#endif
}

void syncDirectory (const fs::path& dir)
{
#if ! defined(_WIN32)
    const int fd = ::open (dir.c_str(), O_RDONLY);
    if (fd >= 0)
    {
        (void) ::fsync (fd);
        ::close (fd);
    }
#else
    (void) dir;
#endif
}

std::string nameOf (const fs::path& p)
{
    return pathToUtf8 (p.filename());
}
} // namespace

IoResult writeFileAtomic (const fs::path& destination, std::string_view bytes, const WriteHooks* hooks)
{
    try
    {
        std::error_code ec;
        if (fs::is_directory (destination, ec))
            return IoResult::failure ("cannot replace a directory: " + pathToUtf8 (destination));

        auto parent = destination.parent_path();
        if (parent.empty())
            parent = fs::path (".");
        fs::create_directories (parent, ec);
        if (ec)
            return IoResult::failure ("cannot create folder " + pathToUtf8 (parent) + ": " + ec.message());

        static std::atomic<unsigned> counter { 0 };
        const auto stamp = static_cast<unsigned long long> (
            std::chrono::steady_clock::now().time_since_epoch().count());
        char suffix[64];
        std::snprintf (suffix, sizeof (suffix), ".%llx.%x.tmp", stamp, counter.fetch_add (1));
        const fs::path temp = parent / pathFromUtf8 ("." + nameOf (destination) + suffix);

        std::FILE* f = openExclusive (temp);
        if (f == nullptr)
            return IoResult::failure ("cannot create temporary file next to " + nameOf (destination));

        bool good = bytes.empty() || std::fwrite (bytes.data(), 1, bytes.size(), f) == bytes.size();
        good = good && std::fflush (f) == 0 && SNARAL_FSYNC (f) == 0;
        good = (std::fclose (f) == 0) && good;
        if (! good)
        {
            fs::remove (temp, ec);
            return IoResult::failure ("write failed (disk full or no permission): " + nameOf (destination));
        }

        if (hooks != nullptr && hooks->beforeCommit && ! hooks->beforeCommit())
        {
            fs::remove (temp, ec);
            return IoResult::failure ("write interrupted before commit: " + nameOf (destination));
        }

        fs::rename (temp, destination, ec);
        if (ec)
        {
            const auto message = ec.message();
            fs::remove (temp, ec);
            return IoResult::failure ("cannot replace " + nameOf (destination) + ": " + message);
        }
        syncDirectory (parent);
        return IoResult::success();
    }
    catch (const std::exception& e)
    {
        return IoResult::failure (std::string ("file error: ") + e.what());
    }
}

IoResult readFileLimited (const fs::path& file, std::size_t maxBytes, std::string& out)
{
    out.clear();
    try
    {
        std::error_code ec;
        if (! fs::is_regular_file (file, ec))
            return IoResult::failure ("not a readable file: " + pathToUtf8 (file));
        const auto size = fs::file_size (file, ec);
        if (ec)
            return IoResult::failure ("cannot read " + nameOf (file) + ": " + ec.message());
        if (size > maxBytes)
            return IoResult::failure (nameOf (file) + " is too large (" + std::to_string (size) + " bytes, limit "
                                      + std::to_string (maxBytes) + ")");
        std::FILE* f = openForRead (file);
        if (f == nullptr)
            return IoResult::failure ("cannot open " + nameOf (file));
        std::string buffer (static_cast<std::size_t> (size), '\0');
        const auto got = size == 0 ? 0 : std::fread (buffer.data(), 1, buffer.size(), f);
        std::fclose (f);
        if (got != buffer.size())
            return IoResult::failure ("short read of " + nameOf (file));
        out = std::move (buffer);
        return IoResult::success();
    }
    catch (const std::exception& e)
    {
        out.clear();
        return IoResult::failure (std::string ("file error: ") + e.what());
    }
}

bool isReservedDeviceName (std::string_view stem)
{
    std::string up (stem);
    for (auto& c : up)
        if (c >= 'a' && c <= 'z')
            c = static_cast<char> (c - 'a' + 'A');
    if (up == "CON" || up == "PRN" || up == "AUX" || up == "NUL")
        return true;
    return up.size() == 4 && (up.rfind ("COM", 0) == 0 || up.rfind ("LPT", 0) == 0) && up[3] >= '0' && up[3] <= '9';
}

std::string sanitiseFileName (std::string_view name, std::size_t maxBytes)
{
    const bool utf8 = json::isValidUtf8 (name);
    std::string out;
    for (const char ch : name)
    {
        const auto c = static_cast<unsigned char> (ch);
        if (c < 0x20 || c == 0x7F)
            continue;
        if (c >= 0x80 && ! utf8)
            continue;
        if (std::string_view ("<>:\"/\\|?*").find (ch) != std::string_view::npos)
            out.push_back ('_');
        else
            out.push_back (ch);
    }

    auto trim = [&out]
    {
        while (! out.empty() && (out.front() == ' ' || out.front() == '.'))
            out.erase (out.begin());
        while (! out.empty() && (out.back() == ' ' || out.back() == '.'))
            out.pop_back();
    };
    trim();

    if (out.size() > maxBytes)
    {
        std::size_t cut = maxBytes;
        while (cut > 0 && (static_cast<unsigned char> (out[cut]) & 0xC0) == 0x80)
            --cut;
        out.resize (cut);
        trim();
    }

    if (out.empty())
        return "Untitled";
    const auto dot = out.find ('.');
    if (isReservedDeviceName (std::string_view (out).substr (0, dot)))
        out.insert (out.begin(), '_');
    return out;
}

bool fileNamesEqualIgnoreCase (std::string_view a, std::string_view b)
{
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        auto x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x = static_cast<char> (x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = static_cast<char> (y - 'A' + 'a');
        if (x != y)
            return false;
    }
    return true;
}
} // namespace snaral::fileio
