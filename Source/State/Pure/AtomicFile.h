#pragma once

// Safe file helpers for user data (presets, browser state). JUCE-free, std::filesystem based, UTF-8 paths.
// Never call from the audio thread.

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

namespace snaral::fileio
{
struct IoResult
{
    bool ok = false;
    std::string error; // short human-readable reason, includes the file name

    explicit operator bool() const noexcept { return ok; }
    static IoResult success() { return { true, {} }; }
    static IoResult failure (std::string message) { return { false, std::move (message) }; }
};

/// UTF-8 <-> filesystem path (correct on Windows, where char paths are ANSI).
std::filesystem::path pathFromUtf8 (std::string_view utf8);
std::string pathToUtf8 (const std::filesystem::path& p);

struct WriteHooks
{
    /// Test seam: called after the temporary file is complete and flushed but before it replaces the
    /// destination. Return false to simulate a crash/failure at the worst moment.
    std::function<bool()> beforeCommit;
};

/// Atomic replace: writes `bytes` to a uniquely named temporary sibling, flushes it to disk, then renames
/// it over `destination`. On any failure the temporary file is removed and an existing destination is left
/// byte-for-byte untouched. Missing parent directories are created. Refuses to replace a directory.
IoResult writeFileAtomic (const std::filesystem::path& destination, std::string_view bytes,
                          const WriteHooks* hooks = nullptr);

/// Reads a whole file; fails (without partial output) if it is missing, not a regular file, or larger than maxBytes.
IoResult readFileLimited (const std::filesystem::path& file, std::size_t maxBytes, std::string& out);

/// Makes a safe single path component from free text: strips path separators, control and reserved characters
/// (<>:"/\|?*), trailing dots/spaces, avoids Windows device names, limits to `maxBytes` bytes on a UTF-8
/// boundary, never returns an empty string ("Untitled"). The extension is NOT added.
std::string sanitiseFileName (std::string_view name, std::size_t maxBytes = 96);

/// Case-insensitive (ASCII) filename comparison, matching the behaviour of default Windows/macOS volumes.
bool fileNamesEqualIgnoreCase (std::string_view a, std::string_view b);
} // namespace snaral::fileio
