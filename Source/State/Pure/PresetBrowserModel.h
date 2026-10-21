#pragma once

// Data side of the preset browser (docs/specs/expansion-preset-browser.md): query -> ordered result list,
// selection and next/previous navigation, loaded-preset / unsaved-changes tracking and the audition contract.
// JUCE-free; the editor binds to this model and performs the actual processor calls.

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "PresetLibrary.h"

namespace snaral
{
enum class SortMode { Name, RecentlyUsed, Category };
enum class OriginFilter { All, Factory, User, Packs, Favourites };

struct BrowserQuery
{
    std::string text;                  // whitespace separated terms; ALL must match name/tags/author/description (ASCII case-insensitive)
    std::vector<std::string> tags;     // ALL listed tags must be present (case-insensitive exact tag match)
    OriginFilter origin = OriginFilter::All;
    bool filterLayer = false;
    LayerBalance layer = LayerBalance::Balanced;
    unsigned motionMask = 0;           // 0 = any; otherwise entry must have at least one of the MotionBits
    unsigned textureMask = 0;          // 0 = any; otherwise entry must have at least one of the TextureBits
    SortMode sort = SortMode::Name;

    bool operator== (const BrowserQuery&) const = default;
};

/// What the editor must do when an audition is cancelled.
struct AuditionRestore
{
    ParamSnapshot snapshot;   // publish through the normal atomic path (sound params; utility untouched)
    bool modified = false;    // dirty flag as it was before the audition
    std::string loadedId;
    std::string loadedName;
};

/// Contract for the audition (preview) feature. The model only holds state; the editor/processor must:
///  1. call beginAudition(current) before the first preview load (it remembers state + dirty flag once),
///  2. publish each previewed preset as one atomic parameter snapshot WITHOUT sending note-offs, resetting
///     voices or touching held notes (mustPreserveHeldNotes),
///  3. end with confirmAudition() (keep) or cancelAudition() (publish the returned AuditionRestore).
inline constexpr bool auditionMustPreserveHeldNotes = true;

class PresetBrowserModel
{
public:
    explicit PresetBrowserModel (PresetLibrary& library) : library_ (library) {}

    // ---- query / results ----
    void setQuery (const BrowserQuery& query);
    const BrowserQuery& query() const noexcept { return query_; }
    /// Indices into library.entries(), filtered and sorted. Cached until the query or library changes.
    const std::vector<int>& results() const;
    int resultCount() const { return static_cast<int> (results().size()); }
    const PresetEntry& entryAt (int resultIndex) const;
    /// Empty when there are results; otherwise a short user-facing explanation with a hint.
    std::string emptyStateMessage() const;
    /// Tags in use across the whole library with counts, most common first (display casing of first occurrence).
    std::vector<std::pair<std::string, int>> availableTags() const;

    // ---- selection / navigation ----
    const std::string& selectedId() const noexcept { return selectedId_; }
    bool select (const std::string& id);          // false if not in the current results
    int selectedResultIndex() const;               // -1 if none / filtered out
    /// Moves to the next/previous result; with wrap the ends connect. With nothing selected, next selects the
    /// first and previous the last result. Returns true if the selection changed.
    bool selectNext (bool wrap = true);
    bool selectPrevious (bool wrap = true);

    // ---- loaded preset / unsaved changes ----
    /// Call after a preset (or Init, with an empty id) has been applied to the processor.
    void notifyLoaded (const std::string& id, const std::string& name, const ParamSnapshot& applied);
    /// Call whenever parameters change; recomputes the modified flag by comparing with the loaded state, so an
    /// edit that is reverted by hand clears the flag again.
    void notifyEdited (const ParamSnapshot& current);
    void setModified (bool modified) noexcept { modified_ = modified; }
    bool hasUnsavedChanges() const noexcept { return modified_; }
    /// True when loading something else would discard edits (the UI should ask first).
    bool needsConfirmationBeforeLoad() const noexcept { return modified_; }
    const std::string& loadedId() const noexcept { return loadedId_; }
    const std::string& loadedName() const noexcept { return loadedName_; }
    /// "Name" or "Name*" (modified) or "Init" when nothing is loaded.
    std::string displayName() const;

    // ---- audition ----
    bool auditionActive() const noexcept { return audition_.has_value(); }
    void beginAudition (const ParamSnapshot& current);  // idempotent while active (keeps the ORIGINAL state)
    void setAuditionTarget (const std::string& id) { auditionTarget_ = id; }
    const std::string& auditionTarget() const noexcept { return auditionTarget_; }
    /// Ends the audition and returns what to restore (nullopt if none was active).
    std::optional<AuditionRestore> cancelAudition();
    /// Ends the audition and keeps the previewed preset as the loaded one (clean, not modified).
    bool confirmAudition (const std::string& id, const std::string& name, const ParamSnapshot& applied);

private:
    void rebuild() const;

    PresetLibrary& library_;
    BrowserQuery query_;
    std::string selectedId_;

    mutable std::vector<int> results_;
    mutable std::uint64_t cachedRevision_ = ~0ull;
    mutable std::uint64_t cachedQueryVersion_ = ~0ull;
    std::uint64_t queryVersion_ = 0;

    std::string loadedId_, loadedName_;
    ParamSnapshot reference_;
    bool modified_ = false;

    std::optional<AuditionRestore> audition_;
    std::string auditionTarget_;
};
} // namespace snaral
