#pragma once

#include "MacroKnob.h"
#include "ParamControls.h"
#include "SectionPanel.h"

namespace snaral::ui
{
/// The Advanced workspace tabs (ui-layout.md, plus the Effects tab for Formant Furnace / Harmonic Fracture).
enum class AdvancedTab { Voice, Sub, Sources, FilterMotion, Drive, Texture, Effects, Output };
inline constexpr int numAdvancedTabs = 8;
const char* advancedTabName (AdvancedTab);

/// One collapsible section of a tab and the parameter ids it shows (in order).
struct AdvancedSectionSpec
{
    AdvancedTab tab;
    const char* title;
    std::vector<const char*> parameterIds;
};
const std::vector<AdvancedSectionSpec>& advancedSections();

/// Parameter groups (ParamDef::group) that belong to a tab. Parameters of those groups that no section lists are
/// appended to a "More" section automatically, so adding a parameter to the table can never hide it from the UI.
std::vector<juce::String> advancedGroupsForTab (AdvancedTab);

/// True for parameters that are edited by a dedicated widget instead of a generic control (trajectory points).
bool isEditedByDedicatedWidget (const juce::String& parameterId);

/// Parameters that appear on the Main view (macros) or in the common action row (utility); not in Advanced tabs.
bool isMainViewParameter (const juce::String& parameterId);

//==================================================================================================================
/// Wrapping flow of fixed-size controls. Item sizes are taken from the child components themselves, so the
/// controls never stretch when the window grows; extra width just fits more per row.
class ParamGrid : public juce::Component,
                  public PreferredHeightProvider
{
public:
    ParamGrid() = default;
    void addItem (juce::Component& c);                       ///< not owned
    void addOwned (std::unique_ptr<juce::Component> c);      ///< owned
    int getPreferredHeight (int width) const override;
    void resized() override;
    int spacing = 12;

private:
    int layout (int width, bool apply);
    std::vector<juce::Component*> items;
    std::vector<std::unique_ptr<juce::Component>> owned;
};

//==================================================================================================================
/// The scrollable body of one Advanced tab: a vertical stack of SectionPanels, each holding a ParamGrid of controls
/// auto-built from the parameter table (knob for continuous, combo for choices, toggle for bool).
class AdvancedTabContent : public juce::Component,
                           public PreferredHeightProvider
{
public:
    /// Called for every generated control so the editor can wire MIDI learn / feedback hooks and indicators.
    using ControlVisitor = std::function<void (const juce::String& parameterId, ContextHooks& hooks, juce::Component& control)>;

    AdvancedTabContent (AdvancedTab tab, juce::AudioProcessorValueTreeState&, ControlVisitor visitor = {});
    ~AdvancedTabContent() override;

    AdvancedTab getTab() const noexcept { return tab; }

    /// Adds a custom section (e.g. the TrajectoryEditor). `atIndex` < 0 appends. The component is owned.
    SectionPanel& addCustomSection (const juce::String& title, std::unique_ptr<juce::Component> content, int atIndex = -1);

    int getNumSections() const noexcept { return static_cast<int> (sections.size()); }
    SectionPanel& getSection (int index) { return *sections[static_cast<std::size_t> (index)]; }
    SectionPanel* findSection (const juce::String& title);
    /// Control generated for a parameter id (MacroKnob, ParamCombo or ParamToggle), or nullptr.
    juce::Component* findControl (const juce::String& parameterId) const;
    std::vector<juce::String> getParameterIds() const;

    /// Fired when a section is collapsed/expanded (the scroll container re-measures).
    std::function<void()> onPreferredHeightChanged;

    int getPreferredHeight (int width) const override;
    void resized() override;

private:
    struct Entry
    {
        juce::String id;
        juce::Component* control;
    };

    void relayout();

    AdvancedTab tab;
    std::vector<std::unique_ptr<SectionPanel>> sections;
    std::vector<std::unique_ptr<juce::Component>> sectionBodies;
    std::vector<Entry> controls;
    std::vector<std::unique_ptr<juce::Component>> customBodies;
    static constexpr int gap = 10;
};

//==================================================================================================================
/// Tab strip + one vertical-scrolling viewport per tab. Scroll positions are remembered per tab for the session;
/// keyboard focus moving to a control outside the visible area scrolls it into view.
class TabbedAdvanced : public juce::Component
{
public:
    TabbedAdvanced();
    ~TabbedAdvanced() override;

    static constexpr int tabBarHeight = 36;

    /// Takes ownership of `content` and shows it in the tab's scroll viewport.
    void setTabContent (AdvancedTab, std::unique_ptr<juce::Component> content);
    juce::Component* getTabContent (AdvancedTab) const;

    /// Convenience: builds all eight tabs from the parameter table.
    void buildDefaultTabs (juce::AudioProcessorValueTreeState&, AdvancedTabContent::ControlVisitor visitor = {});

    void setCurrentTab (AdvancedTab, bool notify = true);
    AdvancedTab getCurrentTab() const noexcept { return current; }

    juce::Viewport& getViewport (AdvancedTab t) noexcept { return *viewports[static_cast<std::size_t> (t)]; }
    int getScrollPosition (AdvancedTab) const;
    void setScrollPosition (AdvancedTab, int y);
    void scrollToBottom (AdvancedTab);
    juce::Button& getTabButton (AdvancedTab t) noexcept;

    /// Re-measures the content of every tab (call after the width changed or sections toggled).
    void refreshLayout();

    std::function<void (AdvancedTab)> onTabChanged;

    void resized() override;
    void paint (juce::Graphics&) override;
    void focusOfChildComponentChanged (FocusChangeType) override;

private:
    class TabButton;
    void layoutContent (AdvancedTab);
    void ensureFocusedChildVisible();

    std::array<std::unique_ptr<TabButton>, numAdvancedTabs> tabButtons;
    std::array<std::unique_ptr<juce::Viewport>, numAdvancedTabs> viewports;
    std::array<std::unique_ptr<juce::Component>, numAdvancedTabs> contents;
    AdvancedTab current = AdvancedTab::Voice;
};
} // namespace snaral::ui
