#pragma once

// Preset browser (docs/specs/expansion-preset-browser.md): search, filter chips, sort, factory + user list with favourite
// stars, audition (temporary state with Apply / Cancel) and load with an unsaved-changes confirmation.

#include "EditorController.h"
#include "UI/Theme.h"

namespace snaral::ui
{
class PresetBrowserPanel : public juce::Component,
                           private EditorController::Listener
{
public:
    explicit PresetBrowserPanel (EditorController&);
    ~PresetBrowserPanel() override;

    static constexpr int panelWidth = 320;

    void refresh();                          ///< re-runs the query and redraws the list
    void focusSearch();
    juce::ListBox& getList() noexcept { return list; }
    juce::TextEditor& getSearch() noexcept { return search; }
    juce::TextButton& auditionButton() noexcept { return auditionBtn; }
    juce::TextButton& applyButton() noexcept { return applyBtn; }
    juce::TextButton& cancelButton() noexcept { return cancelBtn; }
    juce::TextButton& loadButton() noexcept { return loadBtn; }
    juce::ComboBox& sortCombo() noexcept { return sortBox; }
    juce::ComboBox& tagCombo() noexcept { return tagBox; }
    juce::TextButton& chip (const juce::String& name);
    juce::String getEmptyMessage() const { return emptyText; }
    std::function<void()> onCloseRequested;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void visibilityChanged() override;

private:
    class Model;
    void controllerStateChanged() override;
    void queryChanged();
    void updateButtons();
    int layoutChips (juce::Rectangle<int> area, bool apply);

    EditorController& ctl;
    std::unique_ptr<Model> model;
    juce::TextEditor search;
    juce::ComboBox sortBox, tagBox;
    juce::ListBox list;
    juce::TextButton auditionBtn { "Audition" }, applyBtn { "Apply" }, cancelBtn { "Cancel" }, loadBtn { "Load" }, rescanBtn { "Rescan" },
        closeBtn { "Close" };
    std::vector<std::unique_ptr<juce::TextButton>> chips;
    juce::String emptyText;
    bool updating = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetBrowserPanel)
};
} // namespace snaral::ui
