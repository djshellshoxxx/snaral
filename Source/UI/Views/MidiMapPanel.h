#pragma once

// Options list of MIDI-learn mappings (docs/specs/expansion-midi-cc-learn.md, UI flow step 5): target, CC, channel (or
// Omni), min, max, invert, Remove and Clear all. Edits go through MidiLearnModel validation, never silently clamped.

#include "EditorController.h"
#include "UI/Theme.h"

namespace snaral::ui
{
class MidiMapPanel : public juce::Component,
                     private EditorController::Listener
{
public:
    explicit MidiMapPanel (EditorController&);
    ~MidiMapPanel() override;

    void refresh();
    juce::ListBox& getList() noexcept { return list; }
    juce::TextButton& clearAllButton() noexcept { return clearAll; }
    int getNumRows() const;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Model;
    class Row;
    void controllerStateChanged() override;

    EditorController& ctl;
    std::unique_ptr<Model> model;
    juce::ListBox list;
    juce::TextButton clearAll { "Clear all" };
    juce::Label count, hint;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiMapPanel)
};
} // namespace snaral::ui
