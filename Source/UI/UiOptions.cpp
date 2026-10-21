#include "UiOptions.h"

namespace snaral::ui
{
namespace
{
using Ids = OptionsModel::Ids;

bool asBool (const juce::var& v, bool fallback)
{
    if (v.isBool())   return static_cast<bool> (v);
    if (v.isInt() || v.isInt64() || v.isDouble()) return static_cast<double> (v) != 0.0;
    if (v.isString())
    {
        const auto s = v.toString().trim().toLowerCase();
        if (s == "1" || s == "true" || s == "yes" || s == "on")  return true;
        if (s == "0" || s == "false" || s == "no" || s == "off") return false;
    }
    return fallback;
}

float snapScale (double v)
{
    if (! std::isfinite (v))
        v = 1.0;
    v = std::round (v * 20.0) / 20.0;
    return static_cast<float> (juce::jlimit (static_cast<double> (metrics::minUiScale),
                                             static_cast<double> (metrics::maxUiScale), v));
}
} // namespace

struct TreeForwarder final : public juce::ValueTree::Listener
{
    explicit TreeForwarder (juce::ListenerList<OptionsModel::Listener>& l) : listeners (l) {}

    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier& property) override
    {
        listeners.call ([&] (OptionsModel::Listener& l) { l.optionChanged (property); });
    }

    juce::ListenerList<OptionsModel::Listener>& listeners;
};

void OptionsModel::sanitise (juce::ValueTree& t)
{
    t.setProperty (Ids::version, currentVersion, nullptr);

    auto theme = t.getProperty (Ids::theme, defaultThemeId).toString().trim().toLowerCase();
    if (theme.isEmpty() || theme.length() > 32 || ! theme.containsOnly ("abcdefghijklmnopqrstuvwxyz0123456789-_"))
        theme = defaultThemeId;
    t.setProperty (Ids::theme, theme, nullptr);

    t.setProperty (Ids::tooltips,          asBool (t.getProperty (Ids::tooltips), true), nullptr);
    t.setProperty (Ids::animations,        asBool (t.getProperty (Ids::animations), true), nullptr);
    t.setProperty (Ids::showScope,         asBool (t.getProperty (Ids::showScope), true), nullptr);
    t.setProperty (Ids::showCpu,           asBool (t.getProperty (Ids::showCpu), true), nullptr);
    t.setProperty (Ids::firstRunComplete,  asBool (t.getProperty (Ids::firstRunComplete), false), nullptr);
    t.setProperty (Ids::tourPromptDisabled, asBool (t.getProperty (Ids::tourPromptDisabled), false), nullptr);

    const auto scaleVar = t.getProperty (Ids::uiScale, 1.0);
    t.setProperty (Ids::uiScale, static_cast<double> (snapScale (scaleVar.isVoid() ? 1.0 : static_cast<double> (scaleVar))), nullptr);
}

OptionsModel::OptionsModel() : OptionsModel (juce::ValueTree {})
{
}

OptionsModel::OptionsModel (const juce::ValueTree& initial)
    : state (Ids::tree)
{
    if (initial.isValid() && initial.hasType (Ids::tree))
        state.copyPropertiesFrom (initial, nullptr);
    sanitise (state);
    treeListener = std::make_unique<TreeForwarder> (listeners);
    state.addListener (treeListener.get());
}

OptionsModel::~OptionsModel()
{
    state.removeListener (treeListener.get());
}

juce::String OptionsModel::getThemeId() const          { return state.getProperty (Ids::theme, defaultThemeId).toString(); }
bool OptionsModel::areTooltipsEnabled() const          { return asBool (state.getProperty (Ids::tooltips), true); }
bool OptionsModel::areAnimationsEnabled() const        { return asBool (state.getProperty (Ids::animations), true); }
float OptionsModel::getUiScale() const                 { return snapScale (static_cast<double> (state.getProperty (Ids::uiScale, 1.0))); }
bool OptionsModel::isScopeVisible() const              { return asBool (state.getProperty (Ids::showScope), true); }
bool OptionsModel::isCpuGaugeVisible() const           { return asBool (state.getProperty (Ids::showCpu), true); }
bool OptionsModel::isFirstRunComplete() const          { return asBool (state.getProperty (Ids::firstRunComplete), false); }
bool OptionsModel::isTourPromptDisabled() const        { return asBool (state.getProperty (Ids::tourPromptDisabled), false); }

void OptionsModel::setThemeId (const juce::String& id)
{
    auto copy = juce::ValueTree (Ids::tree);
    copy.setProperty (Ids::theme, id.trim().toLowerCase(), nullptr);
    sanitise (copy);
    state.setProperty (Ids::theme, copy.getProperty (Ids::theme), nullptr);
}

void OptionsModel::setTooltipsEnabled (bool b)         { state.setProperty (Ids::tooltips, b, nullptr); }
void OptionsModel::setAnimationsEnabled (bool b)       { state.setProperty (Ids::animations, b, nullptr); }
void OptionsModel::setUiScale (float s)                { state.setProperty (Ids::uiScale, static_cast<double> (snapScale (s)), nullptr); }
void OptionsModel::setScopeVisible (bool b)            { state.setProperty (Ids::showScope, b, nullptr); }
void OptionsModel::setCpuGaugeVisible (bool b)         { state.setProperty (Ids::showCpu, b, nullptr); }
void OptionsModel::setFirstRunComplete (bool b)        { state.setProperty (Ids::firstRunComplete, b, nullptr); }
void OptionsModel::setTourPromptDisabled (bool b)      { state.setProperty (Ids::tourPromptDisabled, b, nullptr); }

juce::ValueTree OptionsModel::toValueTree() const
{
    auto copy = state.createCopy();
    sanitise (copy);
    return copy;
}

bool OptionsModel::setState (const juce::ValueTree& tree)
{
    if (! tree.isValid() || ! tree.hasType (Ids::tree))
        return false;

    auto incoming = tree.createCopy();
    sanitise (incoming);
    for (const auto& id : { Ids::version, Ids::theme, Ids::tooltips, Ids::animations, Ids::uiScale,
                            Ids::showScope, Ids::showCpu, Ids::firstRunComplete, Ids::tourPromptDisabled })
        state.setProperty (id, incoming.getProperty (id), nullptr);
    return true;
}

void OptionsModel::resetToDefaults()
{
    juce::ValueTree defaults (Ids::tree);
    sanitise (defaults);
    setState (defaults);
}

void OptionsModel::addListener (Listener* l)     { listeners.add (l); }
void OptionsModel::removeListener (Listener* l)  { listeners.remove (l); }

// ---------------------------------------------------------------------------------------------------------------
juce::File SettingsFile::defaultFile()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
   #if JUCE_MAC
    dir = dir.getChildFile ("Application Support");
   #endif
    return dir.getChildFile ("Circuit Drift Labs").getChildFile ("Snaral").getChildFile ("ui-settings.xml");
}

juce::Result SettingsFile::save (const OptionsModel& options, const juce::File& file)
{
    const auto parent = file.getParentDirectory();
    if (! parent.exists())
    {
        const auto made = parent.createDirectory();
        if (made.failed())
            return juce::Result::fail ("Could not create settings folder: " + made.getErrorMessage());
    }

    const auto xml = options.toValueTree().createXml();
    if (xml == nullptr)
        return juce::Result::fail ("Could not serialise UI settings");

    juce::TemporaryFile temp (file);
    if (! xml->writeTo (temp.getFile()))
        return juce::Result::fail ("Could not write " + temp.getFile().getFullPathName());
    if (! temp.overwriteTargetFileWithTemporary())
        return juce::Result::fail ("Could not replace " + file.getFullPathName());
    return juce::Result::ok();
}

juce::Result SettingsFile::load (OptionsModel& options, const juce::File& file)
{
    if (! file.existsAsFile())
        return juce::Result::ok();
    if (file.getSize() > 64 * 1024)
        return juce::Result::fail ("Settings file is unexpectedly large; ignored");

    const auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr || ! xml->hasTagName (OptionsModel::Ids::tree.toString()))
        return juce::Result::fail ("Settings file is not a valid Snaral UI settings file; ignored");

    const auto tree = juce::ValueTree::fromXml (*xml);
    if (! options.setState (tree))
        return juce::Result::fail ("Settings file content was rejected");
    return juce::Result::ok();
}
} // namespace snaral::ui
