#pragma once

// Main view (docs/specs/ui-layout.md, macros-ui.md): common action row, five macros, performance controls, variation
// slots, scope + meter + CPU gauge, output controls and the quality readout.

#include "ControlRegistry.h"
#include "EditorController.h"
#include "ProcessorSources.h"
#include "UI/Displays.h"
#include "UI/MacroKnob.h"
#include "UI/ParamControls.h"
#include "UI/Theme.h"

namespace snaral::ui
{
class MainView : public juce::Component,
                 private EditorController::Listener
{
public:
    MainView (EditorController&, ControlRegistry&, DiagnosticsSources&);
    ~MainView() override;

    static constexpr const char* macroIds[5] = { "weight", "growl", "bite", "motion", "space" };

    MacroKnob& macro (int index) { return *knobs[static_cast<std::size_t> (index)]; }
    ScopeView& getScope() noexcept { return scope; }
    LevelMeter& getMeter() noexcept { return meter; }
    CpuGauge& getCpu() noexcept { return cpu; }
    juce::TextButton& panicButton() noexcept { return panicBtn; }
    juce::TextButton& panicButton2() noexcept { return panic2Btn; }
    juce::TextButton& undoButton() noexcept { return undoBtn; }
    juce::TextButton& redoButton() noexcept { return redoBtn; }
    juce::TextButton& abButton (int side) { return side == 0 ? abA : abB; }
    juce::TextButton& bypassButton() noexcept { return bypassBtn; }
    juce::TextButton& randomizeButton() noexcept { return randomBtn; }
    juce::TextButton& resetButton() noexcept { return resetBtn; }
    juce::TextButton& saveButton() noexcept { return saveBtn; }
    juce::TextButton& exportButton() noexcept { return exportBtn; }
    juce::TextButton& moreButton() noexcept { return moreBtn; }
    juce::TextButton& variationButton (int slot) { return *varBtn[static_cast<std::size_t> (slot)]; }
    bool isCompactActionRow() const noexcept { return compactActions; }

    /// Options toggles.
    void setSignalVisibility (bool scopeVisible, bool cpuVisible);
    /// 10 Hz: quality readout, layer chip.
    void tick();
    void refreshLearnStates();

    std::function<void()> onExportWav;
    std::function<void()> onSaveAs;
    std::function<void()> onImportPreset;
    std::function<void()> onExportPreset;
    std::function<void()> onRescan;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    void controllerStateChanged() override;
    void refreshFromController();
    void layoutActionRow (juce::Rectangle<int> row);
    void showMoreMenu();
    void showRandomizePopover();
    void showVariationMenu (int slot);
    template <class C> void registerSecondary (const juce::String& id, C& c);

    EditorController& ctl;
    ControlRegistry& registry;
    DiagnosticsSources& sources;

    // action row
    juce::TextButton panicBtn { "Panic" }, resetBtn { "Reset" }, randomBtn { "Randomize" }, seedBtn { "--------" }, bypassBtn { "Bypass" },
        undoBtn { "Undo" }, redoBtn { "Redo" }, abA { "A" }, abB { "B" }, copyBtn { "A > B" }, saveBtn { "Save" },
        exportBtn { "Export WAV" }, moreBtn { "More" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttach;
    std::vector<juce::Component*> actionOrder;   // visual / focus order
    bool compactActions = false;

    // macros and signal
    std::array<std::unique_ptr<MacroKnob>, 5> knobs;
    ScopeView scope;
    LevelMeter meter;
    CpuGauge cpu;

    // performance
    std::unique_ptr<ParamCombo> voiceMode, lfoMode, lfoDivision, oversampling;
    std::unique_ptr<ParamToggle> legato;
    std::unique_ptr<ParamSlider> glide, lfoRate, trim, ceiling;
    juce::Label qualityLabel;
    std::array<std::unique_ptr<juce::TextButton>, 4> varBtn;
    juce::TextButton panic2Btn { "Panic" };
    juce::String layerChipText;
    juce::Rectangle<int> layerChipBounds, signalBlock;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainView)
};
} // namespace snaral::ui
