#pragma once

// Preset library: index of factory + user + pack presets (metadata only), favourites / recently-used
// persistence, and safe user-file operations. JUCE-free. docs/specs/expansion-preset-browser.md,
// expansion-preset-library.md, preset-file-contract.md.
//
// Threading: not internally synchronised. Intended use: construct and call rescan()/file operations on the
// message thread or one background worker, and never from the audio thread. UI code reads entries() on the
// message thread. Disk I/O happens only in rescan(), loadPreset(), the file operations and the favourite /
// recent setters (which persist browser_state.json atomically).

#include <cstdint>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "AtomicFile.h"
#include "FactoryPresets.h"
#include "PresetFacets.h"
#include "PresetPack.h"

namespace snaral
{
enum class PresetOrigin { Factory, User, Pack };

inline constexpr std::string_view browserStateFormatId = "snaral-browser-state";
inline constexpr int browserStateSchemaVersion = 1;
inline constexpr std::size_t maxRecentEntries = 200;
inline constexpr std::size_t maxFavourites = 10000;

struct PresetEntry
{
    std::string id;          // unique: "factory:<preset_id>", "user:<file name>", "pack:<pack id>:<file name>"
    std::string presetId;    // the file's own preset_id (may be empty)
    std::string name, author, description;
    std::vector<std::string> tags;
    PresetOrigin origin = PresetOrigin::User;
    std::string path;        // UTF-8 path for user/pack presets; empty for factory
    std::string packId;      // pack presets only
    PresetFacets facets;
    bool favourite = false;
    long long lastUsed = 0;  // 0 = never used; larger = more recent (caller-defined clock, e.g. epoch seconds)
    std::string searchText;  // lower-cased "name\ntags\nauthor\ndescription" (ASCII case folding)
};

struct PackInfo
{
    PackManifest manifest;
    std::string directory;  // UTF-8 install folder
    int installedCount = 0; // presets actually indexed
};

struct ScanIssue
{
    std::string path;    // file name or folder (never empty for file problems)
    std::string message; // actionable reason
};

struct ScanReport
{
    int factory = 0, user = 0, pack = 0;
    std::vector<ScanIssue> skipped; // unreadable / invalid / oversized files that were left out of the index
    std::vector<ScanIssue> notes;   // other problems (e.g. unreadable browser_state.json)
};

struct LibraryPaths
{
    std::filesystem::path userPresetDir; // flat folder of .snaralpreset files
    std::filesystem::path packDir;       // one sub-folder per installed pack
    std::filesystem::path stateFile;     // browser_state.json (favourites, recently used)

    /// root/Presets, root/Packs, root/browser_state.json
    static LibraryPaths forRoot (const std::filesystem::path& root);
    /// <per-user app data>/Circuit Drift Labs/Snaral (%APPDATA%, ~/Library/Application Support, $XDG_DATA_HOME or ~/.local/share)
    static std::filesystem::path platformUserDataRoot();
    static LibraryPaths platformDefault() { return forRoot (platformUserDataRoot()); }
};

enum class SaveStatus { Saved, OverwriteRequired, Invalid, IoError };

struct SaveResult
{
    SaveStatus status = SaveStatus::IoError;
    std::string path;     // file written, or the existing file for OverwriteRequired
    std::string id;       // library id of the saved preset (Saved only)
    std::string message;  // concise reason for the UI
    std::vector<Issue> issues;   // validation errors (Invalid)
    std::vector<Issue> warnings; // non-fatal findings from an import

    bool ok() const noexcept { return status == SaveStatus::Saved; }
};

enum class DuplicatePolicy { Cancel, Skip, Replace };

struct PackImportItem
{
    std::string file;
    std::string presetId;
    std::string status;  // "imported", "skipped", "invalid", "cancelled"
    std::string message;
};

struct PackImportResult
{
    bool ok = false;         // true: nothing failed (skips are not failures)
    bool cancelled = false;  // duplicates found under DuplicatePolicy::Cancel; nothing was changed
    bool changed = false;    // the installed pack set changed
    std::string packId;
    std::string message;
    std::vector<PackImportItem> items;
};

/// Test seam for failure injection during pack/file operations.
struct LibraryHooks
{
    const fileio::WriteHooks* write = nullptr;
};

class PresetLibrary
{
public:
    explicit PresetLibrary (LibraryPaths paths);

    const LibraryPaths& paths() const noexcept { return paths_; }

    // ---- index ----
    /// Rebuilds the whole index: factory set, user folder, installed packs; then loads favourites / recents.
    /// Invalid files are skipped and listed in the report (the file name is always included).
    ScanReport rescan (const FactoryPresetSet& factory);
    ScanReport rescan() { return rescan (factoryPresets()); }

    /// Adds or replaces an entry directly (synthetic libraries in tests, or after an operation). Bumps revision().
    void upsertEntry (PresetEntry entry);
    static PresetEntry makeEntry (const PresetData& data, PresetOrigin origin, std::string id, std::string path,
                                  std::string packId);

    const std::vector<PresetEntry>& entries() const noexcept { return entries_; }
    const PresetEntry* find (std::string_view id) const;
    const std::vector<PackInfo>& packs() const noexcept { return packs_; }
    const PackInfo* findPack (std::string_view packId) const;
    /// Incremented on every change to entries/flags; lets models cache their result lists.
    std::uint64_t revision() const noexcept { return revision_; }

    /// Full preset (validated again from disk for user/pack files; factory comes from the embedded set).
    PresetParseResult loadPreset (std::string_view id) const;

    // ---- favourites and recently used (persist to the state file) ----
    fileio::IoResult setFavourite (std::string_view id, bool favourite);
    bool isFavourite (std::string_view id) const;
    fileio::IoResult markUsed (std::string_view id, long long useTime);
    /// Writes browser_state.json atomically. Failure keeps the in-memory state and reports the reason.
    fileio::IoResult saveBrowserState(const LibraryHooks& hooks = {}) const;

    // ---- user preset files ----
    /// Saves under <userPresetDir>/<sanitised name>.snaralpreset. If a file with that name exists (ASCII
    /// case-insensitive) and overwrite is false, returns OverwriteRequired and writes nothing. Factory and
    /// pack presets are never touched.
    SaveResult saveUserPreset (const PresetData& preset, bool overwrite = false, const LibraryHooks& hooks = {});
    /// Validates the whole candidate file, then stores a canonical copy in the user folder.
    SaveResult importPresetFile (const std::filesystem::path& source, bool overwrite = false,
                                 const LibraryHooks& hooks = {});
    /// Writes a canonical copy of any library preset to `destination` (".snaralpreset" is appended if missing).
    SaveResult exportPreset (std::string_view id, const std::filesystem::path& destination, bool overwrite = false,
                             const LibraryHooks& hooks = {});
    fileio::IoResult deleteUserPreset (std::string_view id);
    SaveResult renameUserPreset (std::string_view id, const std::string& newName, bool overwrite = false,
                                 const LibraryHooks& hooks = {});

    // ---- packs ----
    /// Validates every file of the pack at `sourceDir` (manifest.json + presets) before adding anything, then
    /// installs it atomically under packDir/<pack id>. See docs/specs/expansion-preset-library.md for the
    /// duplicate rules. Removing a pack never touches user presets.
    PackImportResult importPack (const std::filesystem::path& sourceDir, DuplicatePolicy policy,
                                 const LibraryHooks& hooks = {});
    fileio::IoResult removePack (std::string_view packId);

private:
    void rebuildLookup();
    void applyFlags (PresetEntry& e) const;
    void removeEntry (std::string_view id);
    std::string findExistingUserFile (const std::string& fileName) const;
    void loadBrowserState (ScanReport& report);
    SaveResult writeUserPreset (const PresetData& preset, bool overwrite, const LibraryHooks& hooks,
                                const std::string& oldId);
    void renameStateKeys (const std::string& from, const std::string& to);

    LibraryPaths paths_;
    std::vector<PresetEntry> entries_;
    std::vector<PackInfo> packs_;
    std::unordered_map<std::string, std::size_t> lookup_;
    FactoryPresetSet factory_;
    std::set<std::string> favourites_;
    std::map<std::string, long long> recent_;
    std::uint64_t revision_ = 0;
};

/// ASCII case folding used for search text and tag comparison.
std::string foldCase (std::string_view s);
} // namespace snaral
