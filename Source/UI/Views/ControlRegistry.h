#pragma once

// Keeps track of which component is the primary control for each registry parameter, plus secondary duplicates (the
// performance row binds the same parameter again). The UI-to-registry test walks parameterTable against this.

#include "UI/UiCommon.h"

#include <vector>

namespace snaral::ui
{
class ControlRegistry
{
public:
    struct Entry
    {
        juce::String id;
        juce::Component::SafePointer<juce::Component> component;
        bool primary = true;
    };

    void add (const juce::String& parameterId, juce::Component* c, bool primary)
    {
        if (c != nullptr)
            entries.push_back ({ parameterId, c, primary });
    }

    int primaryCount (const juce::String& parameterId) const
    {
        int n = 0;
        for (const auto& e : entries)
            n += (e.component != nullptr && e.primary && e.id == parameterId) ? 1 : 0;
        return n;
    }

    int totalCount (const juce::String& parameterId) const
    {
        int n = 0;
        for (const auto& e : entries)
            n += (e.component != nullptr && e.id == parameterId) ? 1 : 0;
        return n;
    }

    juce::Component* primary (const juce::String& parameterId) const
    {
        for (const auto& e : entries)
            if (e.component != nullptr && e.primary && e.id == parameterId)
                return e.component.getComponent();
        return nullptr;
    }

    const std::vector<Entry>& all() const noexcept { return entries; }

private:
    std::vector<Entry> entries;
};
} // namespace snaral::ui
