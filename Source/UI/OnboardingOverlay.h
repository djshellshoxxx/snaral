#pragma once

#include "Theme.h"

#include <map>

namespace snaral::ui
{
struct OnboardingStep
{
    juce::String id;
    juce::String title;
    juce::String text;
    juce::String targetId;       ///< id registered with registerTarget(); empty = centred card with no highlight
};

/// Interactive first-run tour (expansion-onboarding-tour.md, cdl-help-and-onboarding.md): a dimmed overlay with a
/// highlighted cut-out around the registered component of the current step and a card with concise text and Back / Next /
/// Skip. Fully keyboard operable (Right / Enter = next, Left = back, Escape = skip, Tab stays inside the card) and uses no
/// network. Add it as the TOP child of the editor with the editor's full bounds.
///
/// The overlay owns no sound state. The editor can react in `onStepShown` (for example switch to the Advanced view for the
/// Advanced step, or play a built-in demo through a disposable copy of the patch) and must restore the user's patch from
/// `onFinished`, whether the tour was completed or skipped.
class OnboardingOverlay : public juce::Component
{
public:
    enum class Outcome { Completed, Skipped };

    OnboardingOverlay();
    ~OnboardingOverlay() override;

    /// Steps for Snaral: welcome, Weight, Growl, Bite, Motion, Space, preset recall, layer separation, Advanced, finish.
    /// Target ids used: macro.weight, macro.growl, macro.bite, macro.motion, macro.space, preset.selector,
    /// scope, advanced.tabs, options.button.
    static std::vector<OnboardingStep> defaultSnaralTour();

    void setSteps (std::vector<OnboardingStep>);
    int getNumSteps() const noexcept { return static_cast<int> (steps.size()); }
    const OnboardingStep& getStep (int index) const { return steps[static_cast<std::size_t> (index)]; }

    /// Targets are looked up by id when a step is shown; nothing is owned. Re-register after rebuilding the UI.
    void registerTarget (const juce::String& id, juce::Component* target);
    void unregisterTarget (const juce::String& id);
    juce::Component* findTarget (const juce::String& id) const;

    // ---- control ------------------------------------------------------------------------------------------------
    void start (int firstStep = 0);
    void next();                         ///< on the last step this finishes the tour
    void back();
    void skip();                         ///< finishes with Outcome::Skipped
    void goToStep (int index);
    bool isRunning() const noexcept { return running; }
    int getStepIndex() const noexcept { return index; }

    /// "Do not offer this tour again" checkbox (shown on the first step).
    bool isDontShowAgainChecked() const;

    std::function<void (int index, const OnboardingStep&)> onStepShown;
    std::function<void (Outcome)> onFinished;
    std::function<void (bool disabled)> onPromptPreferenceChanged;

    /// Screen-independent geometry used by paint and by tests.
    juce::Rectangle<int> getHighlightBounds() const;
    juce::Rectangle<int> getCardBounds() const;

    juce::TextButton& getNextButton() noexcept { return nextButton; }
    juce::TextButton& getBackButton() noexcept { return backButton; }
    juce::TextButton& getSkipButton() noexcept { return skipButton; }

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseDown (const juce::MouseEvent&) override {}      // swallow clicks on the dimmed area

private:
    void finish (Outcome);
    void showStep();
    void layoutCard();
    juce::Rectangle<int> computeCard (juce::Rectangle<int> target, juce::Point<int> size) const;
    juce::Point<int> measureCard() const;

    std::vector<OnboardingStep> steps;
    std::map<juce::String, juce::Component::SafePointer<juce::Component>> targets;
    int index = 0;
    bool running = false;
    juce::Rectangle<int> highlight, card;

    juce::TextButton backButton { "Back" }, nextButton { "Next" }, skipButton { "Skip tour" };
    juce::ToggleButton dontShowAgain { "Do not offer this tour again" };
};
} // namespace snaral::ui
