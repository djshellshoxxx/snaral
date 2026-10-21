#include "PresetBrowserModel.h"

#include <algorithm>
#include <map>

namespace snaral
{
namespace
{
std::vector<std::string> splitTerms (const std::string& text)
{
    std::vector<std::string> terms;
    std::string cur;
    for (const char c : foldCase (text))
    {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
        {
            if (! cur.empty())
                terms.push_back (std::move (cur));
            cur.clear();
        }
        else
        {
            cur.push_back (c);
        }
    }
    if (! cur.empty())
        terms.push_back (std::move (cur));
    return terms;
}

bool matches (const PresetEntry& e, const BrowserQuery& q, const std::vector<std::string>& terms,
              const std::vector<std::string>& foldedTags)
{
    switch (q.origin)
    {
        case OriginFilter::All: break;
        case OriginFilter::Factory: if (e.origin != PresetOrigin::Factory) return false; break;
        case OriginFilter::User: if (e.origin != PresetOrigin::User) return false; break;
        case OriginFilter::Packs: if (e.origin != PresetOrigin::Pack) return false; break;
        case OriginFilter::Favourites: if (! e.favourite) return false; break;
    }
    if (q.filterLayer && e.facets.layer != q.layer)
        return false;
    if (q.motionMask != 0 && (e.facets.motion & q.motionMask) == 0)
        return false;
    if (q.textureMask != 0 && (e.facets.texture & q.textureMask) == 0)
        return false;
    for (const auto& t : foldedTags)
    {
        const bool found = std::any_of (e.tags.begin(), e.tags.end(),
                                        [&t] (const std::string& tag) { return foldCase (tag) == t; });
        if (! found)
            return false;
    }
    for (const auto& term : terms)
        if (e.searchText.find (term) == std::string::npos)
            return false;
    return true;
}
} // namespace

void PresetBrowserModel::setQuery (const BrowserQuery& query)
{
    if (query == query_)
        return;
    query_ = query;
    ++queryVersion_;
}

void PresetBrowserModel::rebuild() const
{
    const auto terms = splitTerms (query_.text);
    std::vector<std::string> foldedTags;
    for (const auto& t : query_.tags)
        foldedTags.push_back (foldCase (t));

    results_.clear();
    const auto& entries = library_.entries();
    for (std::size_t i = 0; i < entries.size(); ++i)
        if (matches (entries[i], query_, terms, foldedTags))
            results_.push_back (static_cast<int> (i));

    auto nameKey = [&entries] (int i) { return foldCase (entries[static_cast<std::size_t> (i)].name); };
    switch (query_.sort)
    {
        case SortMode::Name:
            std::stable_sort (results_.begin(), results_.end(), [&] (int a, int b)
                              {
                                  const auto ka = nameKey (a), kb = nameKey (b);
                                  return ka != kb ? ka < kb : entries[static_cast<std::size_t> (a)].id < entries[static_cast<std::size_t> (b)].id;
                              });
            break;
        case SortMode::RecentlyUsed:
            std::stable_sort (results_.begin(), results_.end(), [&] (int a, int b)
                              {
                                  const auto& ea = entries[static_cast<std::size_t> (a)];
                                  const auto& eb = entries[static_cast<std::size_t> (b)];
                                  if (ea.lastUsed != eb.lastUsed)
                                      return ea.lastUsed > eb.lastUsed;
                                  const auto ka = nameKey (a), kb = nameKey (b);
                                  return ka != kb ? ka < kb : ea.id < eb.id;
                              });
            break;
        case SortMode::Category:
            std::stable_sort (results_.begin(), results_.end(), [&] (int a, int b)
                              {
                                  const auto& ea = entries[static_cast<std::size_t> (a)];
                                  const auto& eb = entries[static_cast<std::size_t> (b)];
                                  // first tag is the category by convention; untagged presets sort last
                                  const auto ca = ea.tags.empty() ? std::string ("\x7f") : foldCase (ea.tags.front());
                                  const auto cb = eb.tags.empty() ? std::string ("\x7f") : foldCase (eb.tags.front());
                                  if (ca != cb)
                                      return ca < cb;
                                  const auto ka = nameKey (a), kb = nameKey (b);
                                  return ka != kb ? ka < kb : ea.id < eb.id;
                              });
            break;
    }
    cachedRevision_ = library_.revision();
    cachedQueryVersion_ = queryVersion_;
}

const std::vector<int>& PresetBrowserModel::results() const
{
    if (cachedRevision_ != library_.revision() || cachedQueryVersion_ != queryVersion_)
        rebuild();
    return results_;
}

const PresetEntry& PresetBrowserModel::entryAt (int resultIndex) const
{
    return library_.entries()[static_cast<std::size_t> (results().at (static_cast<std::size_t> (resultIndex)))];
}

std::string PresetBrowserModel::emptyStateMessage() const
{
    if (! results().empty())
        return {};
    if (library_.entries().empty())
        return "No presets found. Check that the preset folder is readable.";
    if (query_.origin == OriginFilter::Favourites && query_.text.empty() && query_.tags.empty() && ! query_.filterLayer
        && query_.motionMask == 0 && query_.textureMask == 0)
        return "No favourites yet. Mark a preset with the star to find it here.";
    std::string msg = "No presets match";
    if (! query_.text.empty())
        msg += " \"" + query_.text + "\"";
    msg += ". Try fewer words or clear a filter.";
    return msg;
}

std::vector<std::pair<std::string, int>> PresetBrowserModel::availableTags() const
{
    std::map<std::string, std::pair<std::string, int>> counts; // folded -> (display, count)
    for (const auto& e : library_.entries())
        for (const auto& t : e.tags)
        {
            auto& slot = counts[foldCase (t)];
            if (slot.second == 0)
                slot.first = t;
            ++slot.second;
        }
    std::vector<std::pair<std::string, int>> out;
    for (auto& [key, value] : counts)
        out.push_back ({ value.first, value.second });
    std::stable_sort (out.begin(), out.end(), [] (const auto& a, const auto& b)
                      { return a.second != b.second ? a.second > b.second : foldCase (a.first) < foldCase (b.first); });
    return out;
}

bool PresetBrowserModel::select (const std::string& id)
{
    const auto& r = results();
    const auto& entries = library_.entries();
    for (const int i : r)
        if (entries[static_cast<std::size_t> (i)].id == id)
        {
            selectedId_ = id;
            return true;
        }
    return false;
}

int PresetBrowserModel::selectedResultIndex() const
{
    const auto& r = results();
    const auto& entries = library_.entries();
    for (std::size_t k = 0; k < r.size(); ++k)
        if (entries[static_cast<std::size_t> (r[k])].id == selectedId_)
            return static_cast<int> (k);
    return -1;
}

bool PresetBrowserModel::selectNext (bool wrap)
{
    const auto& r = results();
    if (r.empty())
        return false;
    const int cur = selectedResultIndex();
    int next = cur + 1;
    if (cur < 0)
        next = 0;
    else if (next >= static_cast<int> (r.size()))
    {
        if (! wrap)
            return false;
        next = 0;
    }
    if (next == cur)
        return false;
    selectedId_ = entryAt (next).id;
    return true;
}

bool PresetBrowserModel::selectPrevious (bool wrap)
{
    const auto& r = results();
    if (r.empty())
        return false;
    const int cur = selectedResultIndex();
    int prev = cur - 1;
    if (cur < 0)
        prev = static_cast<int> (r.size()) - 1;
    else if (prev < 0)
    {
        if (! wrap)
            return false;
        prev = static_cast<int> (r.size()) - 1;
    }
    if (prev == cur)
        return false;
    selectedId_ = entryAt (prev).id;
    return true;
}

void PresetBrowserModel::notifyLoaded (const std::string& id, const std::string& name, const ParamSnapshot& applied)
{
    loadedId_ = id;
    loadedName_ = name;
    reference_ = applied;
    modified_ = false;
}

void PresetBrowserModel::notifyEdited (const ParamSnapshot& current)
{
    modified_ = ! soundParamsEqual (current, reference_, 0.0f);
}

std::string PresetBrowserModel::displayName() const
{
    const std::string base = loadedName_.empty() ? std::string ("Init") : loadedName_;
    return modified_ ? base + "*" : base;
}

void PresetBrowserModel::beginAudition (const ParamSnapshot& current)
{
    if (audition_.has_value())
        return;
    AuditionRestore r;
    r.snapshot = current;
    r.modified = modified_;
    r.loadedId = loadedId_;
    r.loadedName = loadedName_;
    audition_ = std::move (r);
}

std::optional<AuditionRestore> PresetBrowserModel::cancelAudition()
{
    auto out = std::move (audition_);
    audition_.reset();
    auditionTarget_.clear();
    if (out.has_value())
    {
        // The pre-audition dirty state is restored; the reference (loaded preset) never changed.
        modified_ = out->modified;
    }
    return out;
}

bool PresetBrowserModel::confirmAudition (const std::string& id, const std::string& name, const ParamSnapshot& applied)
{
    if (! audition_.has_value())
        return false;
    audition_.reset();
    auditionTarget_.clear();
    notifyLoaded (id, name, applied);
    return true;
}
} // namespace snaral
