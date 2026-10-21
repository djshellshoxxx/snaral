#include "EditorPrefs.h"

#include "UI/UiOptions.h"

namespace snaral::ui
{
juce::File EditorPrefs::defaultFile()
{
    return SettingsFile::defaultFile().getSiblingFile ("editor-prefs.xml");
}

EditorPrefs::EditorPrefs (juce::File file) : file_ (std::move (file))
{
    if (file_.existsAsFile() && file_.getSize() < 64 * 1024)
        if (auto xml = juce::XmlDocument::parse (file_))
        {
            auto loaded = juce::ValueTree::fromXml (*xml);
            if (loaded.isValid() && loaded.hasType ("SnaralEditorPrefs"))
                tree_ = loaded;
        }
}

juce::String EditorPrefs::getString (const juce::String& key, const juce::String& fallback) const
{
    return tree_.hasProperty (key) ? tree_.getProperty (key).toString() : fallback;
}

int EditorPrefs::getInt (const juce::String& key, int fallback) const
{
    return tree_.hasProperty (key) ? static_cast<int> (tree_.getProperty (key)) : fallback;
}

double EditorPrefs::getDouble (const juce::String& key, double fallback) const
{
    return tree_.hasProperty (key) ? static_cast<double> (tree_.getProperty (key)) : fallback;
}

bool EditorPrefs::getBool (const juce::String& key, bool fallback) const
{
    return tree_.hasProperty (key) ? static_cast<bool> (tree_.getProperty (key)) : fallback;
}

void EditorPrefs::set (const juce::String& key, const juce::var& value)
{
    if (tree_.hasProperty (key) && tree_.getProperty (key) == value)
        return;
    tree_.setProperty (key, value, nullptr);
    flush();
}

bool EditorPrefs::isCollapsed (const juce::String& sectionKey) const
{
    return tree_.getChildWithProperty ("key", sectionKey).isValid();
}

void EditorPrefs::setCollapsed (const juce::String& sectionKey, bool collapsed)
{
    auto child = tree_.getChildWithProperty ("key", sectionKey);
    if (collapsed == child.isValid())
        return;
    if (collapsed)
    {
        juce::ValueTree c ("COLLAPSED");
        c.setProperty ("key", sectionKey, nullptr);
        tree_.appendChild (c, nullptr);
    }
    else
        tree_.removeChild (child, nullptr);
    flush();
}

bool EditorPrefs::flush()
{
    if (auto xml = tree_.createXml())
    {
        file_.getParentDirectory().createDirectory();
        return xml->writeTo (file_);
    }
    return false;
}
} // namespace snaral::ui
