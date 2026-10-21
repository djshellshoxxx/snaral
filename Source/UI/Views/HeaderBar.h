#pragma once

// Top strip of the editor: product mark, preset prev / name / next, preset browser button, File menu (Save, Save As,
// Import, Export, Initialise, Rescan), Main / Advanced switch, MIDI map, Options and Help.

#include "EditorController.h"
#include "UI/Theme.h"

namespace snaral::ui
{
class HeaderBar : public juce::Component,
                  private EditorController::Listener
{
public:
    explicit HeaderBar (EditorController&);
    ~HeaderBar() override;

    enum class View { Main, Advanced };
    void setView (View v, bool notify);
    View getView() const noexcept { return view; }

    juce::TextButton& prevButton() noexcept { return prevBtn; }
    juce::TextButton& nextButton() noexcept { return nextBtn; }
    juce::TextButton& presetButton() noexcept { return presetBtn; }
    juce::TextButton& browseButton() noexcept { return browseBtn; }
    juce::TextButton& fileButton() noexcept { return fileBtn; }
    juce::TextButton& mainButton() noexcept { return mainBtn; }
    juce::TextButton& advancedButton() noexcept { return advBtn; }
    juce::TextButton& midiButton() noexcept { return midiBtn; }
    juce::TextButton& optionsButton() noexcept { return optionsBtn; }
    juce::TextButton& helpButton() noexcept { return helpBtn; }
    void setBrowserOpen (bool open);

    std::function<void (View)> onViewChanged;
    std::function<void()> onBrowse, onOptions, onHelp, onMidi, onExportWav;

    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void controllerStateChanged() override { refresh(); }
    void showFileMenu();

    EditorController& ctl;
    View view = View::Main;
    juce::TextButton prevBtn { "<" }, nextBtn { ">" }, presetBtn, browseBtn { "Browse" }, fileBtn { "File" }, mainBtn { "Main" },
        advBtn { "Advanced" }, midiBtn { "MIDI" }, optionsBtn { "Options" }, helpBtn { "?" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HeaderBar)
};
} // namespace snaral::ui
