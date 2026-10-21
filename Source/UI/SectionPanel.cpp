#include "SectionPanel.h"

namespace snaral::ui
{
class SectionPanel::HeaderButton : public juce::Button
{
public:
    HeaderButton() : juce::Button ({}) {}

    void paintButton (juce::Graphics& g, bool highlighted, bool) override
    {
        const auto& t = SnaralLookAndFeel::tokensFor (*this);
        const auto r = getLocalBounds().toFloat();
        if (highlighted)
        {
            g.setColour (t.panelRaised);
            g.fillRoundedRectangle (r.reduced (2.0f, 2.0f), 4.0f);
        }

        // chevron: points right when collapsed, down when expanded
        const auto c = juce::Point<float> (18.0f, r.getCentreY());
        juce::Path chevron;
        if (getToggleState())
        {
            chevron.startNewSubPath (c.x - 4.0f, c.y - 2.0f);
            chevron.lineTo (c.x, c.y + 3.0f);
            chevron.lineTo (c.x + 4.0f, c.y - 2.0f);
        }
        else
        {
            chevron.startNewSubPath (c.x - 2.0f, c.y - 4.0f);
            chevron.lineTo (c.x + 3.0f, c.y);
            chevron.lineTo (c.x - 2.0f, c.y + 4.0f);
        }
        g.setColour (t.accentText);
        g.strokePath (chevron, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        g.setFont (fonts::heading());
        g.setColour (t.textPrimary);
        g.drawFittedText (getButtonText(), getLocalBounds().withTrimmedLeft (34).withTrimmedRight (8),
                          juce::Justification::centredLeft, 1, 0.9f);

        if (hasKeyboardFocus (true))
            SnaralLookAndFeel::drawFocusRing (g, t, r.reduced (2.0f), 4.0f);
    }
};

SectionPanel::SectionPanel (const juce::String& title, juce::Component* c)
    : sectionTitle (title), header (std::make_unique<HeaderButton>())
{
    header->setButtonText (title);
    header->setClickingTogglesState (true);
    header->setToggleable (true);
    header->setToggleState (true, juce::dontSendNotification);
    header->setWantsKeyboardFocus (true);
    header->setMouseCursor (juce::MouseCursor::PointingHandCursor);
    header->onClick = [this] { setExpanded (header->getToggleState(), true); };
    addAndMakeVisible (*header);
    setContent (c);
    updateAccessibleText();
}

SectionPanel::~SectionPanel() = default;

juce::Button& SectionPanel::getHeaderButton() noexcept { return *header; }

void SectionPanel::setTitle (const juce::String& t)
{
    sectionTitle = t;
    header->setButtonText (t);
    updateAccessibleText();
    repaint();
}

void SectionPanel::updateAccessibleText()
{
    header->setTitle (sectionTitle + (expanded ? ", expanded" : ", collapsed"));
    header->setDescription ("Section " + sectionTitle + " is " + juce::String (expanded ? "expanded" : "collapsed")
                            + ". Press Space to " + (expanded ? "collapse" : "expand") + " it.");
    header->setTooltip ((expanded ? "Collapse " : "Expand ") + sectionTitle);
}

void SectionPanel::setContent (juce::Component* c)
{
    if (content != nullptr)
        removeChildComponent (content);
    content = c;
    if (content != nullptr)
    {
        addChildComponent (*content);
        content->setVisible (expanded);
    }
    resized();
}

void SectionPanel::setExpanded (bool e, bool notify)
{
    if (expanded == e && header->getToggleState() == e)
        return;
    expanded = e;
    header->setToggleState (e, juce::dontSendNotification);
    if (content != nullptr)
        content->setVisible (e);
    updateAccessibleText();
    resized();
    repaint();
    if (notify && onLayoutChanged)
        onLayoutChanged();
}

int SectionPanel::getPreferredHeight (int width) const
{
    int h = headerHeight;
    if (expanded && content != nullptr)
    {
        int ch = content->getHeight();
        if (auto* p = dynamic_cast<PreferredHeightProvider*> (content))
            ch = p->getPreferredHeight (juce::jmax (1, width - 2 * padding));
        h += ch + padding;
    }
    return h;
}

void SectionPanel::resized()
{
    auto r = getLocalBounds();
    header->setBounds (r.removeFromTop (headerHeight));
    if (content != nullptr && expanded)
    {
        r.removeFromBottom (padding);
        content->setBounds (r.reduced (padding, 0));
    }
}

void SectionPanel::paint (juce::Graphics& g)
{
    const auto& t = SnaralLookAndFeel::tokensFor (*this);
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (t.panel);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (t.border);
    g.drawRoundedRectangle (r, 6.0f, t.borderThickness);
    if (expanded && content != nullptr)
    {
        g.setColour (t.border);
        g.drawHorizontalLine (headerHeight - 1, 10.0f, static_cast<float> (getWidth() - 10));
    }
}
} // namespace snaral::ui
