#include "InlineDialog.h"

namespace snaral::ui
{
namespace
{
constexpr int pad = 18;
constexpr int titleH = 26;
constexpr int fieldLabelH = 15;
constexpr int fieldH = 30;
constexpr int multiFieldH = 70;
constexpr int buttonH = 32;
} // namespace

InlineDialog::InlineDialog (DialogSpec s, std::function<void()> onClosed)
    : spec (std::move (s)), closed (std::move (onClosed))
{
    setWantsKeyboardFocus (true);
    setFocusContainerType (FocusContainerType::keyboardFocusContainer);
    setInterceptsMouseClicks (true, true);
    setTitle (spec.title);
    setDescription (spec.message);

    messageLabel.setText (spec.message, juce::dontSendNotification);
    messageLabel.setJustificationType (juce::Justification::topLeft);
    messageLabel.setMinimumHorizontalScale (1.0f);
    addAndMakeVisible (messageLabel);

    for (const auto& f : spec.fields)
    {
        auto label = std::make_unique<juce::Label> ("", f.label);
        label->setFont (fonts::label());
        addAndMakeVisible (*label);
        fieldLabels.push_back (std::move (label));

        auto ed = std::make_unique<juce::TextEditor>();
        ed->setMultiLine (f.multiline, true);
        ed->setReturnKeyStartsNewLine (f.multiline);
        ed->setText (f.text, false);
        ed->setTitle (f.label);
        ed->setWantsKeyboardFocus (true);
        ed->setFont (fonts::body());
        ed->onEscapeKey = [this] { finish (static_cast<int> (spec.buttons.size()) - 1); };
        if (! f.multiline)
            ed->onReturnKey = [this] { finish (0); };
        addAndMakeVisible (*ed);
        editors.push_back (std::move (ed));
    }

    int index = 0;
    for (const auto& b : spec.buttons)
    {
        auto btn = std::make_unique<juce::TextButton> (b);
        btn->setTitle (b);
        btn->setWantsKeyboardFocus (true);
        const int i = index++;
        btn->onClick = [this, i] { finish (i); };
        addAndMakeVisible (*btn);
        buttons.push_back (std::move (btn));
    }
}

InlineDialog::~InlineDialog() = default;

void InlineDialog::parentHierarchyChanged()
{
    if (getParentComponent() != nullptr && ! finished)
    {
        juce::Component::SafePointer<InlineDialog> safe (this);
        juce::MessageManager::callAsync ([safe]
        {
            if (safe == nullptr)
                return;
            if (! safe->editors.empty())
                safe->editors.front()->grabKeyboardFocus();
            else
                safe->grabKeyboardFocus();
        });
    }
}

void InlineDialog::finish (int button)
{
    if (finished)
        return;
    finished = true;
    juce::StringArray values;
    for (auto& e : editors)
        values.add (e->getText());
    auto cb = std::move (spec.onResult);
    auto close = std::move (closed);
    juce::Component::SafePointer<InlineDialog> self (this);
    if (close)
        close(); // removes and destroys this dialog (deferred by the owner)
    if (cb)
        cb (button, values);
    juce::ignoreUnused (self);
}

int InlineDialog::cardHeight (int cardWidth) const
{
    juce::AttributedString as;
    as.append (spec.message, fonts::body());
    juce::TextLayout tl;
    tl.createLayout (as, static_cast<float> (cardWidth - 2 * pad));
    int h = pad + titleH + 6 + static_cast<int> (std::ceil (tl.getHeight())) + 10;
    for (const auto& f : spec.fields)
        h += fieldLabelH + (f.multiline ? multiFieldH : fieldH) + 8;
    h += buttonH + pad;
    return h;
}

juce::Rectangle<int> InlineDialog::cardBounds() const
{
    const int w = juce::jmin (460, getWidth() - 24);
    const int h = juce::jmin (getHeight() - 16, cardHeight (w));
    return juce::Rectangle<int> (w, h).withCentre (getLocalBounds().getCentre());
}

void InlineDialog::paint (juce::Graphics& g)
{
    const auto& t = SnaralLookAndFeel::tokensFor (*this);
    g.fillAll (juce::Colours::black.withAlpha (0.55f));
    const auto card = cardBounds().toFloat();
    SnaralLookAndFeel::drawPanelShadow (g, t, card, 6.0f);
    g.setColour (t.panel);
    g.fillRoundedRectangle (card, 6.0f);
    g.setColour (t.outline);
    g.drawRoundedRectangle (card.reduced (0.5f), 6.0f, t.borderThickness);
    g.setColour (t.textPrimary);
    g.setFont (fonts::title());
    g.drawText (spec.title, cardBounds().reduced (pad, pad).removeFromTop (titleH), juce::Justification::centredLeft, true);
}

void InlineDialog::resized()
{
    auto r = cardBounds().reduced (pad);
    r.removeFromTop (titleH + 6);
    juce::AttributedString as;
    as.append (spec.message, fonts::body());
    juce::TextLayout tl;
    tl.createLayout (as, static_cast<float> (r.getWidth()));
    const int msgH = static_cast<int> (std::ceil (tl.getHeight())) + 2;
    messageLabel.setFont (fonts::body());
    messageLabel.setBounds (r.removeFromTop (msgH));
    r.removeFromTop (10);
    for (std::size_t i = 0; i < editors.size(); ++i)
    {
        fieldLabels[i]->setBounds (r.removeFromTop (fieldLabelH));
        editors[i]->setBounds (r.removeFromTop (spec.fields[i].multiline ? multiFieldH : fieldH));
        r.removeFromTop (8);
    }
    auto row = r.removeFromBottom (buttonH);
    int bw = 0;
    for (auto& b : buttons)
        bw += juce::jmax (84, static_cast<int> (textWidth (fonts::body(), b->getButtonText())) + 32) + 8;
    row = row.removeFromRight (juce::jmin (row.getWidth(), bw));
    for (auto& b : buttons)
    {
        const int w = juce::jmax (84, static_cast<int> (textWidth (fonts::body(), b->getButtonText())) + 32);
        b->setBounds (row.removeFromLeft (w + 8).withTrimmedRight (8));
    }
}

bool InlineDialog::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey)
    {
        finish (static_cast<int> (spec.buttons.size()) - 1);
        return true;
    }
    if (k == juce::KeyPress::returnKey && ! buttons.empty())
    {
        finish (0);
        return true;
    }
    return true; // modal: swallow everything else
}

// ---- OverlayCard ----------------------------------------------------------------------------------------------
OverlayCard::OverlayCard (const juce::String& title, juce::Component& c, juce::Point<int> max)
    : heading (title), content (c), maxSize (max)
{
    setWantsKeyboardFocus (true);
    setFocusContainerType (FocusContainerType::keyboardFocusContainer);
    setTitle (title);
    addAndMakeVisible (content);
    closeButton.setTitle ("Close " + title);
    closeButton.setTooltip ("Close (Escape)");
    closeButton.onClick = [this] { if (onDismiss) onDismiss(); };
    addAndMakeVisible (closeButton);
}

OverlayCard::~OverlayCard()
{
    removeChildComponent (&content);
}

juce::Rectangle<int> OverlayCard::cardBounds() const
{
    const int w = juce::jmin (maxSize.x, getWidth() - 24);
    const int h = juce::jmin (maxSize.y, getHeight() - 24);
    return juce::Rectangle<int> (w, h).withCentre (getLocalBounds().getCentre());
}

void OverlayCard::paint (juce::Graphics& g)
{
    const auto& t = SnaralLookAndFeel::tokensFor (*this);
    g.fillAll (juce::Colours::black.withAlpha (0.6f));
    const auto card = cardBounds().toFloat();
    SnaralLookAndFeel::drawPanelShadow (g, t, card, 6.0f);
    g.setColour (t.panel);
    g.fillRoundedRectangle (card, 6.0f);
    g.setColour (t.outline);
    g.drawRoundedRectangle (card.reduced (0.5f), 6.0f, t.borderThickness);
    g.setColour (t.textPrimary);
    g.setFont (fonts::title());
    g.drawText (heading, cardBounds().reduced (14, 0).removeFromTop (40), juce::Justification::centredLeft, true);
    g.setColour (t.border);
    g.drawHorizontalLine (cardBounds().getY() + 40, static_cast<float> (cardBounds().getX()), static_cast<float> (cardBounds().getRight()));
}

void OverlayCard::resized()
{
    auto card = cardBounds();
    auto head = card.removeFromTop (40);
    closeButton.setBounds (head.removeFromRight (92).reduced (6, 4));
    content.setBounds (card.reduced (8));
}

void OverlayCard::mouseDown (const juce::MouseEvent& e)
{
    if (! cardBounds().contains (e.getPosition()) && onDismiss)
        onDismiss();
}

bool OverlayCard::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey && onDismiss)
    {
        onDismiss();
        return true;
    }
    return false;
}
} // namespace snaral::ui
