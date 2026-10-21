#pragma once

#include "HelpContent.h"
#include "Theme.h"

namespace snaral::ui
{
/// Local, offline, searchable help: topic list on the left (filtered by the search box), the selected page on the right,
/// and a footer with the Circuit Drift Labs link and "Run introduction again". The website is opened only when the user
/// activates the link, and through `callbacks.openUrl` when supplied (tests inject a recorder).
///
/// Place it in a dialog, a sheet or full-window overlay of at least ~480 x 320 logical px.
class HelpPanel : public juce::Component
{
public:
    struct Callbacks
    {
        std::function<void (const juce::String& url)> openUrl;   ///< default: URL::launchInDefaultBrowser
        std::function<void()> rerunTour;                         ///< hides the button when empty
        std::function<void()> closePanel;                        ///< shows a Close button when set
    };

    HelpPanel();
    ~HelpPanel() override;

    Callbacks callbacks;

    /// Selects a topic by id (see HelpContent). Clears a search that would hide it. Returns false for unknown ids.
    bool showTopic (const juce::String& id);
    /// Context help: shows the page that documents a parameter.
    bool showTopicForParameter (const juce::String& parameterId);
    juce::String getCurrentTopicId() const;

    void setSearchText (const juce::String&);
    juce::String getSearchText() const;
    int getNumVisibleTopics() const noexcept { return static_cast<int> (visible.size()); }
    juce::String getVisibleTopicId (int row) const;
    /// Text currently shown in the page area (for tests and accessibility checks).
    juce::String getDisplayedText() const;

    void openWebsite();

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    class Model;
    void rebuildList (const juce::String& keepTopicId);
    void showRow (int row);

    juce::TextEditor search;
    juce::ListBox list;
    juce::TextEditor page;
    juce::TextButton websiteButton { "Open Circuit Drift Labs website" };
    juce::TextButton tourButton { "Run introduction again" };
    juce::TextButton closeButton { "Close" };
    std::unique_ptr<Model> model;
    std::vector<int> visible;      // indices into HelpContent::topics()
    int currentTopic = -1;
};
} // namespace snaral::ui
