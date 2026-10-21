#include "HelpPanel.h"

namespace snaral::ui
{
class HelpPanel::Model : public juce::ListBoxModel
{
public:
    explicit Model (HelpPanel& o) : owner (o) {}

    int getNumRows() override { return owner.getNumVisibleTopics(); }

    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) override
    {
        if (! juce::isPositiveAndBelow (row, owner.getNumVisibleTopics()))
            return;
        const auto& t = SnaralLookAndFeel::tokensFor (owner);
        const auto& topic = HelpContent::topics()[static_cast<std::size_t> (owner.visible[static_cast<std::size_t> (row)])];
        if (selected)
        {
            g.setColour (t.accentTint (t.panel));
            g.fillRect (0, 0, w, h);
            g.setColour (t.accent);
            g.fillRect (0, 0, 3, h);                          // selection bar: shape cue in addition to the tint
        }
        auto r = juce::Rectangle<int> (0, 0, w, h).reduced (10, 3);
        g.setFont (selected ? fonts::heading().withHeight (12.5f) : fonts::body().withHeight (12.5f));
        g.setColour (selected ? t.textPrimary : t.textPrimary);
        g.drawFittedText (topic.title, r.removeFromTop (h / 2 + 1), juce::Justification::bottomLeft, 1, 0.85f);
        g.setFont (fonts::small());
        g.setColour (t.textMuted);
        g.drawFittedText (topic.category, r, juce::Justification::topLeft, 1, 0.85f);
    }

    void selectedRowsChanged (int row) override
    {
        if (row >= 0)
            owner.showRow (row);
    }

    juce::String getNameForRow (int row) override
    {
        return juce::isPositiveAndBelow (row, owner.getNumVisibleTopics())
                   ? HelpContent::topics()[static_cast<std::size_t> (owner.visible[static_cast<std::size_t> (row)])].title
                   : juce::String();
    }

private:
    HelpPanel& owner;
};

HelpPanel::HelpPanel()
{
    model = std::make_unique<Model> (*this);

    search.setMultiLine (false);
    search.setTextToShowWhenEmpty ("Search help (for example: cutoff, MIDI learn, tempo)", juce::Colours::grey);
    search.setTitle ("Search help");
    search.setFont (fonts::body());
    search.setIndents (8, 7);
    search.onTextChange = [this] { rebuildList (getCurrentTopicId()); };
    search.onEscapeKey = [this] { if (search.getText().isNotEmpty()) search.clear(); else if (callbacks.closePanel) callbacks.closePanel(); };
    addAndMakeVisible (search);

    list.setModel (model.get());
    list.setRowHeight (42);
    list.setTitle ("Help topics");
    list.setWantsKeyboardFocus (true);
    addAndMakeVisible (list);

    page.setMultiLine (true, true);
    page.setReadOnly (true);
    page.setCaretVisible (false);
    page.setScrollbarsShown (true);
    page.setPopupMenuEnabled (true);
    page.setTitle ("Help page");
    page.setWantsKeyboardFocus (true);
    page.setIndents (14, 10);
    addAndMakeVisible (page);

    websiteButton.setTooltip ("Opens the Circuit Drift Labs website in your browser");
    websiteButton.onClick = [this] { openWebsite(); };
    addAndMakeVisible (websiteButton);

    tourButton.onClick = [this] { if (callbacks.rerunTour) callbacks.rerunTour(); };
    addChildComponent (tourButton);

    closeButton.onClick = [this] { if (callbacks.closePanel) callbacks.closePanel(); };
    addChildComponent (closeButton);

    setTitle ("Snaral help");
    setWantsKeyboardFocus (false);
    setFocusContainerType (FocusContainerType::focusContainer);
    setSize (680, 440);
    rebuildList ("quick-start");
}

HelpPanel::~HelpPanel()
{
    list.setModel (nullptr);
}

void HelpPanel::openWebsite()
{
    const juce::String url (HelpContent::cdlWebsite);
    if (callbacks.openUrl)
        callbacks.openUrl (url);
    else
        juce::URL (url).launchInDefaultBrowser();
}

juce::String HelpPanel::getCurrentTopicId() const
{
    return currentTopic >= 0 ? HelpContent::topics()[static_cast<std::size_t> (currentTopic)].id : juce::String();
}

juce::String HelpPanel::getVisibleTopicId (int row) const
{
    return juce::isPositiveAndBelow (row, getNumVisibleTopics())
               ? HelpContent::topics()[static_cast<std::size_t> (visible[static_cast<std::size_t> (row)])].id
               : juce::String();
}

juce::String HelpPanel::getDisplayedText() const { return page.getText(); }
juce::String HelpPanel::getSearchText() const    { return search.getText(); }

void HelpPanel::setSearchText (const juce::String& s)
{
    search.setText (s, juce::dontSendNotification);      // TextEditor change callbacks are asynchronous
    rebuildList (getCurrentTopicId());
}

void HelpPanel::rebuildList (const juce::String& keepId)
{
    visible = HelpContent::search (search.getText());
    list.updateContent();

    int row = -1;
    for (int i = 0; i < static_cast<int> (visible.size()); ++i)
        if (HelpContent::topics()[static_cast<std::size_t> (visible[static_cast<std::size_t> (i)])].id == keepId)
            row = i;
    if (row < 0 && ! visible.empty())
        row = 0;

    if (row >= 0)
    {
        list.selectRow (row, false, true);
        showRow (row);
    }
    else
    {
        currentTopic = -1;
        page.clear();
    }
    repaint();
}

void HelpPanel::showRow (int row)
{
    if (! juce::isPositiveAndBelow (row, getNumVisibleTopics()))
        return;
    currentTopic = visible[static_cast<std::size_t> (row)];
    const auto& topic = HelpContent::topics()[static_cast<std::size_t> (currentTopic)];

    page.clear();
    page.setFont (fonts::title());
    page.insertTextAtCaret (topic.title + "\n");
    page.setFont (fonts::small());
    page.insertTextAtCaret (topic.category.toUpperCase() + "\n\n");
    page.setFont (fonts::body());
    page.insertTextAtCaret (topic.body);
    page.setCaretPosition (0);
    page.scrollEditorToPositionCaret (0, 0);
    list.scrollToEnsureRowIsOnscreen (row);
}

bool HelpPanel::showTopic (const juce::String& id)
{
    if (HelpContent::findTopic (id) == nullptr)
        return false;
    if (search.getText().isNotEmpty())
        search.setText ({}, juce::dontSendNotification);
    rebuildList (id);
    return getCurrentTopicId() == id;
}

bool HelpPanel::showTopicForParameter (const juce::String& parameterId)
{
    const auto id = HelpContent::topicIdForParameter (parameterId);
    return id.isNotEmpty() && showTopic (id);
}

void HelpPanel::resized()
{
    auto r = getLocalBounds().reduced (12, 10);
    search.setBounds (r.removeFromTop (30));
    r.removeFromTop (8);

    auto footer = r.removeFromBottom (metrics::buttonHeight);
    r.removeFromBottom (8);
    websiteButton.setBounds (footer.removeFromLeft (230));
    footer.removeFromLeft (8);
    tourButton.setVisible (static_cast<bool> (callbacks.rerunTour));
    tourButton.setBounds (footer.removeFromLeft (170));
    closeButton.setVisible (static_cast<bool> (callbacks.closePanel));
    closeButton.setBounds (footer.removeFromRight (80));

    const int listW = juce::jlimit (150, 230, r.getWidth() / 3);
    list.setBounds (r.removeFromLeft (listW));
    r.removeFromLeft (8);
    page.setBounds (r);
}

void HelpPanel::paint (juce::Graphics& g)
{
    const auto& t = SnaralLookAndFeel::tokensFor (*this);
    g.fillAll (t.panel);
    if (visible.empty())
    {
        g.setFont (fonts::body());
        g.setColour (t.textMuted);
        g.drawFittedText ("No help topics match your search.", getLocalBounds().reduced (24), juce::Justification::centred, 2, 1.0f);
    }
}

bool HelpPanel::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey && callbacks.closePanel)
    {
        callbacks.closePanel();
        return true;
    }
    return false;
}
} // namespace snaral::ui
