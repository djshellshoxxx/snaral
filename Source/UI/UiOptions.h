#pragma once

#include "UiCommon.h"

namespace snaral::ui
{
/// User interface preferences (theme, tooltips, animation, UI scale, signal displays, onboarding state).
///
/// Persistence: the preferences live in a plain juce::ValueTree. The integration worker can
///  * store `toValueTree()` inside project state and hand it back to `setState()` on load, and/or
///  * use SettingsFile to keep a per-user file outside host/preset state (spec cdl-theme-options: theme, tooltips and
///    animation persist per installation and never enter preset/host sound state).
/// Nothing here touches audio parameters.
class OptionsModel
{
public:
    // Property identifiers of the ValueTree (tree type is "SnaralUi").
    struct Ids
    {
        static inline const juce::Identifier tree { "SnaralUi" };
        static inline const juce::Identifier version { "version" };
        static inline const juce::Identifier theme { "theme" };                      // string id, see ThemeCatalog
        static inline const juce::Identifier tooltips { "tooltips" };                // bool
        static inline const juce::Identifier animations { "animations" };            // bool (false = reduced motion)
        static inline const juce::Identifier uiScale { "uiScale" };                  // double 0.75 .. 2.0
        static inline const juce::Identifier showScope { "showScope" };              // bool
        static inline const juce::Identifier showCpu { "showCpu" };                  // bool
        static inline const juce::Identifier firstRunComplete { "firstRunComplete" };// bool, set when the tour was finished/skipped
        static inline const juce::Identifier tourPromptDisabled { "tourPromptDisabled" }; // bool, "do not offer the tour again"
    };

    static constexpr int currentVersion = 1;
    static inline const juce::String defaultThemeId { "cdl" };

    struct Listener
    {
        virtual ~Listener() = default;
        /// Called on the message thread after any property changed (also after setState()).
        virtual void optionChanged (const juce::Identifier& property) = 0;
    };

    OptionsModel();
    explicit OptionsModel (const juce::ValueTree& initial);
    ~OptionsModel();

    OptionsModel (const OptionsModel&) = delete;
    OptionsModel& operator= (const OptionsModel&) = delete;

    // ---- accessors --------------------------------------------------------------------------------------------
    juce::String getThemeId() const;
    bool areTooltipsEnabled() const;
    bool areAnimationsEnabled() const;
    float getUiScale() const;
    bool isScopeVisible() const;
    bool isCpuGaugeVisible() const;
    bool isFirstRunComplete() const;
    bool isTourPromptDisabled() const;
    /// True when the onboarding tour should be offered automatically (first run, not disabled).
    bool shouldOfferTour() const { return ! isFirstRunComplete() && ! isTourPromptDisabled(); }

    void setThemeId (const juce::String&);
    void setTooltipsEnabled (bool);
    void setAnimationsEnabled (bool);
    void setUiScale (float);                 ///< clamped to metrics::minUiScale .. maxUiScale, snapped to 0.05
    void setScopeVisible (bool);
    void setCpuGaugeVisible (bool);
    void setFirstRunComplete (bool);
    void setTourPromptDisabled (bool);

    // ---- persistence ------------------------------------------------------------------------------------------
    /// A detached copy suitable for storing in a parent tree / project state.
    juce::ValueTree toValueTree() const;
    /// Replaces all values from `tree` after validation. Unknown or ill-typed properties are ignored/defaulted, so a
    /// malformed or foreign tree never breaks the UI. Returns false if `tree` was not a SnaralUi tree.
    bool setState (const juce::ValueTree& tree);
    /// Resets everything (including first-run state) to defaults.
    void resetToDefaults();
    /// Live tree (for ValueTree::Listener users). Do not re-parent it; copy with toValueTree().
    juce::ValueTree& getLiveState() noexcept { return state; }

    void addListener (Listener*);
    void removeListener (Listener*);

    /// Fills in missing properties with defaults and coerces types/ranges. Public for tests.
    static void sanitise (juce::ValueTree& tree);

private:
    struct Impl;
    juce::ValueTree state;
    juce::ListenerList<Listener> listeners;
    std::unique_ptr<juce::ValueTree::Listener> treeListener;
};

/// Per-user settings file helper. The path is injectable so tests (and portable installs) do not touch the real
/// profile.
struct SettingsFile
{
    /// `<userApplicationData>/Circuit Drift Labs/Snaral/ui-settings.xml` (Application Support on macOS).
    static juce::File defaultFile();

    /// Writes atomically (temporary file + replace). Creates parent directories.
    static juce::Result save (const OptionsModel& options, const juce::File& file);

    /// Reads and validates. A missing file is not an error (returns Result::ok and leaves `options` untouched);
    /// a malformed file returns a failure and also leaves `options` untouched.
    static juce::Result load (OptionsModel& options, const juce::File& file);
};
} // namespace snaral::ui
