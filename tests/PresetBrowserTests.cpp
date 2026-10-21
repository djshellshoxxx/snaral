#include "TestSupport.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <iostream>

#include "State/Pure/PresetBrowserModel.h"

using namespace snaral;
using namespace snaral::testing;

namespace
{
PresetEntry synthetic (int i, Rng& rng)
{
    static const char* adjectives[] = { "Dark", "Rolling", "Wide", "Heavy", "Hollow", "Rusty", "Sharp", "Deep" };
    static const char* nouns[] = { "Reese", "Growl", "Stab", "Sub", "Wobble", "Rasp", "Snarl", "Pluck" };
    static const char* tagPool[] = { "sub", "growl", "reese", "neuro", "wobble", "synced", "free", "static", "clean", "noise", "mono", "poly" };
    PresetData d;
    d.name = std::string (adjectives[rng.below (8)]) + " " + nouns[rng.below (8)] + " " + std::to_string (i);
    d.author = rng.below (2) ? "Circuit Drift Labs" : "Some User";
    d.description = "Test sound number " + std::to_string (i) + " for browsing";
    d.tags = { tagPool[rng.below (12)], tagPool[(rng.below (11) + 1 + static_cast<int> (i)) % 12] };
    if (d.tags[0] == d.tags[1])
        d.tags.pop_back();
    d.params = randomSnapshot (static_cast<std::uint64_t> (i) + 1000);
    return PresetLibrary::makeEntry (d, i % 3 == 0 ? PresetOrigin::Factory : PresetOrigin::User, "id:" + std::to_string (i), {}, {});
}

std::vector<std::string> ids (const PresetBrowserModel& m)
{
    std::vector<std::string> out;
    for (int i = 0; i < m.resultCount(); ++i)
        out.push_back (m.entryAt (i).id);
    return out;
}

void testFactorySearchAndFilters()
{
    TempDir tmp;
    PresetLibrary lib (LibraryPaths::forRoot (tmp.path()));
    lib.rescan();
    PresetBrowserModel model (lib);
    const int total = static_cast<int> (lib.entries().size());
    CHECK (model.resultCount() == total);
    CHECK (model.emptyStateMessage().empty());

    // sorted by name (case-insensitively) by default
    for (int i = 1; i < model.resultCount(); ++i)
        CHECK (foldCase (model.entryAt (i - 1).name) <= foldCase (model.entryAt (i).name));

    // every factory preset can be found by its own name, in any letter case
    for (const auto& e : lib.entries())
    {
        BrowserQuery q;
        q.text = "  " + e.name + "  ";
        for (auto& c : q.text)
            c = static_cast<char> (std::toupper (static_cast<unsigned char> (c)));
        model.setQuery (q);
        bool found = false;
        for (int i = 0; i < model.resultCount(); ++i)
            found = found || model.entryAt (i).id == e.id;
        CHECK_MSG (found, e.name);
    }

    // tags, author and description are searched (case-insensitive), terms are ANDed
    BrowserQuery q;
    q.text = "WOBBLE";
    model.setQuery (q);
    CHECK (model.resultCount() >= 3);
    for (int i = 0; i < model.resultCount(); ++i)
        CHECK (model.entryAt (i).searchText.find ("wobble") != std::string::npos);
    q.text = "wobble synced";
    model.setQuery (q);
    const int both = model.resultCount();
    CHECK (both >= 2 && both <= 3 + 2);
    q.text = "circuit drift labs";
    model.setQuery (q);
    CHECK (model.resultCount() == total); // author field
    q.text = "tempo-locked";
    model.setQuery (q);
    CHECK (model.resultCount() >= 1); // description field
    q.text = "zzzz-not-found";
    model.setQuery (q);
    CHECK (model.resultCount() == 0 && model.emptyStateMessage().find ("zzzz-not-found") != std::string::npos);

    // tag filter (exact, ANDed, case-insensitive)
    q = BrowserQuery {};
    q.tags = { "WOBBLE", "synced" };
    model.setQuery (q);
    CHECK (model.resultCount() == 3 || model.resultCount() == 2);
    for (int i = 0; i < model.resultCount(); ++i)
    {
        const auto& t = model.entryAt (i).tags;
        CHECK (std::find (t.begin(), t.end(), "wobble") != t.end() && std::find (t.begin(), t.end(), "synced") != t.end());
    }
    q.tags = { "wob" }; // not a prefix filter
    model.setQuery (q);
    CHECK (model.resultCount() == 0);

    // facet filters
    q = BrowserQuery {};
    q.filterLayer = true;
    q.layer = LayerBalance::SubForward;
    model.setQuery (q);
    CHECK (model.resultCount() >= 3);
    for (int i = 0; i < model.resultCount(); ++i)
        CHECK (model.entryAt (i).facets.layer == LayerBalance::SubForward);
    q.motionMask = motionStatic;
    model.setQuery (q);
    const int staticSub = model.resultCount();
    CHECK (staticSub >= 2);
    for (int i = 0; i < model.resultCount(); ++i)
        CHECK ((model.entryAt (i).facets.motion & motionStatic) != 0);
    q.textureMask = textureMetal; // sub-forward + static + metal: composes down further
    model.setQuery (q);
    CHECK (model.resultCount() <= staticSub);
    q = BrowserQuery {};
    q.motionMask = motionSynced | motionFree;
    model.setQuery (q);
    CHECK (model.resultCount() >= 5);
    for (int i = 0; i < model.resultCount(); ++i)
        CHECK ((model.entryAt (i).facets.motion & (motionSynced | motionFree)) != 0);
    q = BrowserQuery {};
    q.textureMask = textureNoise | textureMetal;
    model.setQuery (q);
    CHECK (model.resultCount() >= 2 && model.resultCount() < total);

    // filters compose with text; impossible combinations give the empty state
    q = BrowserQuery {};
    q.text = "foundation";
    q.origin = OriginFilter::User;
    model.setQuery (q);
    CHECK (model.resultCount() == 0 && ! model.emptyStateMessage().empty());
    q.origin = OriginFilter::Factory;
    model.setQuery (q);
    CHECK (model.resultCount() >= 1 && model.emptyStateMessage().empty());

    // favourites
    q = BrowserQuery {};
    q.origin = OriginFilter::Favourites;
    model.setQuery (q);
    CHECK (model.resultCount() == 0);
    CHECK (model.emptyStateMessage().find ("No favourites yet") != std::string::npos);
    CHECK (lib.setFavourite ("factory:factory.tearout", true).ok);   // library change invalidates the cached results
    CHECK (model.resultCount() == 1 && model.entryAt (0).name == "Tearout");
    q.text = "foundation";
    model.setQuery (q);
    CHECK (model.resultCount() == 0 && model.emptyStateMessage().find ("foundation") != std::string::npos);

    // available tags: counted, most common first
    const auto tags = model.availableTags();
    CHECK (! tags.empty());
    for (std::size_t i = 1; i < tags.size(); ++i)
        CHECK (tags[i - 1].second >= tags[i].second);
    int cleanCount = 0;
    for (const auto& [tag, n] : tags)
        if (tag == "clean")
            cleanCount = n;
    CHECK (cleanCount > 10);
}

void testSortModes()
{
    TempDir tmp;
    PresetLibrary lib (LibraryPaths::forRoot (tmp.path()));
    lib.rescan (FactoryPresetSet {});
    auto add = [&] (const std::string& id, const std::string& name, std::vector<std::string> tags, long long used)
    {
        PresetData d;
        d.name = name;
        d.tags = std::move (tags);
        lib.upsertEntry (PresetLibrary::makeEntry (d, PresetOrigin::User, id, {}, {}));
        if (used > 0)
            CHECK (lib.markUsed (id, used).ok);
    };
    add ("1", "banana", { "wobble" }, 0);
    add ("2", "Apple", { "sub" }, 50);
    add ("3", "cherry", { }, 100);
    add ("4", "Avocado", { "sub", "x" }, 100);
    add ("5", "apple", { "growl" }, 10);

    PresetBrowserModel model (lib);
    BrowserQuery q;
    q.sort = SortMode::Name;
    model.setQuery (q);
    CHECK ((ids (model) == std::vector<std::string> { "2", "5", "4", "1", "3" })); // apple/Apple tie broken by id
    q.sort = SortMode::RecentlyUsed;
    model.setQuery (q);
    CHECK ((ids (model) == std::vector<std::string> { "4", "3", "2", "5", "1" })); // newest first, ties by name, never used last
    q.sort = SortMode::Category;
    model.setQuery (q);
    CHECK ((ids (model) == std::vector<std::string> { "5", "2", "4", "1", "3" })); // growl, sub, sub, wobble, untagged last

    // navigation
    q.sort = SortMode::Name;
    model.setQuery (q);
    CHECK (model.selectedId().empty() && model.selectedResultIndex() == -1);
    CHECK (model.selectNext() && model.selectedId() == "2");
    CHECK (model.selectNext() && model.selectedId() == "5");
    CHECK (model.selectPrevious() && model.selectedId() == "2");
    CHECK (model.selectPrevious() && model.selectedId() == "3"); // wraps to the end
    CHECK (model.selectNext() && model.selectedId() == "2");     // and back to the start
    CHECK (model.select ("3") && ! model.selectNext (false) && model.selectedId() == "3");
    CHECK (model.select ("2") && ! model.selectPrevious (false) && model.selectedId() == "2");
    CHECK (! model.select ("missing") && model.selectedId() == "2");

    // a selection hidden by a filter is "not in the list"; navigation restarts at the ends
    q.text = "cherry";
    model.setQuery (q);
    CHECK (model.selectedResultIndex() == -1);
    CHECK (model.selectNext() && model.selectedId() == "3");
    CHECK (! model.selectNext() || model.selectedId() == "3"); // single result: stays
    q.text = "nothing";
    model.setQuery (q);
    CHECK (! model.selectNext() && ! model.selectPrevious());
    CHECK (model.emptyStateMessage().find ("nothing") != std::string::npos);

    TempDir other;
    PresetLibrary empty (LibraryPaths::forRoot (other.path()));
    empty.rescan (FactoryPresetSet {});
    PresetBrowserModel emptyModel (empty);
    CHECK (emptyModel.resultCount() == 0 && emptyModel.emptyStateMessage().find ("No presets found") != std::string::npos);
}

void testSearchSpeed()
{
    TempDir tmp;
    PresetLibrary lib (LibraryPaths::forRoot (tmp.path()));
    lib.rescan (FactoryPresetSet {});
    Rng rng (5);
    for (int i = 0; i < 2000; ++i)
        lib.upsertEntry (synthetic (i, rng));
    CHECK (lib.entries().size() == 2000);

    PresetBrowserModel model (lib);
    using Clock = std::chrono::steady_clock;
    double worstMs = 0.0;
    const char* queries[] = { "reese", "DARK growl", "labs", "wobble synced", "number 19", "x", "heavy stab 1", "zzz" };
    for (const char* text : queries)
    {
        BrowserQuery q;
        q.text = text;
        q.sort = SortMode::RecentlyUsed;
        const auto t0 = Clock::now();
        model.setQuery (q);
        const int n = model.resultCount();
        const double ms = std::chrono::duration<double, std::milli> (Clock::now() - t0).count();
        worstMs = std::max (worstMs, ms);
        (void) n;
    }
    std::cout << "worst search over 2000 presets: " << worstMs << " ms\n";
    CHECK (worstMs < 200.0);

    // results are consistent with a brute-force reference
    BrowserQuery q;
    q.text = "dark reese";
    q.tags = { "sub" };
    model.setQuery (q);
    int expected = 0;
    for (const auto& e : lib.entries())
        if (e.searchText.find ("dark") != std::string::npos && e.searchText.find ("reese") != std::string::npos
            && std::find (e.tags.begin(), e.tags.end(), "sub") != e.tags.end())
            ++expected;
    CHECK (model.resultCount() == expected);
}

void testModifiedTrackingAndAudition()
{
    TempDir tmp;
    PresetLibrary lib (LibraryPaths::forRoot (tmp.path()));
    lib.rescan();
    PresetBrowserModel model (lib);
    CHECK (! model.hasUnsavedChanges() && model.displayName() == "Init");

    ParamSnapshot loaded = randomSnapshot (3);
    model.notifyLoaded ("factory:factory.foundation", "Foundation", loaded);
    CHECK (model.displayName() == "Foundation" && ! model.needsConfirmationBeforeLoad());

    ParamSnapshot edited = loaded;
    edited[P::bite] = loaded[P::bite] > 50.0f ? loaded[P::bite] - 10.0f : loaded[P::bite] + 10.0f;
    model.notifyEdited (edited);
    CHECK (model.hasUnsavedChanges() && model.needsConfirmationBeforeLoad() && model.displayName() == "Foundation*");
    model.notifyEdited (loaded); // edited back by hand
    CHECK (! model.hasUnsavedChanges());
    ParamSnapshot utilityOnly = loaded;
    utilityOnly[P::bypass] = 1.0f;
    model.notifyEdited (utilityOnly); // bypass is not part of the patch
    CHECK (! model.hasUnsavedChanges());

    // audition: remembers the ORIGINAL state including dirty edits, cancel restores exactly
    model.notifyEdited (edited);
    CHECK (model.hasUnsavedChanges());
    CHECK (! model.auditionActive() && ! model.cancelAudition().has_value());
    model.beginAudition (edited);
    CHECK (model.auditionActive());
    model.setAuditionTarget ("factory:factory.tearout");
    ParamSnapshot preview = randomSnapshot (9);
    model.notifyEdited (preview); // the preview is on the engine; dirty flag wobbles meanwhile
    model.beginAudition (preview); // switching to the next preview must NOT replace the saved state
    model.setAuditionTarget ("factory:factory.saw_rasp");
    const auto restore = model.cancelAudition();
    CHECK (restore.has_value() && ! model.auditionActive() && model.auditionTarget().empty());
    CHECK (snapshotsIdentical (restore->snapshot, edited));
    CHECK (restore->modified && restore->loadedId == "factory:factory.foundation" && restore->loadedName == "Foundation");
    CHECK (model.hasUnsavedChanges() && model.loadedId() == "factory:factory.foundation");

    // confirm keeps the preview as the loaded, clean preset
    model.beginAudition (edited);
    CHECK (model.confirmAudition ("factory:factory.tearout", "Tearout", preview));
    CHECK (! model.auditionActive() && ! model.hasUnsavedChanges() && model.displayName() == "Tearout");
    model.notifyEdited (preview);
    CHECK (! model.hasUnsavedChanges());
    CHECK (! model.confirmAudition ("x", "x", preview)); // nothing to confirm

    CHECK (auditionMustPreserveHeldNotes);
    model.setModified (true);
    CHECK (model.hasUnsavedChanges());
}
} // namespace

int main()
{
    testFactorySearchAndFilters();
    testSortModes();
    testSearchSpeed();
    testModifiedTrackingAndAudition();
    std::cout << "PresetBrowserTests passed\n";
    return 0;
}
