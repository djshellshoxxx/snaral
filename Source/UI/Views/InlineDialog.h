#pragma once

// A small modal question drawn inside the editor (title, message, optional text fields, buttons). Used instead of native
// alert windows, which are unreliable inside plug-in hosts and cannot be screenshot-tested.

#include "EditorController.h"
#include "UI/Theme.h"

namespace snaral::ui
{
class InlineDialog : public juce::Component
{
public:
    InlineDialog (DialogSpec spec, std::function<void()> onClosed);
    ~InlineDialog() override;

    void finish (int button);
    juce::TextButton& getButton (int index) { return *buttons[static_cast<std::size_t> (index)]; }
    int getNumButtons() const noexcept { return static_cast<int> (buttons.size()); }
    juce::TextEditor& getField (int index) { return *editors[static_cast<std::size_t> (index)]; }
    int getNumFields() const noexcept { return static_cast<int> (editors.size()); }
    const DialogSpec& getSpec() const noexcept { return spec; }

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseDown (const juce::MouseEvent&) override {}
    void parentHierarchyChanged() override;

private:
    juce::Rectangle<int> cardBounds() const;
    int cardHeight (int cardWidth) const;

    DialogSpec spec;
    std::function<void()> closed;
    juce::Label messageLabel;
    std::vector<std::unique_ptr<juce::Label>> fieldLabels;
    std::vector<std::unique_ptr<juce::TextEditor>> editors;
    std::vector<std::unique_ptr<juce::TextButton>> buttons;
    bool finished = false;
};

/// Full-size scrim plus a centred card around a caller-owned content component (Help, Options, Export).
class OverlayCard : public juce::Component
{
public:
    OverlayCard (const juce::String& title, juce::Component& content, juce::Point<int> maxSize);
    ~OverlayCard() override;

    std::function<void()> onDismiss;
    juce::TextButton& getCloseButton() noexcept { return closeButton; }
    void setTitle (const juce::String& t) { heading = t; repaint(); }
    juce::Rectangle<int> cardBounds() const;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    juce::String heading;
    juce::Component& content;
    juce::Point<int> maxSize;
    juce::TextButton closeButton { "Close" };
};
} // namespace snaral::ui
