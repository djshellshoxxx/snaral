#pragma once

// Small per-user editor preferences that are neither presets nor host state (docs/specs/cdl-common-actions.md:
// randomize locks; macros-ui.md: collapsed groups; cdl-offline-wav-export.md: last export folder and settings).
// Stored next to ui-settings.xml. Every read tolerates a missing or damaged file.

#include "UI/UiCommon.h"

namespace snaral::ui
{
class EditorPrefs
{
public:
    explicit EditorPrefs (juce::File file = defaultFile());

    static juce::File defaultFile();
    const juce::File& file() const noexcept { return file_; }

    juce::String getString (const juce::String& key, const juce::String& fallback = {}) const;
    int getInt (const juce::String& key, int fallback) const;
    double getDouble (const juce::String& key, double fallback) const;
    bool getBool (const juce::String& key, bool fallback) const;
    void set (const juce::String& key, const juce::var& value);

    bool isCollapsed (const juce::String& sectionKey) const;
    void setCollapsed (const juce::String& sectionKey, bool collapsed);

    /// Writes the file now. Done after every change (the file is a few hundred bytes and changes rarely).
    bool flush();

private:
    juce::File file_;
    juce::ValueTree tree_ { "SnaralEditorPrefs" };
};
} // namespace snaral::ui
