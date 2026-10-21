#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "UI/Displays.h"
#include "UI/HelpPanel.h"
#include "UI/OnboardingOverlay.h"
#include "UI/OptionsPanel.h"
#include "UI/StatusBar.h"
#include "UI/Theme.h"
#include "UI/UiOptions.h"
#include "UI/Views/AdvancedView.h"
#include "UI/Views/ControlRegistry.h"
#include "UI/Views/EditorController.h"
#include "UI/Views/EditorPrefs.h"
#include "UI/Views/ExportDialog.h"
#include "UI/Views/HeaderBar.h"
#include "UI/Views/InlineDialog.h"
#include "UI/Views/MainView.h"
#include "UI/Views/MidiMapPanel.h"
#include "UI/Views/PresetBrowserPanel.h"
#include "UI/Views/ProcessorSources.h"

#include <memory>

struct SnaralEditorConfig
{
    juce::File settingsFolder;   ///< where ui-settings.xml and editor-prefs.xml live; empty = the per-user default
    bool autoStartTour = true;   ///< first-run tour starts once, shortly after the editor opens
};

class SnaralAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                         private snaral::ui::OptionsModel::Listener,
                                         private snaral::ui::ThemeManager::Listener,
                                         private snaral::ui::EditorController::Listener,
                                         private juce::Timer
{
public:
    explicit SnaralAudioProcessorEditor (SnaralAudioProcessor&, SnaralEditorConfig config = {});
    ~SnaralAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    // ---- accessors (used by the tests and the snapshot tool) ----
    snaral::ui::EditorController& controller() noexcept { return *ctl; }
    snaral::ui::HeaderBar& header() noexcept { return headerBar; }
    snaral::ui::MainView& mainView() noexcept { return main; }
    snaral::ui::AdvancedView& advancedView() noexcept { return advanced; }
    snaral::ui::PresetBrowserPanel& browserPanel() noexcept { return browser; }
    snaral::ui::StatusBar& statusBar() noexcept { return status; }
    snaral::ui::ToastHost& toastHost() noexcept { return toasts; }
    snaral::ui::OnboardingOverlay& tourOverlay() noexcept { return tour; }
    snaral::ui::ExportDialog& exportDialog() noexcept { return exportDlg; }
    snaral::ui::OptionsPanel& optionsPanel() noexcept { return *optionsUi; }
    snaral::ui::HelpPanel& helpPanel() noexcept { return helpUi; }
    snaral::ui::OptionsModel& options() noexcept { return optionsModel; }
    snaral::ui::ControlRegistry& registry() noexcept { return controls; }
    snaral::ui::InlineDialog* activeDialog() noexcept { return dialog.get(); }

    void showMainView();
    void showAdvancedView();
    bool isAdvancedShown() const noexcept { return headerBar.getView() == snaral::ui::HeaderBar::View::Advanced; }
    void setBrowserOpen (bool open);
    bool isBrowserOpen() const noexcept { return browserOpen; }
    void showOptions();
    void showHelp();
    void showMidiMap();
    void showExport();
    void closeCard();
    bool isCardOpen() const noexcept { return card != nullptr; }
    void startTour();
    void tickNow() { timerCallback(); }

private:
    void optionChanged (const juce::Identifier&) override;
    void preferencesChanged() override;
    void controllerStateChanged() override;
    void timerCallback() override;

    void showCard (const juce::String& title, juce::Component& content, juce::Point<int> maxSize);
    void showDialogSpec (snaral::ui::DialogSpec);
    void applyPreferences();
    void persistOptions();
    void loadOptionsFromProject();
    void finishTour (snaral::ui::OnboardingOverlay::Outcome);
    void updateStatusChips();
    void layoutOverlays();

    SnaralAudioProcessor& proc;
    SnaralEditorConfig config_;
    juce::File settingsFile;
    snaral::ui::EditorPrefs prefs;
    snaral::ui::OptionsModel optionsModel;
    std::unique_ptr<snaral::ui::ThemeManager> theme;
    std::unique_ptr<snaral::ui::EditorController> ctl;
    snaral::ui::DiagnosticsSources sources;
    snaral::ui::ControlRegistry controls;

    snaral::ui::HeaderBar headerBar;
    snaral::ui::MainView main;
    snaral::ui::AdvancedView advanced;
    snaral::ui::PresetBrowserPanel browser;
    snaral::ui::StatusBar status;
    snaral::ui::ToastHost toasts;
    snaral::ui::MidiMapPanel midiPanel;
    snaral::ui::ExportDialog exportDlg;
    snaral::ui::HelpPanel helpUi;
    std::unique_ptr<snaral::ui::OptionsPanel> optionsUi;
    snaral::ui::OnboardingOverlay tour;
    std::unique_ptr<snaral::ui::OverlayCard> card;
    std::unique_ptr<snaral::ui::InlineDialog> dialog;
    std::unique_ptr<juce::TooltipWindow> tooltipWindow;

    bool browserOpen = false;
    bool applyingState = false;
    bool tourAutoPending = false;
    std::uint64_t seenRestoreSequence = 0;
    int tickCount = 0;
    std::uint32_t seenOverruns = 0, overrunChipUntilMs = 0;
    snaral::ParamSnapshot tourBackup;
    bool tourHasBackup = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SnaralAudioProcessorEditor)
};
