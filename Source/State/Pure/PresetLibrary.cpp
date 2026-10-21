#include "PresetLibrary.h"

#include <algorithm>
#include <cstdlib>
#include <system_error>

#include "Json.h"
#include "NumberText.h"

namespace snaral
{
namespace fs = std::filesystem;
using fileio::IoResult;
using fileio::pathFromUtf8;
using fileio::pathToUtf8;

std::string foldCase (std::string_view s)
{
    std::string out (s);
    for (auto& c : out)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char> (c - 'A' + 'a');
    return out;
}

// ---------------------------------------------------------------------------------------------------------
// paths
// ---------------------------------------------------------------------------------------------------------

LibraryPaths LibraryPaths::forRoot (const fs::path& root)
{
    LibraryPaths p;
    p.userPresetDir = root / "Presets";
    p.packDir = root / "Packs";
    p.stateFile = root / "browser_state.json";
    return p;
}

fs::path LibraryPaths::platformUserDataRoot()
{
    const auto env = [] (const char* name) -> std::string
    {
        const char* v = std::getenv (name);
        return v != nullptr ? std::string (v) : std::string();
    };
    fs::path base;
#if defined(_WIN32)
    const auto appData = env ("APPDATA");
    base = appData.empty() ? fs::path (".") : pathFromUtf8 (appData);
#elif defined(__APPLE__)
    const auto home = env ("HOME");
    base = (home.empty() ? fs::path (".") : pathFromUtf8 (home)) / "Library" / "Application Support";
#else
    const auto xdg = env ("XDG_DATA_HOME");
    const auto home = env ("HOME");
    if (! xdg.empty())
        base = pathFromUtf8 (xdg);
    else
        base = (home.empty() ? fs::path (".") : pathFromUtf8 (home)) / ".local" / "share";
#endif
    return base / "Circuit Drift Labs" / "Snaral";
}

// ---------------------------------------------------------------------------------------------------------
// entries
// ---------------------------------------------------------------------------------------------------------

PresetLibrary::PresetLibrary (LibraryPaths paths) : paths_ (std::move (paths)) {}

PresetEntry PresetLibrary::makeEntry (const PresetData& data, PresetOrigin origin, std::string id, std::string path,
                                      std::string packId)
{
    PresetEntry e;
    e.id = std::move (id);
    e.presetId = data.presetId;
    e.name = data.name;
    e.author = data.author;
    e.description = data.description;
    e.tags = data.tags;
    e.origin = origin;
    e.path = std::move (path);
    e.packId = std::move (packId);
    e.facets = deriveFacets (data.params);
    std::string hay = e.name + "\n";
    for (std::size_t i = 0; i < e.tags.size(); ++i)
        hay += (i ? " " : "") + e.tags[i];
    hay += "\n" + e.author + "\n" + e.description;
    e.searchText = foldCase (hay);
    return e;
}

void PresetLibrary::rebuildLookup()
{
    lookup_.clear();
    lookup_.reserve (entries_.size());
    for (std::size_t i = 0; i < entries_.size(); ++i)
        lookup_[entries_[i].id] = i;
    ++revision_;
}

void PresetLibrary::applyFlags (PresetEntry& e) const
{
    e.favourite = favourites_.count (e.id) > 0;
    const auto it = recent_.find (e.id);
    e.lastUsed = it != recent_.end() ? it->second : 0;
}

void PresetLibrary::upsertEntry (PresetEntry entry)
{
    applyFlags (entry);
    const auto it = lookup_.find (entry.id);
    if (it != lookup_.end())
    {
        entries_[it->second] = std::move (entry);
        ++revision_;
        return;
    }
    entries_.push_back (std::move (entry));
    lookup_[entries_.back().id] = entries_.size() - 1;
    ++revision_;
}

void PresetLibrary::removeEntry (std::string_view id)
{
    const auto it = std::find_if (entries_.begin(), entries_.end(), [id] (const PresetEntry& e) { return e.id == id; });
    if (it == entries_.end())
        return;
    entries_.erase (it);
    rebuildLookup();
}

const PresetEntry* PresetLibrary::find (std::string_view id) const
{
    const auto it = lookup_.find (std::string (id));
    return it == lookup_.end() ? nullptr : &entries_[it->second];
}

const PackInfo* PresetLibrary::findPack (std::string_view packId) const
{
    for (const auto& p : packs_)
        if (p.manifest.packId == packId)
            return &p;
    return nullptr;
}

// ---------------------------------------------------------------------------------------------------------
// scanning
// ---------------------------------------------------------------------------------------------------------

namespace
{
bool hasPresetExtension (const fs::path& p)
{
    const auto name = pathToUtf8 (p.filename());
    return name.size() > presetFileExtension.size()
           && fileio::fileNamesEqualIgnoreCase (name.substr (name.size() - presetFileExtension.size()),
                                                presetFileExtension);
}

std::string firstProblem (const PresetParseResult& r)
{
    return r.errors.empty() ? std::string ("invalid preset") : describe (r.errors.front());
}
} // namespace

ScanReport PresetLibrary::rescan (const FactoryPresetSet& factory)
{
    ScanReport report;
    entries_.clear();
    packs_.clear();
    factory_ = factory;

    loadBrowserState (report);

    for (const auto& p : factory_.problems)
        report.skipped.push_back ({ p.path, p.message });
    std::set<std::string> factoryIds;
    for (const auto& f : factory_.presets)
    {
        auto id = factoryEntryId (f.data);
        if (! factoryIds.insert (id).second)
        {
            report.skipped.push_back ({ f.fileName, "duplicate factory preset id " + id });
            continue;
        }
        auto e = makeEntry (f.data, PresetOrigin::Factory, id, {}, {});
        applyFlags (e);
        entries_.push_back (std::move (e));
        ++report.factory;
    }

    // user folder (flat)
    std::error_code ec;
    if (fs::is_directory (paths_.userPresetDir, ec))
    {
        std::vector<fs::path> files;
        for (fs::directory_iterator it (paths_.userPresetDir, fs::directory_options::skip_permission_denied, ec), end;
             ! ec && it != end; it.increment (ec))
        {
            std::error_code e2;
            if (it->is_regular_file (e2) && hasPresetExtension (it->path())
                && pathToUtf8 (it->path().filename()).front() != '.')
                files.push_back (it->path());
        }
        if (ec)
            report.notes.push_back ({ pathToUtf8 (paths_.userPresetDir), "cannot list folder: " + ec.message() });
        std::sort (files.begin(), files.end());
        for (const auto& file : files)
        {
            const auto fileName = pathToUtf8 (file.filename());
            std::string bytes;
            auto io = fileio::readFileLimited (file, maxPresetFileBytes, bytes);
            if (! io.ok)
            {
                report.skipped.push_back ({ fileName, io.error });
                continue;
            }
            const auto r = parsePreset (bytes);
            if (! r.ok)
            {
                report.skipped.push_back ({ fileName, firstProblem (r) });
                continue;
            }
            auto e = makeEntry (r.data, PresetOrigin::User, "user:" + fileName, pathToUtf8 (file), {});
            applyFlags (e);
            entries_.push_back (std::move (e));
            ++report.user;
        }
    }

    // installed packs
    if (fs::is_directory (paths_.packDir, ec))
    {
        std::vector<fs::path> dirs;
        for (fs::directory_iterator it (paths_.packDir, fs::directory_options::skip_permission_denied, ec), end;
             ! ec && it != end; it.increment (ec))
        {
            std::error_code e2;
            if (it->is_directory (e2) && pathToUtf8 (it->path().filename()).front() != '.')
                dirs.push_back (it->path());
        }
        std::sort (dirs.begin(), dirs.end());
        for (const auto& dir : dirs)
        {
            const auto dirName = pathToUtf8 (dir.filename());
            std::string manifestBytes;
            auto io = fileio::readFileLimited (dir / std::string (packManifestFileName), maxPackManifestBytes,
                                               manifestBytes);
            if (! io.ok)
            {
                report.skipped.push_back ({ dirName, "pack has no readable manifest: " + io.error });
                continue;
            }
            const auto mr = parsePackManifest (manifestBytes);
            if (! mr.ok)
            {
                report.skipped.push_back ({ dirName + "/" + std::string (packManifestFileName),
                                            describe (mr.errors.front()) });
                continue;
            }
            PackInfo info;
            info.manifest = mr.manifest;
            info.directory = pathToUtf8 (dir);
            PresetParseOptions po;
            po.requireComplete = true;
            for (const auto& fileName : mr.manifest.files)
            {
                std::string bytes;
                io = fileio::readFileLimited (dir / pathFromUtf8 (fileName), maxPresetFileBytes, bytes);
                if (! io.ok)
                {
                    report.skipped.push_back ({ dirName + "/" + fileName, io.error });
                    continue;
                }
                const auto r = parsePreset (bytes, po);
                if (! r.ok)
                {
                    report.skipped.push_back ({ dirName + "/" + fileName, firstProblem (r) });
                    continue;
                }
                auto e = makeEntry (r.data, PresetOrigin::Pack, "pack:" + mr.manifest.packId + ":" + fileName,
                                    pathToUtf8 (dir / pathFromUtf8 (fileName)), mr.manifest.packId);
                applyFlags (e);
                entries_.push_back (std::move (e));
                ++info.installedCount;
                ++report.pack;
            }
            packs_.push_back (std::move (info));
        }
    }

    rebuildLookup();
    return report;
}

// ---------------------------------------------------------------------------------------------------------
// load
// ---------------------------------------------------------------------------------------------------------

PresetParseResult PresetLibrary::loadPreset (std::string_view id) const
{
    PresetParseResult r;
    const auto* e = find (id);
    if (e == nullptr)
    {
        r.errors.push_back ({ std::string (id), "preset is not in the library" });
        return r;
    }
    if (e->origin == PresetOrigin::Factory)
    {
        for (const auto& f : factory_.presets)
            if (factoryEntryId (f.data) == e->id)
            {
                r.ok = true;
                r.data = f.data;
                r.sourceSchemaVersion = presetSchemaVersion;
                r.warnings = f.warnings;
                return r;
            }
        r.errors.push_back ({ e->id, "factory preset data is missing" });
        return r;
    }
    std::string bytes;
    const auto io = fileio::readFileLimited (pathFromUtf8 (e->path), maxPresetFileBytes, bytes);
    if (! io.ok)
    {
        r.errors.push_back ({ e->name, io.error });
        return r;
    }
    PresetParseOptions po;
    po.requireComplete = e->origin == PresetOrigin::Pack;
    return parsePreset (bytes, po);
}

// ---------------------------------------------------------------------------------------------------------
// browser state (favourites, recents)
// ---------------------------------------------------------------------------------------------------------

void PresetLibrary::loadBrowserState (ScanReport& report)
{
    favourites_.clear();
    recent_.clear();
    std::error_code ec;
    if (paths_.stateFile.empty() || ! fs::exists (paths_.stateFile, ec))
        return;

    auto quarantine = [&] (const std::string& reason)
    {
        report.notes.push_back ({ pathToUtf8 (paths_.stateFile.filename()),
                                  reason + "; favourites and history were reset (the old file was kept as .corrupt)" });
        std::error_code e2;
        auto bad = paths_.stateFile;
        bad += ".corrupt";
        fs::rename (paths_.stateFile, bad, e2);
    };

    std::string bytes;
    const auto io = fileio::readFileLimited (paths_.stateFile, 4 * 1024 * 1024, bytes);
    if (! io.ok)
    {
        report.notes.push_back ({ pathToUtf8 (paths_.stateFile.filename()), io.error });
        return;
    }
    json::Value root;
    json::ParseError perr;
    json::ParseOptions po;
    po.maxBytes = 4 * 1024 * 1024;
    po.maxDepth = 6;
    if (! json::parse (bytes, root, perr, po) || ! root.isObject())
        return quarantine ("browser state is not valid JSON");
    const auto* format = root.find ("format");
    const auto* ver = root.find ("schema_version");
    long long v = 0;
    if (format == nullptr || ! format->isString() || format->text != browserStateFormatId || ver == nullptr
        || ! ver->isNumber() || ! text::parseInt (ver->text, v))
        return quarantine ("browser state has an unknown format");
    if (v > browserStateSchemaVersion)
    {
        // Written by a newer build: leave the file alone and run with empty state for this session.
        report.notes.push_back ({ pathToUtf8 (paths_.stateFile.filename()),
                                  "browser state was written by a newer version; favourites unavailable" });
        return;
    }
    if (const auto* fav = root.find ("favourites"); fav != nullptr && fav->isArray())
        for (const auto& item : fav->items)
            if (item.isString() && favourites_.size() < maxFavourites && json::isValidUtf8 (item.text))
                favourites_.insert (item.text);
    if (const auto* rec = root.find ("recent"); rec != nullptr && rec->isArray())
        for (const auto& item : rec->items)
        {
            const auto* id = item.find ("id");
            const auto* used = item.find ("used");
            long long t = 0;
            if (id != nullptr && id->isString() && used != nullptr && used->isNumber() && text::parseInt (used->text, t)
                && recent_.size() < maxRecentEntries)
                recent_[id->text] = t;
        }
}

IoResult PresetLibrary::saveBrowserState (const LibraryHooks& hooks) const
{
    if (paths_.stateFile.empty())
        return IoResult::failure ("no browser state file configured");
    std::string s = "{\n  \"favourites\": [";
    {
        bool first = true;
        for (const auto& id : favourites_)
        {
            s += first ? "\n    " : ",\n    ";
            s += json::quote (id);
            first = false;
        }
        s += first ? "" : "\n  ";
    }
    s += "],\n  \"format\": " + json::quote (browserStateFormatId) + ",\n  \"recent\": [";
    {
        std::vector<std::pair<std::string, long long>> items (recent_.begin(), recent_.end());
        std::sort (items.begin(), items.end(), [] (const auto& a, const auto& b)
                   { return a.second != b.second ? a.second > b.second : a.first < b.first; });
        if (items.size() > maxRecentEntries)
            items.resize (maxRecentEntries);
        bool first = true;
        for (const auto& [id, used] : items)
        {
            s += first ? "\n    " : ",\n    ";
            s += "{ \"id\": " + json::quote (id) + ", \"used\": " + std::to_string (used) + " }";
            first = false;
        }
        s += first ? "" : "\n  ";
    }
    s += "],\n  \"schema_version\": " + std::to_string (browserStateSchemaVersion) + "\n}\n";
    return fileio::writeFileAtomic (paths_.stateFile, s, hooks.write);
}

bool PresetLibrary::isFavourite (std::string_view id) const
{
    return favourites_.count (std::string (id)) > 0;
}

IoResult PresetLibrary::setFavourite (std::string_view id, bool favourite)
{
    const std::string key (id);
    if (favourite)
    {
        if (favourites_.size() >= maxFavourites && favourites_.count (key) == 0)
            return IoResult::failure ("too many favourites");
        favourites_.insert (key);
    }
    else
    {
        favourites_.erase (key);
    }
    if (const auto it = lookup_.find (key); it != lookup_.end())
        entries_[it->second].favourite = favourite;
    ++revision_;
    return saveBrowserState();
}

IoResult PresetLibrary::markUsed (std::string_view id, long long useTime)
{
    const std::string key (id);
    recent_[key] = useTime;
    if (recent_.size() > maxRecentEntries * 2)
    {
        // drop the oldest half-overflow to keep the file bounded
        std::vector<std::pair<long long, std::string>> v;
        for (const auto& [k, t] : recent_)
            v.emplace_back (t, k);
        std::sort (v.begin(), v.end());
        for (std::size_t i = 0; i + maxRecentEntries < v.size(); ++i)
            recent_.erase (v[i].second);
    }
    if (const auto it = lookup_.find (key); it != lookup_.end())
        entries_[it->second].lastUsed = useTime;
    ++revision_;
    return saveBrowserState();
}

void PresetLibrary::renameStateKeys (const std::string& from, const std::string& to)
{
    if (from == to)
        return;
    if (favourites_.erase (from) > 0)
        favourites_.insert (to);
    if (const auto it = recent_.find (from); it != recent_.end())
    {
        recent_[to] = it->second;
        recent_.erase (from);
    }
}

// ---------------------------------------------------------------------------------------------------------
// user file operations
// ---------------------------------------------------------------------------------------------------------

std::string PresetLibrary::findExistingUserFile (const std::string& fileName) const
{
    std::error_code ec;
    if (! fs::is_directory (paths_.userPresetDir, ec))
        return {};
    for (fs::directory_iterator it (paths_.userPresetDir, fs::directory_options::skip_permission_denied, ec), end;
         ! ec && it != end; it.increment (ec))
    {
        const auto name = pathToUtf8 (it->path().filename());
        if (fileio::fileNamesEqualIgnoreCase (name, fileName))
            return name;
    }
    return {};
}

SaveResult PresetLibrary::writeUserPreset (const PresetData& preset, bool overwrite, const LibraryHooks& hooks,
                                           const std::string& oldId)
{
    SaveResult result;
    std::string bytes;
    std::vector<Issue> issues;
    if (! writePreset (preset, bytes, &issues))
    {
        result.status = SaveStatus::Invalid;
        result.issues = std::move (issues);
        result.message = describe (result.issues.front());
        return result;
    }

    const std::string wanted = fileio::sanitiseFileName (preset.name) + std::string (presetFileExtension);
    const std::string existing = findExistingUserFile (wanted);
    const std::string fileName = existing.empty() ? wanted : existing;
    const fs::path target = paths_.userPresetDir / pathFromUtf8 (fileName);
    result.path = pathToUtf8 (target);

    const bool sameAsOld = ! oldId.empty() && ("user:" + fileName) == oldId;
    if (! existing.empty() && ! overwrite && ! sameAsOld)
    {
        result.status = SaveStatus::OverwriteRequired;
        result.message = "A preset named \"" + existing + "\" already exists. Overwrite it?";
        return result;
    }

    const auto io = fileio::writeFileAtomic (target, bytes, hooks.write);
    if (! io.ok)
    {
        result.status = SaveStatus::IoError;
        result.message = io.error;
        return result;
    }

    result.status = SaveStatus::Saved;
    result.id = "user:" + fileName;
    upsertEntry (makeEntry (preset, PresetOrigin::User, result.id, result.path, {}));
    return result;
}

SaveResult PresetLibrary::saveUserPreset (const PresetData& preset, bool overwrite, const LibraryHooks& hooks)
{
    return writeUserPreset (preset, overwrite, hooks, {});
}

SaveResult PresetLibrary::importPresetFile (const fs::path& source, bool overwrite, const LibraryHooks& hooks)
{
    SaveResult result;
    const auto display = pathToUtf8 (source.filename());
    std::string bytes;
    const auto io = fileio::readFileLimited (source, maxPresetFileBytes, bytes);
    if (! io.ok)
    {
        result.status = SaveStatus::IoError;
        result.message = io.error;
        return result;
    }
    auto parsed = parsePreset (bytes);
    if (! parsed.ok)
    {
        result.status = SaveStatus::Invalid;
        result.issues = parsed.errors;
        result.message = "Cannot import " + display + ": " + describe (parsed.errors.front());
        return result;
    }
    result = writeUserPreset (parsed.data, overwrite, hooks, {});
    result.warnings = std::move (parsed.warnings);
    return result;
}

SaveResult PresetLibrary::exportPreset (std::string_view id, const fs::path& destination, bool overwrite,
                                        const LibraryHooks& hooks)
{
    SaveResult result;
    const auto loaded = loadPreset (id);
    if (! loaded.ok)
    {
        result.status = SaveStatus::Invalid;
        result.issues = loaded.errors;
        result.message = loaded.errors.empty() ? "cannot read preset" : describe (loaded.errors.front());
        return result;
    }
    std::string bytes;
    std::vector<Issue> issues;
    if (! writePreset (loaded.data, bytes, &issues))
    {
        result.status = SaveStatus::Invalid;
        result.issues = std::move (issues);
        result.message = describe (result.issues.front());
        return result;
    }
    fs::path dest = destination;
    if (! hasPresetExtension (dest))
        dest += std::string (presetFileExtension);
    result.path = pathToUtf8 (dest);
    std::error_code ec;
    if (fs::exists (dest, ec) && ! overwrite)
    {
        result.status = SaveStatus::OverwriteRequired;
        result.message = "\"" + pathToUtf8 (dest.filename()) + "\" already exists. Overwrite it?";
        return result;
    }
    const auto io = fileio::writeFileAtomic (dest, bytes, hooks.write);
    result.status = io.ok ? SaveStatus::Saved : SaveStatus::IoError;
    result.message = io.error;
    return result;
}

IoResult PresetLibrary::deleteUserPreset (std::string_view id)
{
    const auto* e = find (id);
    if (e == nullptr)
        return IoResult::failure ("preset is not in the library");
    if (e->origin != PresetOrigin::User)
        return IoResult::failure ("only user presets can be deleted");
    const std::string key (id);
    const auto path = pathFromUtf8 (e->path);
    std::error_code ec;
    fs::remove (path, ec);
    if (ec)
        return IoResult::failure ("cannot delete " + pathToUtf8 (path.filename()) + ": " + ec.message());
    removeEntry (key);
    const bool hadState = favourites_.erase (key) > 0 || recent_.erase (key) > 0;
    if (hadState)
        (void) saveBrowserState();
    return IoResult::success();
}

SaveResult PresetLibrary::renameUserPreset (std::string_view id, const std::string& newName, bool overwrite,
                                            const LibraryHooks& hooks)
{
    SaveResult result;
    const auto* e = find (id);
    if (e == nullptr || e->origin != PresetOrigin::User)
    {
        result.status = SaveStatus::Invalid;
        result.message = "only user presets can be renamed";
        return result;
    }
    const std::string oldId (id);
    const auto oldPath = pathFromUtf8 (e->path);
    auto loaded = loadPreset (id);
    if (! loaded.ok)
    {
        result.status = SaveStatus::Invalid;
        result.issues = loaded.errors;
        result.message = describe (loaded.errors.front());
        return result;
    }
    loaded.data.name = newName;
    result = writeUserPreset (loaded.data, overwrite, hooks, oldId);
    if (! result.ok())
        return result;
    if (result.id != oldId)
    {
        std::error_code ec;
        if (! fs::equivalent (oldPath, pathFromUtf8 (result.path), ec))
            fs::remove (oldPath, ec);
        removeEntry (oldId);
        renameStateKeys (oldId, result.id);
        if (const auto it = lookup_.find (result.id); it != lookup_.end())
            applyFlags (entries_[it->second]);
        (void) saveBrowserState();
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------
// packs
// ---------------------------------------------------------------------------------------------------------

IoResult PresetLibrary::removePack (std::string_view packId)
{
    const auto* info = findPack (packId);
    if (info == nullptr)
        return IoResult::failure ("pack is not installed");
    const auto dir = pathFromUtf8 (info->directory);
    std::error_code ec;
    auto trash = dir.parent_path() / pathFromUtf8 ("." + std::string (packId) + ".removing");
    fs::remove_all (trash, ec);
    fs::rename (dir, trash, ec);
    if (ec)
        return IoResult::failure ("cannot remove pack folder: " + ec.message());
    fs::remove_all (trash, ec);

    const std::string pid (packId);
    entries_.erase (std::remove_if (entries_.begin(), entries_.end(),
                                    [&] (const PresetEntry& x) { return x.origin == PresetOrigin::Pack && x.packId == pid; }),
                    entries_.end());
    packs_.erase (std::remove_if (packs_.begin(), packs_.end(),
                                  [&] (const PackInfo& p) { return p.manifest.packId == pid; }),
                  packs_.end());
    rebuildLookup();
    return IoResult::success();
}

PackImportResult PresetLibrary::importPack (const fs::path& sourceDir, DuplicatePolicy policy, const LibraryHooks& hooks)
{
    PackImportResult result;
    auto fail = [&result] (std::string message)
    {
        result.ok = false;
        result.message = std::move (message);
        return result;
    };

    std::string manifestBytes;
    auto io = fileio::readFileLimited (sourceDir / std::string (packManifestFileName), maxPackManifestBytes,
                                       manifestBytes);
    if (! io.ok)
        return fail ("pack has no readable manifest: " + io.error);
    const auto mr = parsePackManifest (manifestBytes);
    if (! mr.ok)
        return fail ("invalid pack manifest: " + describe (mr.errors.front()));
    const auto& manifest = mr.manifest;
    result.packId = manifest.packId;

    // 1) validate EVERY file before anything is added
    struct Staged
    {
        std::string file;
        PresetData data;
        std::string bytes;
        bool install = true;
    };
    std::vector<Staged> staged;
    std::set<std::string> idsInPack;
    bool anyInvalid = false;
    PresetParseOptions po;
    po.requireComplete = true;
    for (const auto& file : manifest.files)
    {
        PackImportItem item;
        item.file = file;
        std::string bytes;
        io = fileio::readFileLimited (sourceDir / pathFromUtf8 (file), maxPresetFileBytes, bytes);
        if (! io.ok)
        {
            item.status = "invalid";
            item.message = io.error;
            anyInvalid = true;
            result.items.push_back (std::move (item));
            continue;
        }
        auto r = parsePreset (bytes, po);
        if (! r.ok)
        {
            item.status = "invalid";
            item.message = describe (r.errors.front());
            anyInvalid = true;
            result.items.push_back (std::move (item));
            continue;
        }
        item.presetId = r.data.presetId;
        if (r.data.presetId.empty())
        {
            item.status = "invalid";
            item.message = "pack presets must carry a preset_id";
            anyInvalid = true;
        }
        else if (! idsInPack.insert (r.data.presetId).second)
        {
            item.status = "invalid";
            item.message = "duplicate preset_id " + r.data.presetId + " inside the pack";
            anyInvalid = true;
        }
        else
        {
            item.status = "ready";
        }
        result.items.push_back (item);
        if (item.status == "ready")
        {
            Staged s;
            s.file = file;
            s.data = std::move (r.data);
            std::vector<Issue> issues;
            writePreset (s.data, s.bytes, &issues); // canonical copy; cannot fail for a parsed preset
            staged.push_back (std::move (s));
        }
    }
    if (anyInvalid)
    {
        for (auto& i : result.items)
            if (i.status == "ready")
                i.status = "cancelled";
        return fail ("pack not imported: one or more files are invalid");
    }

    // 2) duplicates
    const bool alreadyInstalled = findPack (manifest.packId) != nullptr;
    if (alreadyInstalled && policy == DuplicatePolicy::Skip)
    {
        for (auto& i : result.items)
        {
            i.status = "skipped";
            i.message = "pack is already installed";
        }
        result.ok = true;
        result.message = "pack already installed; nothing changed";
        return result;
    }
    if (alreadyInstalled && policy == DuplicatePolicy::Cancel)
    {
        for (auto& i : result.items)
            i.status = "cancelled";
        result.cancelled = true;
        result.message = "pack \"" + manifest.packId + "\" is already installed";
        return result;
    }
    bool conflicts = false;
    for (std::size_t k = 0; k < staged.size(); ++k)
    {
        for (const auto& e : entries_)
        {
            if (e.presetId.empty() || e.presetId != staged[k].data.presetId)
                continue;
            if (e.origin == PresetOrigin::Pack && e.packId == manifest.packId)
                continue; // replaced together with its own pack
            conflicts = true;
            staged[k].install = false;
            result.items[k].status = "skipped";
            result.items[k].message = "preset_id already used by " + e.name
                                      + (e.origin == PresetOrigin::Factory ? " (factory)"
                                         : e.origin == PresetOrigin::User  ? " (user preset)"
                                                                           : " (another pack)");
            break;
        }
    }
    if (conflicts && policy == DuplicatePolicy::Cancel)
    {
        for (auto& i : result.items)
            i.status = "cancelled";
        result.cancelled = true;
        result.message = "duplicate preset ids found; nothing was imported";
        return result;
    }

    std::vector<std::size_t> toInstall;
    for (std::size_t k = 0; k < staged.size(); ++k)
        if (staged[k].install)
            toInstall.push_back (k);
    if (toInstall.empty())
    {
        result.ok = true;
        result.message = "nothing to import";
        return result;
    }

    // 3) stage in a hidden sibling folder, then swap in with renames
    PackManifest installed = manifest;
    installed.files.clear();
    for (const auto k : toInstall)
        installed.files.push_back (staged[k].file);
    installed.presetCount = static_cast<int> (installed.files.size());
    std::string installedManifest;
    std::vector<Issue> issues;
    if (! writePackManifest (installed, installedManifest, &issues))
        return fail ("cannot write manifest: " + describe (issues.front()));

    std::error_code ec;
    const fs::path staging = paths_.packDir / pathFromUtf8 ("." + manifest.packId + ".staging");
    const fs::path backup = paths_.packDir / pathFromUtf8 ("." + manifest.packId + ".old");
    const fs::path target = paths_.packDir / pathFromUtf8 (manifest.packId);
    fs::remove_all (staging, ec);
    fs::create_directories (staging, ec);
    if (ec)
        return fail ("cannot create staging folder: " + ec.message());

    auto cleanupStaging = [&] { std::error_code e2; fs::remove_all (staging, e2); };
    for (const auto k : toInstall)
    {
        io = fileio::writeFileAtomic (staging / pathFromUtf8 (staged[k].file), staged[k].bytes, hooks.write);
        if (! io.ok)
        {
            cleanupStaging();
            return fail ("pack not imported: " + io.error);
        }
    }
    io = fileio::writeFileAtomic (staging / std::string (packManifestFileName), installedManifest, hooks.write);
    if (! io.ok)
    {
        cleanupStaging();
        return fail ("pack not imported: " + io.error);
    }

    const bool hadTarget = fs::exists (target, ec);
    if (hadTarget)
    {
        fs::remove_all (backup, ec);
        fs::rename (target, backup, ec);
        if (ec)
        {
            cleanupStaging();
            return fail ("cannot replace the installed pack: " + ec.message());
        }
    }
    fs::rename (staging, target, ec);
    if (ec)
    {
        const auto message = ec.message();
        if (hadTarget)
            fs::rename (backup, target, ec);
        cleanupStaging();
        return fail ("pack not imported: " + message);
    }
    if (hadTarget)
        fs::remove_all (backup, ec);

    // 4) update the index
    entries_.erase (std::remove_if (entries_.begin(), entries_.end(),
                                    [&] (const PresetEntry& x)
                                    { return x.origin == PresetOrigin::Pack && x.packId == manifest.packId; }),
                    entries_.end());
    packs_.erase (std::remove_if (packs_.begin(), packs_.end(),
                                  [&] (const PackInfo& p) { return p.manifest.packId == manifest.packId; }),
                  packs_.end());
    PackInfo info;
    info.manifest = installed;
    info.directory = pathToUtf8 (target);
    for (const auto k : toInstall)
    {
        auto e = makeEntry (staged[k].data, PresetOrigin::Pack, "pack:" + manifest.packId + ":" + staged[k].file,
                            pathToUtf8 (target / pathFromUtf8 (staged[k].file)), manifest.packId);
        applyFlags (e);
        entries_.push_back (std::move (e));
        ++info.installedCount;
        result.items[k].status = "imported";
    }
    packs_.push_back (std::move (info));
    rebuildLookup();

    result.ok = true;
    result.changed = true;
    result.message = "imported " + std::to_string (toInstall.size()) + " preset(s)";
    return result;
}
} // namespace snaral
