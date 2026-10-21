#include "OnboardingOverlay.h"

namespace snaral::ui
{
std::vector<OnboardingStep> OnboardingOverlay::defaultSnaralTour()
{
    return {
        { "welcome", "Welcome to Snaral",
          "Snaral builds a bass from two layers: a centred Sub for weight and a Mid for growl and movement. Five macros control "
          "both. This short tour does not change your project. Press Escape at any time to skip it.", {} },
        { "weight", "Weight",
          "Weight sets how much Sub you hear. Drag the knob, or click it and use the arrow keys. At 0 the Sub is silent.", "macro.weight" },
        { "growl", "Growl",
          "Growl sets how loud and aggressive the Mid layer is. At 0 the Mid is silent and you hear only the Sub.", "macro.growl" },
        { "bite", "Bite",
          "Bite adds filter emphasis and drive to the Mid layer for edge. The Sub stays clean.", "macro.bite" },
        { "motion", "Motion",
          "Motion sets how much the sound moves during each note. At 0 the sound is static.", "macro.motion" },
        { "space", "Space",
          "Space widens the Mid layer. The Sub always stays in the centre, so the low end stays solid in mono.", "macro.space" },
        { "preset", "Preset recall",
          "Pick a preset here to load a complete sound. Your own patch is restored when the tour ends.", "preset.selector" },
        { "layers", "Sub and Mid are separate",
          "The scope can show the Output, the Sub alone or the Mid alone, so you can see how the two layers differ. "
          "Layer Solo in Advanced lets you hear each one.", "scope" },
        { "advanced", "Advanced controls",
          "Advanced has a tab for every detailed control: Voice, Sub, Sources, Filter & Motion, Drive, Texture, Effects and "
          "Output. Each tab scrolls and its sections can be collapsed.", "advanced.tabs" },
        { "finish", "That is the tour",
          "You can run it again from Options at any time, and Help has a page for every control.", "options.button" },
    };
}

OnboardingOverlay::OnboardingOverlay()
{
    setSteps (defaultSnaralTour());
    setVisible (false);
    setWantsKeyboardFocus (true);
    setInterceptsMouseClicks (true, true);
    setFocusContainerType (FocusContainerType::keyboardFocusContainer);
    setTitle ("Introduction tour");

    backButton.onClick = [this] { back(); };
    nextButton.onClick = [this] { next(); };
    skipButton.onClick = [this] { skip(); };
    for (auto* b : { &backButton, &nextButton, &skipButton })
    {
        b->setWantsKeyboardFocus (true);
        addAndMakeVisible (*b);
    }
    backButton.setTooltip ("Previous step (Left arrow)");
    nextButton.setTooltip ("Next step (Right arrow or Enter)");
    skipButton.setTooltip ("Close the tour (Escape)");

    dontShowAgain.setWantsKeyboardFocus (true);
    dontShowAgain.onClick = [this] { if (onPromptPreferenceChanged) onPromptPreferenceChanged (dontShowAgain.getToggleState()); };
    addAndMakeVisible (dontShowAgain);
}

OnboardingOverlay::~OnboardingOverlay() = default;

void OnboardingOverlay::setSteps (std::vector<OnboardingStep> s)
{
    steps = std::move (s);
    index = 0;
}

void OnboardingOverlay::registerTarget (const juce::String& id, juce::Component* c)
{
    targets[id] = c;
}

void OnboardingOverlay::unregisterTarget (const juce::String& id) { targets.erase (id); }

juce::Component* OnboardingOverlay::findTarget (const juce::String& id) const
{
    const auto it = targets.find (id);
    return it == targets.end() ? nullptr : it->second.getComponent();
}

bool OnboardingOverlay::isDontShowAgainChecked() const { return dontShowAgain.getToggleState(); }

void OnboardingOverlay::start (int first)
{
    if (steps.empty())
        return;
    running = true;
    index = juce::jlimit (0, getNumSteps() - 1, first);
    setVisible (true);
    toFront (false);
    showStep();
    if (isShowing())
        nextButton.grabKeyboardFocus();
}

void OnboardingOverlay::goToStep (int i)
{
    if (! running)
        return;
    index = juce::jlimit (0, getNumSteps() - 1, i);
    showStep();
}

void OnboardingOverlay::next()
{
    if (! running)
        return;
    if (index + 1 >= getNumSteps())
        finish (Outcome::Completed);
    else
        goToStep (index + 1);
}

void OnboardingOverlay::back()
{
    if (running && index > 0)
        goToStep (index - 1);
}

void OnboardingOverlay::skip()
{
    if (running)
        finish (Outcome::Skipped);
}

void OnboardingOverlay::finish (Outcome o)
{
    running = false;
    setVisible (false);
    if (onFinished)
        onFinished (o);
}

void OnboardingOverlay::showStep()
{
    const auto& s = steps[static_cast<std::size_t> (index)];
    const bool last = index + 1 >= getNumSteps();
    nextButton.setButtonText (last ? "Finish" : "Next");
    backButton.setEnabled (index > 0);
    skipButton.setVisible (! last);
    dontShowAgain.setVisible (index == 0);
    setDescription ("Step " + juce::String (index + 1) + " of " + juce::String (getNumSteps()) + ": " + s.title + ". " + s.text);
    layoutCard();
    if (onStepShown)
        onStepShown (index, s);
    layoutCard();                                  // the callback may have moved the target (for example switching tabs)
    repaint();
    if (auto* h = getAccessibilityHandler())
        h->notifyAccessibilityEvent (juce::AccessibilityEvent::titleChanged);
    juce::AccessibilityHandler::postAnnouncement ("Step " + juce::String (index + 1) + " of " + juce::String (getNumSteps())
                                                      + ": " + s.title + ". " + s.text,
                                                  juce::AccessibilityHandler::AnnouncementPriority::medium);
}

juce::Rectangle<int> OnboardingOverlay::getHighlightBounds() const
{
    if (! juce::isPositiveAndBelow (index, getNumSteps()))
        return {};
    if (auto* target = findTarget (steps[static_cast<std::size_t> (index)].targetId))
        if (target->isVisible() && target->getWidth() > 0)
            return getLocalArea (target, target->getLocalBounds()).expanded (6).getIntersection (getLocalBounds());
    return {};
}

juce::Rectangle<int> OnboardingOverlay::getCardBounds() const { return card; }

juce::Point<int> OnboardingOverlay::measureCard() const
{
    const int w = juce::jmin (380, juce::jmax (240, getWidth() - 24));
    const auto& s = steps[static_cast<std::size_t> (index)];
    juce::AttributedString a;
    a.append (s.text, fonts::body().withHeight (13.0f), juce::Colours::white);
    juce::TextLayout tl;
    tl.createLayout (a, static_cast<float> (w - 32));
    int h = 16 + 14 + 4 + 22 + 8 + static_cast<int> (std::ceil (tl.getHeight())) + 12;
    if (index == 0)
        h += 34;
    h += metrics::buttonHeight + 16;
    return { w, h };
}

juce::Rectangle<int> OnboardingOverlay::computeCard (juce::Rectangle<int> target, juce::Point<int> size) const
{
    const auto bounds = getLocalBounds().reduced (10);
    juce::Rectangle<int> c (size.x, size.y);
    if (target.isEmpty())
        return c.withCentre (bounds.getCentre());

    const int gap = 12;
    const std::array<juce::Rectangle<int>, 4> candidates {
        c.withPosition (target.getCentreX() - size.x / 2, target.getBottom() + gap),     // below
        c.withPosition (target.getCentreX() - size.x / 2, target.getY() - gap - size.y), // above
        c.withPosition (target.getRight() + gap, target.getCentreY() - size.y / 2),      // right
        c.withPosition (target.getX() - gap - size.x, target.getCentreY() - size.y / 2)  // left
    };
    for (auto cand : candidates)
    {
        cand = cand.withX (juce::jlimit (bounds.getX(), juce::jmax (bounds.getX(), bounds.getRight() - size.x), cand.getX()))
                   .withY (juce::jlimit (bounds.getY(), juce::jmax (bounds.getY(), bounds.getBottom() - size.y), cand.getY()));
        if (! cand.intersects (target) && bounds.contains (cand))
            return cand;
    }
    // Nothing fits cleanly (small window, big target): keep the card on screen at the side with most room.
    const int below = bounds.getBottom() - target.getBottom(), above = target.getY() - bounds.getY();
    auto fallback = below >= above ? candidates[0] : candidates[1];
    return fallback.withX (juce::jlimit (bounds.getX(), juce::jmax (bounds.getX(), bounds.getRight() - size.x), fallback.getX()))
                   .withY (juce::jlimit (bounds.getY(), juce::jmax (bounds.getY(), bounds.getBottom() - size.y), fallback.getY()));
}

void OnboardingOverlay::layoutCard()
{
    if (! juce::isPositiveAndBelow (index, getNumSteps()) || getWidth() <= 0)
        return;
    highlight = getHighlightBounds();
    const auto size = measureCard();
    card = computeCard (highlight, size);

    auto inner = card.reduced (16, 0);
    auto bottom = inner.removeFromBottom (metrics::buttonHeight + 16).withTrimmedBottom (16);
    nextButton.setBounds (bottom.removeFromRight (84));
    bottom.removeFromRight (8);
    backButton.setBounds (bottom.removeFromRight (74));
    skipButton.setBounds (bottom.removeFromLeft (92));
    dontShowAgain.setBounds (inner.removeFromBottom (34).withHeight (28).translated (0, 0));
}

void OnboardingOverlay::resized()
{
    if (running)
        layoutCard();
}

bool OnboardingOverlay::keyPressed (const juce::KeyPress& k)
{
    if (! running)
        return false;
    if (k == juce::KeyPress::escapeKey)  { skip(); return true; }
    if (k == juce::KeyPress::rightKey || k == juce::KeyPress::returnKey) { next(); return true; }
    if (k == juce::KeyPress::leftKey)    { back(); return true; }
    return true;                                   // modal: swallow other keys so the editor beneath is not operated
}

void OnboardingOverlay::paint (juce::Graphics& g)
{
    if (! running || ! juce::isPositiveAndBelow (index, getNumSteps()))
        return;
    const auto& t = SnaralLookAndFeel::tokensFor (*this);
    const auto& s = steps[static_cast<std::size_t> (index)];

    // dimmed backdrop with a cut-out around the target
    juce::Path dim;
    dim.setUsingNonZeroWinding (false);
    dim.addRectangle (getLocalBounds().toFloat());
    if (! highlight.isEmpty())
        dim.addRoundedRectangle (highlight.toFloat(), 8.0f);
    g.setColour (juce::Colours::black.withAlpha (t.borderThickness > 1.0f ? 0.86f : 0.68f));
    g.fillPath (dim);

    if (! highlight.isEmpty())
    {
        g.setColour (t.accent);
        g.drawRoundedRectangle (highlight.toFloat().expanded (1.0f), 8.0f, 2.5f);
        g.setColour (t.textPrimary);                                               // second thin ring: visible on any theme
        g.drawRoundedRectangle (highlight.toFloat().expanded (4.0f), 10.0f, 1.0f);
    }

    // card
    const auto cf = card.toFloat();
    SnaralLookAndFeel::drawPanelShadow (g, t, cf, 8.0f);
    g.setColour (t.panel);
    g.fillRoundedRectangle (cf, 8.0f);
    g.setColour (t.outline);
    g.drawRoundedRectangle (cf.reduced (0.5f), 8.0f, juce::jmax (1.0f, t.borderThickness));

    auto inner = card.reduced (16, 16);
    g.setFont (fonts::label());
    g.setColour (t.textMuted);
    g.drawText ("STEP " + juce::String (index + 1) + " OF " + juce::String (getNumSteps()), inner.removeFromTop (14),
                juce::Justification::centredLeft, false);
    inner.removeFromTop (4);
    g.setFont (fonts::title().withHeight (15.0f));
    g.setColour (t.textPrimary);
    g.drawText (s.title, inner.removeFromTop (22), juce::Justification::centredLeft, false);
    inner.removeFromTop (8);
    g.setFont (fonts::body().withHeight (13.0f));
    g.setColour (t.textPrimary);
    g.drawFittedText (s.text, inner.withTrimmedBottom (index == 0 ? 80 : 46), juce::Justification::topLeft, 12, 1.0f);
}
} // namespace snaral::ui
