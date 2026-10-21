#include "TestSupport.h"

#include <algorithm>
#include <fstream>
#include <iostream>

#include "State/Pure/AtomicFile.h"
#include "State/Pure/PresetFormat.h"
#include "State/Pure/PresetLibrary.h"

using namespace snaral;
using namespace snaral::testing;
namespace fs = std::filesystem;

namespace
{
std::string slurp (const fs::path& p)
{
    std::string s;
    const auto io = fileio::readFileLimited (p, 64u << 20, s);
    CHECK_MSG (io.ok, io.error);
    return s;
}

void spit (const fs::path& p, const std::string& bytes)
{
    fs::create_directories (p.parent_path());
    std::ofstream f (p, std::ios::binary);
    f << bytes;
}

int fileCount (const fs::path& dir)
{
    int n = 0;
    if (fs::exists (dir))
        for ([[maybe_unused]] const auto& e : fs::directory_iterator (dir))
            ++n;
    return n;
}

PresetData makePreset (const std::string& name, std::uint64_t seed, const std::string& id = {})
{
    PresetData d;
    d.name = name;
    d.author = "Tester";
    d.description = "Test preset " + name;
    d.presetId = id;
    d.tags = { "test", "sub" };
    d.params = randomSnapshot (seed);
    return d;
}

std::string writeOrDie (const PresetData& d)
{
    std::string out;
    CHECK (writePreset (d, out));
    return out;
}

void testAtomicWrite()
{
    TempDir tmp;
    const auto file = tmp.path() / "out" / "a.snaralpreset";

    CHECK (fileio::writeFileAtomic (file, "first").ok);           // creates the parent folder
    CHECK (slurp (file) == "first");
    CHECK (fileio::writeFileAtomic (file, "second").ok);
    CHECK (slurp (file) == "second");
    CHECK (fileCount (file.parent_path()) == 1);                  // no temp files left behind
    CHECK (fileio::writeFileAtomic (file, "").ok && slurp (file).empty());

    // failure after the temp file is complete but before the commit: the old file survives untouched
    CHECK (fileio::writeFileAtomic (file, "precious").ok);
    fileio::WriteHooks failing;
    failing.beforeCommit = [] { return false; };
    const auto r = fileio::writeFileAtomic (file, "NEW CONTENT THAT MUST NOT APPEAR", &failing);
    CHECK (! r.ok && r.error.find ("a.snaralpreset") != std::string::npos);
    CHECK (slurp (file) == "precious");
    CHECK (fileCount (file.parent_path()) == 1);

    // failure when the destination did not exist: no partial final file either
    const auto fresh = tmp.path() / "out" / "b.snaralpreset";
    CHECK (! fileio::writeFileAtomic (fresh, "x", &failing).ok);
    CHECK (! fs::exists (fresh) && fileCount (file.parent_path()) == 1);

    // a real rename failure (destination is a directory) leaves everything in place
    const auto dirDest = tmp.path() / "out" / "dir.snaralpreset";
    fs::create_directories (dirDest / "inner");
    CHECK (! fileio::writeFileAtomic (dirDest, "x").ok);
    CHECK (fs::is_directory (dirDest) && fs::exists (dirDest / "inner"));
    CHECK (fileCount (file.parent_path()) == 2);

    // parent path blocked by a regular file
    CHECK (! fileio::writeFileAtomic (file / "child.snaralpreset", "x").ok);
    CHECK (slurp (file) == "precious");

    // reading
    std::string bytes;
    CHECK (fileio::readFileLimited (file, 100, bytes).ok && bytes == "precious");
    CHECK (! fileio::readFileLimited (file, 3, bytes).ok && bytes.empty());
    CHECK (! fileio::readFileLimited (tmp.path() / "missing", 100, bytes).ok);
    CHECK (! fileio::readFileLimited (tmp.path(), 100, bytes).ok);
}

void testFileNames()
{
    using fileio::sanitiseFileName;
    CHECK (sanitiseFileName ("My Bass") == "My Bass");
    CHECK (sanitiseFileName ("a:b*c?d\"e<f>g|h") == "a_b_c_d_e_f_g_h");
    const auto traversal = sanitiseFileName ("../../etc/passwd");
    CHECK (traversal.find ('/') == std::string::npos && traversal.find ('\\') == std::string::npos && traversal[0] != '.');
    CHECK (sanitiseFileName ("..\\..\\windows\\system32").find ('\\') == std::string::npos);
    CHECK (sanitiseFileName ("") == "Untitled" && sanitiseFileName (" . . ") == "Untitled" && sanitiseFileName ("...") == "Untitled");
    CHECK (sanitiseFileName ("CON") == "_CON" && sanitiseFileName ("nul.txt") == "_nul.txt" && sanitiseFileName ("com1") == "_com1");
    CHECK (sanitiseFileName ("CONSOLE") == "CONSOLE");
    CHECK (sanitiseFileName ("trailing dots...") == "trailing dots");
    CHECK (sanitiseFileName ("tab\there\nnewline") == "tabherenewline");
    CHECK (sanitiseFileName (".hidden") == "hidden");
    const auto longName = sanitiseFileName (std::string (500, 'x'));
    CHECK (longName.size() == 96);
    std::string unicode;
    for (int i = 0; i < 100; ++i)
        unicode += "\xE2\x82\xAC"; // 3-byte code points
    const auto cut = sanitiseFileName (unicode);
    CHECK (cut.size() <= 96 && cut.size() % 3 == 0 && json::isValidUtf8 (cut));
    CHECK (sanitiseFileName ("bad\xff" "utf8") == "badutf8");
    CHECK (fileio::fileNamesEqualIgnoreCase ("My Bass.snaralpreset", "my bass.SNARALPRESET"));
    CHECK (! fileio::fileNamesEqualIgnoreCase ("a", "ab"));
}

void testFactoryIndex()
{
    TempDir tmp;
    PresetLibrary lib (LibraryPaths::forRoot (tmp.path()));
    const auto report = lib.rescan();
    const auto& factory = factoryPresets();
    CHECK (report.skipped.empty() && report.notes.empty());
    CHECK (report.factory == static_cast<int> (factory.presets.size()) && report.user == 0 && report.pack == 0);
    CHECK (lib.entries().size() == factory.presets.size());
    const auto* e = lib.find ("factory:factory.foundation");
    CHECK (e != nullptr && e->origin == PresetOrigin::Factory && e->name == "Foundation" && e->path.empty());
    CHECK (e->facets.layer == LayerBalance::SubForward && ! e->tags.empty());
    CHECK (e->searchText.find ("foundation") != std::string::npos);
    const auto loaded = lib.loadPreset (e->id);
    CHECK (loaded.ok && loaded.data.name == "Foundation");
    CHECK (! lib.loadPreset ("factory:nope").ok);

    // the factory set is read-only to every user operation
    CHECK (! lib.deleteUserPreset (e->id).ok);
    CHECK (! lib.renameUserPreset (e->id, "x").ok());
    CHECK (! fs::exists (tmp.path() / "Presets")); // nothing was written
}

void testSaveOverwriteAndAtomicity()
{
    TempDir tmp;
    PresetLibrary lib (LibraryPaths::forRoot (tmp.path()));
    lib.rescan();
    const auto factoryCount = lib.entries().size();

    const auto first = makePreset ("My Bass", 1);
    auto r = lib.saveUserPreset (first);
    CHECK_MSG (r.ok(), r.message);
    CHECK (r.id == "user:My Bass.snaralpreset");
    const auto file = tmp.path() / "Presets" / "My Bass.snaralpreset";
    CHECK (fs::exists (file) && slurp (file) == writeOrDie (first));
    CHECK (lib.entries().size() == factoryCount + 1);
    CHECK (lib.find (r.id)->origin == PresetOrigin::User);

    // name collision (also case-insensitive) needs explicit confirmation; nothing is written
    const auto second = makePreset ("my bass", 2);
    r = lib.saveUserPreset (second);
    CHECK (r.status == SaveStatus::OverwriteRequired && r.path == fileio::pathToUtf8 (file));
    CHECK (slurp (file) == writeOrDie (first));
    CHECK (fileCount (tmp.path() / "Presets") == 1);

    // injected failure while overwriting: the old file is intact, index unchanged
    fileio::WriteHooks failing;
    failing.beforeCommit = [] { return false; };
    LibraryHooks hooks;
    hooks.write = &failing;
    r = lib.saveUserPreset (second, true, hooks);
    CHECK (r.status == SaveStatus::IoError && ! r.message.empty());
    CHECK (slurp (file) == writeOrDie (first));
    CHECK (fileCount (tmp.path() / "Presets") == 1);
    CHECK (lib.find ("user:My Bass.snaralpreset")->name == "My Bass");

    // confirmed overwrite keeps the existing file name and replaces the content
    r = lib.saveUserPreset (second, true);
    CHECK (r.ok() && r.id == "user:My Bass.snaralpreset");
    CHECK (slurp (file) == writeOrDie (second));
    CHECK (lib.find (r.id)->name == "my bass");
    CHECK (lib.entries().size() == factoryCount + 1);

    // invalid presets are refused with the reason, nothing written
    PresetData bad = makePreset ("", 3);
    r = lib.saveUserPreset (bad);
    CHECK (r.status == SaveStatus::Invalid && ! r.issues.empty() && r.message.find ("name") != std::string::npos);
    bad = makePreset ("Has\nNewline", 3);
    CHECK (lib.saveUserPreset (bad).status == SaveStatus::Invalid);
    CHECK (fileCount (tmp.path() / "Presets") == 1);

    // hostile names cannot escape the preset folder
    r = lib.saveUserPreset (makePreset ("../../evil", 4));
    CHECK (r.ok());
    CHECK (fileio::pathToUtf8 (fs::path (r.path).parent_path()) == fileio::pathToUtf8 (tmp.path() / "Presets"));
    CHECK (! fs::exists (tmp.path() / "evil.snaralpreset"));

    // a factory preset name does not collide with (and never overwrites) factory data
    r = lib.saveUserPreset (makePreset ("Foundation", 5));
    CHECK (r.ok() && r.id == "user:Foundation.snaralpreset");
    CHECK (lib.find ("factory:factory.foundation") != nullptr);
}

void testImportExport()
{
    TempDir tmp;
    PresetLibrary lib (LibraryPaths::forRoot (tmp.path() / "data"));
    lib.rescan();
    const auto inbox = tmp.path() / "inbox";

    const auto good = makePreset ("Imported Bass", 8, "user.imported");
    spit (inbox / "good.snaralpreset", writeOrDie (good));
    auto r = lib.importPresetFile (inbox / "good.snaralpreset");
    CHECK_MSG (r.ok(), r.message);
    CHECK (r.id == "user:Imported Bass.snaralpreset");
    CHECK (slurp (tmp.path() / "data" / "Presets" / "Imported Bass.snaralpreset") == writeOrDie (good)); // canonical copy
    CHECK (lib.loadPreset (r.id).ok);

    // importing the same name again asks first
    r = lib.importPresetFile (inbox / "good.snaralpreset");
    CHECK (r.status == SaveStatus::OverwriteRequired);

    // invalid candidates never change anything
    const auto presetsBefore = fileCount (tmp.path() / "data" / "Presets");
    const auto entriesBefore = lib.entries().size();
    const std::string text = writeOrDie (good);
    auto replaceFirst = [&] (std::string from, std::string to)
    {
        auto s = text;
        const auto pos = s.find (from);
        CHECK (pos != std::string::npos);
        s.replace (pos, from.size(), to);
        return s;
    };
    const std::vector<std::pair<std::string, std::string>> badFiles {
        { "truncated", text.substr (0, text.size() / 2) },
        { "future", replaceFirst ("\"schema_version\": 1", "\"schema_version\": 2") },
        { "wrongformat", replaceFirst ("snaral-preset", "other-format") },
        { "range", replaceFirst ("\"weight\":", "\"weight\": 1000, \"w\":") },
        { "enum", replaceFirst ("\"voice_mode\": \"", "\"voice_mode\": \"zz") },
        { "duplicate", replaceFirst ("\"format\":", "\"format\": \"snaral-preset\", \"format\":") },
        { "garbage", "not json at all" },
        { "empty", "" } };
    for (const auto& [name, content] : badFiles)
    {
        spit (inbox / (name + ".snaralpreset"), content);
        r = lib.importPresetFile (inbox / (name + ".snaralpreset"), true);
        CHECK_MSG (r.status == SaveStatus::Invalid, name);
        CHECK_MSG (r.message.find (name + ".snaralpreset") != std::string::npos, "message names the file: " + r.message);
    }
    CHECK (lib.importPresetFile (inbox / "missing.snaralpreset").status == SaveStatus::IoError);
    CHECK (fileCount (tmp.path() / "data" / "Presets") == presetsBefore && lib.entries().size() == entriesBefore);

    // oversized file is refused without reading it all
    spit (inbox / "huge.snaralpreset", std::string (maxPresetFileBytes + 10, ' '));
    CHECK (lib.importPresetFile (inbox / "huge.snaralpreset").status == SaveStatus::IoError);

    // import with unknown parameters succeeds and reports warnings
    {
        auto withUnknown = writeOrDie (makePreset ("Warn Bass", 9));
        const auto pos = withUnknown.find ("\"weight\":");
        CHECK (pos != std::string::npos);
        withUnknown.insert (pos, "\"from_the_future\": 1, ");
        spit (inbox / "future_param.snaralpreset", withUnknown);
        r = lib.importPresetFile (inbox / "future_param.snaralpreset");
        CHECK (r.ok() && ! r.warnings.empty());
    }

    // export: canonical bytes, extension added, confirmation required, atomic
    const auto out = tmp.path() / "exports" / "copy";
    auto e = lib.exportPreset ("factory:factory.clean_sub", out);
    CHECK_MSG (e.ok(), e.message);
    const auto exported = tmp.path() / "exports" / "copy.snaralpreset";
    CHECK (fs::exists (exported) && parsePreset (slurp (exported)).ok);
    CHECK (parsePreset (slurp (exported)).data.name == "Clean Sub");
    const auto before = slurp (exported);
    e = lib.exportPreset ("factory:factory.foundation", exported);
    CHECK (e.status == SaveStatus::OverwriteRequired && slurp (exported) == before);
    fileio::WriteHooks failing;
    failing.beforeCommit = [] { return false; };
    LibraryHooks hooks;
    hooks.write = &failing;
    e = lib.exportPreset ("factory:factory.foundation", exported, true, hooks);
    CHECK (e.status == SaveStatus::IoError && slurp (exported) == before && fileCount (exported.parent_path()) == 1);
    e = lib.exportPreset ("factory:factory.foundation", exported, true);
    CHECK (e.ok() && parsePreset (slurp (exported)).data.name == "Foundation");
    CHECK (lib.exportPreset ("user:nope", exported, true).status == SaveStatus::Invalid);
}

void testRenameDeleteAndRescan()
{
    TempDir tmp;
    const auto paths = LibraryPaths::forRoot (tmp.path());
    PresetLibrary lib (paths);
    lib.rescan();
    auto r = lib.saveUserPreset (makePreset ("Old Name", 1));
    CHECK (r.ok());
    CHECK (lib.setFavourite (r.id, true).ok);

    const auto renamed = lib.renameUserPreset (r.id, "New Name");
    CHECK_MSG (renamed.ok(), renamed.message);
    CHECK (renamed.id == "user:New Name.snaralpreset");
    CHECK (! fs::exists (tmp.path() / "Presets" / "Old Name.snaralpreset") && fs::exists (tmp.path() / "Presets" / "New Name.snaralpreset"));
    CHECK (lib.find (r.id) == nullptr && lib.find (renamed.id)->name == "New Name");
    CHECK (lib.isFavourite (renamed.id) && ! lib.isFavourite (r.id)); // favourite follows the rename

    // case-only rename of the same file
    const auto recased = lib.renameUserPreset (renamed.id, "new name");
    CHECK (recased.ok());
    CHECK (fileCount (tmp.path() / "Presets") == 1);

    // renaming onto another preset needs confirmation
    CHECK (lib.saveUserPreset (makePreset ("Other", 2)).ok());
    CHECK (lib.renameUserPreset (recased.id, "Other").status == SaveStatus::OverwriteRequired);
    CHECK (fileCount (tmp.path() / "Presets") == 2);

    // rescan from disk sees what was saved; junk is skipped with the file name
    spit (tmp.path() / "Presets" / "broken.snaralpreset", "{ nope");
    spit (tmp.path() / "Presets" / "readme.txt", "not a preset");
    spit (tmp.path() / "Presets" / ".hidden.snaralpreset", "x");
    spit (tmp.path() / "Presets" / "huge.snaralpreset", std::string (maxPresetFileBytes + 1, 'x'));
    fs::create_directories (tmp.path() / "Presets" / "sub.snaralpreset");
    PresetLibrary lib2 (paths);
    const auto report = lib2.rescan();
    CHECK (report.user == 2);
    CHECK (report.skipped.size() == 2);
    bool sawBroken = false, sawHuge = false;
    for (const auto& s : report.skipped)
    {
        sawBroken = sawBroken || (s.path == "broken.snaralpreset" && ! s.message.empty());
        sawHuge = sawHuge || s.path == "huge.snaralpreset";
    }
    CHECK (sawBroken && sawHuge);
    CHECK (lib2.find (recased.id) != nullptr && lib2.find (recased.id)->name == "new name");
    CHECK (lib2.isFavourite (recased.id)); // persisted across "restart"

    CHECK (lib2.deleteUserPreset (recased.id).ok);
    CHECK (lib2.find (recased.id) == nullptr && ! fs::exists (tmp.path() / "Presets" / "New Name.snaralpreset"));
    CHECK (! lib2.isFavourite (recased.id));
    CHECK (! lib2.deleteUserPreset (recased.id).ok);
}

void testFavouritesAndRecentPersistence()
{
    TempDir tmp;
    const auto paths = LibraryPaths::forRoot (tmp.path());
    {
        PresetLibrary lib (paths);
        lib.rescan();
        CHECK (lib.setFavourite ("factory:factory.foundation", true).ok);
        CHECK (lib.setFavourite ("factory:factory.tearout", true).ok);
        CHECK (lib.setFavourite ("factory:factory.tearout", false).ok);
        CHECK (lib.markUsed ("factory:factory.saw_rasp", 1000).ok);
        CHECK (lib.markUsed ("factory:factory.clean_sub", 2000).ok);
        CHECK (lib.markUsed ("factory:factory.saw_rasp", 3000).ok);
        CHECK (lib.find ("factory:factory.foundation")->favourite);
    }
    CHECK (fs::exists (paths.stateFile));
    {
        PresetLibrary lib (paths);
        const auto report = lib.rescan();
        CHECK (report.notes.empty());
        CHECK (lib.find ("factory:factory.foundation")->favourite);
        CHECK (! lib.find ("factory:factory.tearout")->favourite);
        CHECK (lib.find ("factory:factory.saw_rasp")->lastUsed == 3000);
        CHECK (lib.find ("factory:factory.clean_sub")->lastUsed == 2000);
        CHECK (lib.find ("factory:factory.free_rotor")->lastUsed == 0);
    }

    // the state file is deterministic JSON
    const auto before = slurp (paths.stateFile);
    {
        PresetLibrary lib (paths);
        lib.rescan();
        CHECK (lib.saveBrowserState().ok);
    }
    CHECK (slurp (paths.stateFile) == before);
    json::Value v;
    json::ParseError err;
    CHECK (json::parse (before, v, err) && v.find ("format")->text == "snaral-browser-state");

    // corrupt state: reported, quarantined, library still works
    spit (paths.stateFile, "{ this is not json");
    {
        PresetLibrary lib (paths);
        const auto report = lib.rescan();
        CHECK (report.notes.size() == 1 && report.notes[0].path == "browser_state.json");
        CHECK (! lib.find ("factory:factory.foundation")->favourite);
        CHECK (fs::exists (fs::path (paths.stateFile.string() + ".corrupt")));
        CHECK (lib.setFavourite ("factory:factory.foundation", true).ok);
    }

    // state from a newer build is left untouched
    const std::string newer = "{\"format\": \"snaral-browser-state\", \"favourites\": [], \"schema_version\": 99}";
    spit (paths.stateFile, newer);
    {
        PresetLibrary lib (paths);
        const auto report = lib.rescan();
        CHECK (report.notes.size() == 1);
        CHECK (slurp (paths.stateFile) == newer);
    }

    // unwritable state location: the in-memory change stays, the failure is reported
    {
        TempDir blocked;
        spit (blocked.path() / "file", "x");
        LibraryPaths bad = LibraryPaths::forRoot (blocked.path());
        bad.stateFile = blocked.path() / "file" / "browser_state.json";
        PresetLibrary lib (bad);
        lib.rescan();
        const auto io = lib.setFavourite ("factory:factory.foundation", true);
        CHECK (! io.ok && ! io.error.empty());
        CHECK (lib.isFavourite ("factory:factory.foundation"));
    }
}

struct PackFixture
{
    fs::path dir;
    PackManifest manifest;
};

PackFixture makePack (const fs::path& root, const std::string& packId, int count, const std::string& idPrefix)
{
    PackFixture f;
    f.dir = root / packId;
    f.manifest.packId = packId;
    f.manifest.name = "Pack " + packId;
    f.manifest.version = "1.0.0";
    f.manifest.author = "Pack Author";
    f.manifest.license = "Proprietary, free to use in music";
    f.manifest.minPluginVersion = "0.2.0";
    for (int i = 0; i < count; ++i)
    {
        const auto fileName = "preset_" + std::to_string (i) + ".snaralpreset";
        auto d = makePreset ("Pack Preset " + std::to_string (i), 500 + static_cast<std::uint64_t> (i), idPrefix + std::to_string (i));
        spit (f.dir / fileName, writeOrDie (d));
        f.manifest.files.push_back (fileName);
    }
    f.manifest.presetCount = count;
    std::string manifest;
    CHECK (writePackManifest (f.manifest, manifest));
    spit (f.dir / "manifest.json", manifest);
    return f;
}

void testPackManifest()
{
    PackManifest m;
    m.packId = "good-pack";
    m.name = "Good";
    m.version = "1";
    m.license = "MIT";
    m.files = { "a.snaralpreset" };
    m.presetCount = 1;
    std::string text;
    CHECK (writePackManifest (m, text));
    auto r = parsePackManifest (text);
    CHECK_MSG (r.ok, r.errors.empty() ? "" : describe (r.errors[0]));
    CHECK (r.manifest.packId == "good-pack" && r.manifest.files.size() == 1 && r.manifest.license == "MIT");

    auto expectBad = [&] (const std::string& from, const std::string& to, const char* what)
    {
        auto s = text;
        const auto pos = s.find (from);
        CHECK_MSG (pos != std::string::npos, what);
        s.replace (pos, from.size(), to);
        CHECK_MSG (! parsePackManifest (s).ok, what);
    };
    expectBad ("\"preset_count\": 1", "\"preset_count\": 2", "count mismatch");
    expectBad ("a.snaralpreset", "../a.snaralpreset", "path traversal");
    expectBad ("a.snaralpreset", "a.exe", "wrong extension");
    expectBad ("a.snaralpreset", "sub/a.snaralpreset", "separator");
    expectBad ("\"good-pack\"", "\"Bad Pack\"", "bad id");
    expectBad ("\"license\": \"MIT\"", "\"license\": \"\"", "empty license");
    expectBad ("\"schema_version\": 1", "\"schema_version\": 2", "future manifest");
    expectBad ("snaral-pack", "snaral-packs", "format");
    CHECK (! parsePackManifest ("{").ok && ! parsePackManifest ("").ok);
    m.files = { "a.snaralpreset", "A.snaralpreset" };
    m.presetCount = 2;
    CHECK (! writePackManifest (m, text)); // duplicate (case-insensitive) file names
    m.files.clear();
    m.presetCount = 0;
    CHECK (! writePackManifest (m, text));
}

void testPackImport()
{
    TempDir tmp;
    const auto paths = LibraryPaths::forRoot (tmp.path() / "data");
    PresetLibrary lib (paths);
    lib.rescan();
    CHECK (lib.saveUserPreset (makePreset ("Keep Me", 1, "user.keepme")).ok());
    const auto base = lib.entries().size();
    const auto src = tmp.path() / "incoming";

    const auto pack = makePack (src, "dnb-essentials", 3, "pack.dnb.");
    auto r = lib.importPack (pack.dir, DuplicatePolicy::Cancel);
    CHECK_MSG (r.ok && r.changed, r.message);
    CHECK (r.items.size() == 3 && r.items[0].status == "imported");
    CHECK (lib.entries().size() == base + 3);
    const auto* info = lib.findPack ("dnb-essentials");
    CHECK (info != nullptr && info->installedCount == 3 && info->manifest.license == "Proprietary, free to use in music"
           && info->manifest.author == "Pack Author");
    const auto* entry = lib.find ("pack:dnb-essentials:preset_1.snaralpreset");
    CHECK (entry != nullptr && entry->origin == PresetOrigin::Pack && entry->packId == "dnb-essentials");
    CHECK (lib.loadPreset (entry->id).ok);
    CHECK (fs::exists (paths.packDir / "dnb-essentials" / "manifest.json"));
    CHECK (fileCount (paths.packDir) == 1); // staging folder is gone

    // survives a restart
    {
        PresetLibrary again (paths);
        const auto report = again.rescan();
        CHECK (report.pack == 3 && report.skipped.empty());
        CHECK (again.packs().size() == 1 && again.find (entry->id) != nullptr);
    }

    // re-import: Cancel asks, Skip keeps, Replace swaps
    r = lib.importPack (pack.dir, DuplicatePolicy::Cancel);
    CHECK (! r.ok && r.cancelled && ! r.changed);
    r = lib.importPack (pack.dir, DuplicatePolicy::Skip);
    CHECK (r.ok && ! r.changed && r.items[0].status == "skipped");
    r = lib.importPack (pack.dir, DuplicatePolicy::Replace);
    CHECK (r.ok && r.changed && lib.entries().size() == base + 3 && fileCount (paths.packDir) == 1);

    // a pack with one broken file is rejected as a whole
    const auto broken = makePack (src, "broken-pack", 3, "pack.broken.");
    spit (broken.dir / "preset_1.snaralpreset", "{ garbage");
    r = lib.importPack (broken.dir, DuplicatePolicy::Replace);
    CHECK (! r.ok && ! r.changed);
    CHECK (r.items[1].status == "invalid" && r.items[1].file == "preset_1.snaralpreset");
    CHECK (r.items[0].status == "cancelled" && r.items[2].status == "cancelled");
    CHECK (lib.findPack ("broken-pack") == nullptr && ! fs::exists (paths.packDir / "broken-pack") && lib.entries().size() == base + 3);

    // manifest lies about the count / lists a missing file
    const auto lying = makePack (src, "lying-pack", 2, "pack.lying.");
    fs::remove (lying.dir / "preset_0.snaralpreset");
    r = lib.importPack (lying.dir, DuplicatePolicy::Skip);
    CHECK (! r.ok && lib.findPack ("lying-pack") == nullptr);
    CHECK (! lib.importPack (src / "does-not-exist", DuplicatePolicy::Skip).ok);

    // duplicate preset ids against factory / user presets
    const auto dup = makePack (src, "dup-pack", 2, "pack.dup.");
    {
        auto d = makePreset ("Clashes With User", 77, "user.keepme");
        spit (dup.dir / "preset_0.snaralpreset", writeOrDie (d));
        d = makePreset ("Clashes With Factory", 78, "factory.foundation");
        spit (dup.dir / "preset_1.snaralpreset", writeOrDie (d));
    }
    r = lib.importPack (dup.dir, DuplicatePolicy::Cancel);
    CHECK (r.cancelled && ! r.changed && lib.findPack ("dup-pack") == nullptr);
    r = lib.importPack (dup.dir, DuplicatePolicy::Skip);
    CHECK (r.ok && ! r.changed && r.items[0].status == "skipped" && r.items[1].status == "skipped");
    CHECK (lib.find ("user:Keep Me.snaralpreset")->name == "Keep Me"); // user preset untouched

    // partial duplicates: the clean ones are installed, the manifest on disk lists only those
    {
        const auto mixed = makePack (src, "mixed-pack", 3, "pack.mixed.");
        auto d = makePreset ("Clash", 79, "user.keepme");
        spit (mixed.dir / "preset_2.snaralpreset", writeOrDie (d));
        r = lib.importPack (mixed.dir, DuplicatePolicy::Replace);
        CHECK (r.ok && r.changed && r.items[2].status == "skipped" && r.items[0].status == "imported");
        CHECK (lib.findPack ("mixed-pack")->installedCount == 2);
        PresetLibrary again (paths);
        const auto report = again.rescan();
        CHECK (report.skipped.empty() && again.findPack ("mixed-pack")->manifest.files.size() == 2);
    }

    // failure injected while copying: nothing installed, no staging leftovers
    {
        const auto victim = makePack (src, "victim-pack", 2, "pack.victim.");
        fileio::WriteHooks failing;
        failing.beforeCommit = [] { return false; };
        LibraryHooks hooks;
        hooks.write = &failing;
        const auto packsBefore = fileCount (paths.packDir);
        r = lib.importPack (victim.dir, DuplicatePolicy::Skip, hooks);
        CHECK (! r.ok && ! r.changed && lib.findPack ("victim-pack") == nullptr);
        CHECK (fileCount (paths.packDir) == packsBefore);
    }

    // failure while REPLACING keeps the installed version
    {
        const auto again = makePack (src, "dnb-essentials", 3, "pack.dnb.");
        fileio::WriteHooks failing;
        failing.beforeCommit = [] { return false; };
        LibraryHooks hooks;
        hooks.write = &failing;
        const auto packsBefore = fileCount (paths.packDir);
        r = lib.importPack (again.dir, DuplicatePolicy::Replace, hooks);
        CHECK (! r.ok && lib.findPack ("dnb-essentials")->installedCount == 3 && fileCount (paths.packDir) == packsBefore);
        CHECK (fs::exists (paths.packDir / "dnb-essentials" / "preset_2.snaralpreset"));
    }

    // removing a pack removes only that pack
    CHECK (lib.removePack ("dnb-essentials").ok);
    CHECK (lib.findPack ("dnb-essentials") == nullptr && lib.find ("pack:dnb-essentials:preset_1.snaralpreset") == nullptr);
    CHECK (! fs::exists (paths.packDir / "dnb-essentials"));
    CHECK (lib.find ("user:Keep Me.snaralpreset") != nullptr && fs::exists (paths.userPresetDir / "Keep Me.snaralpreset"));
    CHECK (! lib.removePack ("dnb-essentials").ok);
}
} // namespace

int main()
{
    testAtomicWrite();
    testFileNames();
    testFactoryIndex();
    testSaveOverwriteAndAtomicity();
    testImportExport();
    testRenameDeleteAndRescan();
    testFavouritesAndRecentPersistence();
    testPackManifest();
    testPackImport();
    std::cout << "PresetLibraryTests passed\n";
    return 0;
}
