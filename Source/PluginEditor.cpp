#include "PluginEditor.h"

#if JucePlugin_Build_Standalone
 #include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#endif

#include "Dsp/Lfo.h"

namespace ui = snaral::ui;

namespace
{
juce::File settingsFileFor (const SnaralEditorConfig& c)
{
    return c.settingsFolder == juce::File() ? ui::SettingsFile::defaultFile() : c.settingsFolder.getChildFile ("ui-settings.xml");
}

juce::File prefsFileFor (const SnaralEditorConfig& c)
{
    return c.settingsFolder == juce::File() ? ui::EditorPrefs::defaultFile() : c.settingsFolder.getChildFile ("editor-prefs.xml");
}

const juce::Identifier viewProp { "view" }, wProp { "editorW" }, hProp { "editorH" }, browserProp { "browserOpen" };
} // namespace

SnaralAudioProcessorEditor::SnaralAudioProcessorEditor (SnaralAudioProcessor& p, SnaralEditorConfig cfg)
    : AudioProcessorEditor (&p),
      proc (p),
      config_ (cfg),
      settingsFile (settingsFileFor (cfg)),
      prefs (prefsFileFor (cfg)),
      theme (std::make_unique<ui::ThemeManager> (optionsModel)),
      ctl (std::make_unique<ui::EditorController> (p, optionsModel, prefs)),
      sources (p.diagnostics(), [&p] { return p.getSampleRate(); }),
      headerBar (*ctl),
      main (*ctl, controls, sources),
      advanced (*ctl, controls),
      browser (*ctl),
      midiPanel (*ctl),
      exportDlg (*ctl)
{
    setTitle ("Snaral");
    setWantsKeyboardFocus (true);
    setFocusContainerType (FocusContainerType::keyboardFocusContainer);

    // ---- options: user file first, then the project's copy wins (except that a finished tour stays finished) ----
    ui::SettingsFile::load (optionsModel, settingsFile);
    const bool tourDoneForUser = optionsModel.isFirstRunComplete();
    const bool tourDisabledForUser = optionsModel.isTourPromptDisabled();
    {
        applyingState = true;
        loadOptionsFromProject();
        if (tourDoneForUser)
            optionsModel.setFirstRunComplete (true);
        if (tourDisabledForUser)
            optionsModel.setTourPromptDisabled (true);
        applyingState = false;
    }
    theme->applyTo (*this);

    helpUi.callbacks.rerunTour = [this] { closeCard(); startTour(); };

    ui::OptionsPanel::Config oc;
    oc.standalone = ctl->isStandalone();
    oc.versionText = ctl->versionText();
    oc.formatText = ctl->isStandalone() ? "Standalone" : juce::AudioProcessor::getWrapperTypeDescription (proc.wrapperType);
    oc.onRerunTour = [this] { closeCard(); startTour(); };
    oc.onOpenHelp = [this] { closeCard(); showHelp(); };
    oc.onReferenceTone = [this] (bool running)
    {
        if (! proc.referenceTone().setEnabled (running) && running)
        {
            optionsUi->setReferenceToneRunning (false, false);
            ctl->status (ui::Severity::Warning, "The 440 Hz reference tone is only available in the standalone app.");
        }
        else
            ctl->status (ui::Severity::Info, running ? "Reference tone on (440 Hz)." : "Reference tone off.");
    };
   #if JucePlugin_Build_Standalone
    if (ctl->isStandalone())
        oc.createDeviceSelector = []() -> std::unique_ptr<juce::Component>
        {
            if (auto* holder = juce::StandalonePluginHolder::getInstance())
            {
                auto sel = std::make_unique<juce::AudioDeviceSelectorComponent> (holder->deviceManager, 0, 0, 0, 2, true, false, true, false);
                sel->setSize (460, 300);
                return sel;
            }
            return nullptr;
        };
   #endif
    optionsUi = std::make_unique<ui::OptionsPanel> (optionsModel, oc);

    // ---- children (focus order = add order) ----
    addAndMakeVisible (headerBar);
    addAndMakeVisible (main);
    addChildComponent (advanced);
    addChildComponent (browser);
    addAndMakeVisible (status);
    addAndMakeVisible (toasts);
    addChildComponent (tour);
    status.setVersionText (ctl->versionText());

    headerBar.onViewChanged = [this] (ui::HeaderBar::View v)
    {
        main.setVisible (v == ui::HeaderBar::View::Main);
        advanced.setVisible (v == ui::HeaderBar::View::Advanced);
        resized();
        if (! applyingState)
            persistOptions();
    };
    headerBar.onBrowse = [this] { setBrowserOpen (! browserOpen); };
    headerBar.onOptions = [this] { showOptions(); };
    headerBar.onHelp = [this] { showHelp(); };
    headerBar.onMidi = [this] { showMidiMap(); };
    headerBar.onExportWav = [this] { showExport(); };
    main.onExportWav = [this] { showExport(); };
    main.onSaveAs = [this] { ctl->savePresetAs(); };
    main.onImportPreset = [this] { ctl->importPreset(); };
    main.onExportPreset = [this] { ctl->exportPreset(); };
    main.onRescan = [this] { ctl->rescanLibrary(); };
    browser.onCloseRequested = [this] { setBrowserOpen (false); };

    ctl->onStatus = [this] (ui::Severity s, const juce::String& text, int ttl)
    {
        status.post (s, text, ttl);
        if (s == ui::Severity::Warning || s == ui::Severity::Error)
            toasts.show (s, text, 0);
    };
    ctl->showDialog = [this] (ui::DialogSpec spec) { showDialogSpec (std::move (spec)); };
    ctl->onPanicPerformed = [this] { if (optionsUi != nullptr) optionsUi->setReferenceToneRunning (false, false); };
    ctl->addListener (this);
    optionsModel.addListener (this);
    theme->addListener (this);

    // ---- tour ----
    tour.setSteps (ui::OnboardingOverlay::defaultSnaralTour());
    for (int i = 0; i < 5; ++i)
        tour.registerTarget (juce::String ("macro.") + ui::MainView::macroIds[i], &main.macro (i));
    tour.registerTarget ("preset.selector", &headerBar.presetButton());
    tour.registerTarget ("scope", &main.getScope());
    tour.registerTarget ("advanced.tabs", &headerBar.advancedButton());
    tour.registerTarget ("options.button", &headerBar.optionsButton());
    tour.onStepShown = [this] (int, const ui::OnboardingStep&) { if (isAdvancedShown()) showMainView(); };
    tour.onFinished = [this] (ui::OnboardingOverlay::Outcome o) { finishTour (o); };
    tour.onPromptPreferenceChanged = [this] (bool disabled) { optionsModel.setTourPromptDisabled (disabled); };

    // ---- size and view restore ----
    const auto& ut = proc.uiTree();
    const int w = juce::jlimit (ui::metrics::minWidth, ui::metrics::maxWidth, static_cast<int> (ut.getProperty (wProp, ui::metrics::defaultWidth)));
    const int h = juce::jlimit (ui::metrics::minHeight, ui::metrics::maxHeight, static_cast<int> (ut.getProperty (hProp, ui::metrics::defaultHeight)));
    setResizable (true, true);
    setResizeLimits (ui::metrics::minWidth, ui::metrics::minHeight, ui::metrics::maxWidth, ui::metrics::maxHeight);
    setSize (w, h);
    if (static_cast<int> (ut.getProperty (viewProp, 0)) == 1)
        showAdvancedView();

    applyPreferences();
    seenRestoreSequence = proc.restoreSequence();
    updateStatusChips();
    main.refreshLearnStates();
    advanced.refreshLearnStates();
    tourAutoPending = config_.autoStartTour && optionsModel.shouldOfferTour();
    startTimerHz (30);
}

SnaralAudioProcessorEditor::~SnaralAudioProcessorEditor()
{
    stopTimer();
    if (exportDlg.isRendering())
        exportDlg.cancelRender();
    theme->removeListener (this);
    optionsModel.removeListener (this);
    ctl->removeListener (this);
    ctl->onStatus = nullptr;
    ctl->showDialog = nullptr;
    ctl->onPanicPerformed = nullptr;
    tooltipWindow.reset();
    card.reset();
    dialog.reset();
    theme->detach (*this);
}

void SnaralAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (ui::SnaralLookAndFeel::tokensFor (*this).background);
}

void SnaralAudioProcessorEditor::resized()
{
    auto r = getLocalBounds();
    headerBar.setBounds (r.removeFromTop (ui::metrics::headerHeight));
    status.setBounds (r.removeFromBottom (ui::StatusBar::preferredHeight));
    main.setBounds (r);
    advanced.setBounds (r);
    const int bw = juce::jmin (ui::PresetBrowserPanel::panelWidth, r.getWidth());
    browser.setBounds (r.removeFromRight (bw));
    toasts.setBounds (getLocalBounds().withTrimmedBottom (ui::StatusBar::preferredHeight));
    layoutOverlays();

    if (! applyingState)
    {
        auto& t = proc.uiTree();
        t.setProperty (wProp, getWidth(), nullptr);
        t.setProperty (hProp, getHeight(), nullptr);
    }
}

void SnaralAudioProcessorEditor::layoutOverlays()
{
    if (card != nullptr)
        card->setBounds (getLocalBounds());
    if (dialog != nullptr)
        dialog->setBounds (getLocalBounds());
    tour.setBounds (getLocalBounds());
}

bool SnaralAudioProcessorEditor::keyPressed (const juce::KeyPress& k)
{
    const auto mods = k.getModifiers();
    const int code = juce::CharacterFunctions::toUpperCase (static_cast<juce::juce_wchar> (k.getKeyCode()));

    if (mods.isCommandDown() && code == 'Z')
    {
        if (mods.isShiftDown()) ctl->redo(); else ctl->undo();
        return true;
    }
    if (mods.isCommandDown() && code == 'Y')
    {
        ctl->redo();
        return true;
    }
    if (mods.isCommandDown() && (k.getKeyCode() == '.' || k.getTextCharacter() == '.'))
    {
        ctl->panic();
        return true;
    }
    if (k == juce::KeyPress::escapeKey)
    {
        if (dialog != nullptr) return false;   // the dialog handles its own Escape
        if (card != nullptr) { closeCard(); return true; }
        if (ctl->learnArmed()) { ctl->cancelLearn(); return true; }
        if (browserOpen) { setBrowserOpen (false); return true; }
    }
    return false;
}

// ---- views ------------------------------------------------------------------------------------------------------

void SnaralAudioProcessorEditor::showMainView()
{
    headerBar.setView (ui::HeaderBar::View::Main, true);
}

void SnaralAudioProcessorEditor::showAdvancedView()
{
    headerBar.setView (ui::HeaderBar::View::Advanced, true);
}

void SnaralAudioProcessorEditor::setBrowserOpen (bool open)
{
    if (browserOpen == open)
        return;
    browserOpen = open;
    browser.setVisible (open);
    if (open)
    {
        browser.toFront (false);
        browser.refresh();
        browser.focusSearch();
    }
    else if (ctl->isAuditioning())
        ctl->cancelAudition();
    headerBar.setBrowserOpen (open);
    toasts.toFront (false);
    if (card != nullptr) card->toFront (false);
    if (dialog != nullptr) dialog->toFront (false);
    if (tour.isRunning()) tour.toFront (false);
}

void SnaralAudioProcessorEditor::showCard (const juce::String& title, juce::Component& content, juce::Point<int> maxSize)
{
    closeCard();
    card = std::make_unique<ui::OverlayCard> (title, content, maxSize);
    card->onDismiss = [this] { closeCard(); };
    addAndMakeVisible (*card);
    card->setBounds (getLocalBounds());
    card->toFront (true);
    if (dialog != nullptr) dialog->toFront (false);
}

void SnaralAudioProcessorEditor::closeCard()
{
    if (card == nullptr)
        return;
    if (exportDlg.isRendering())
        exportDlg.cancelRender();
    auto old = std::shared_ptr<ui::OverlayCard> (std::move (card));
    removeChildComponent (old.get());
    juce::MessageManager::callAsync ([old] {});   // destroyed after the click that closed it has returned
    grabKeyboardFocus();
}

void SnaralAudioProcessorEditor::showOptions() { showCard ("Options", *optionsUi, { 560, 520 }); }
void SnaralAudioProcessorEditor::showHelp() { showCard ("Help", helpUi, { 720, 500 }); }
void SnaralAudioProcessorEditor::showMidiMap()
{
    midiPanel.refresh();
    showCard ("MIDI mappings", midiPanel, { 680, 440 });
}

void SnaralAudioProcessorEditor::showExport()
{
    exportDlg.prepareForOpen();
    showCard ("Export WAV", exportDlg, { 640, 580 });
}

void SnaralAudioProcessorEditor::showDialogSpec (ui::DialogSpec spec)
{
    if (dialog != nullptr)
    {
        auto old = std::shared_ptr<ui::InlineDialog> (std::move (dialog));
        removeChildComponent (old.get());
        juce::MessageManager::callAsync ([old] {});
    }
    dialog = std::make_unique<ui::InlineDialog> (std::move (spec), [this]
    {
        if (dialog == nullptr)
            return;
        auto old = std::shared_ptr<ui::InlineDialog> (std::move (dialog));
        removeChildComponent (old.get());
        juce::MessageManager::callAsync ([old] {});
    });
    addAndMakeVisible (*dialog);
    dialog->setBounds (getLocalBounds());
    dialog->toFront (true);
}

// ---- tour -------------------------------------------------------------------------------------------------------

void SnaralAudioProcessorEditor::startTour()
{
    closeCard();
    if (isAdvancedShown())
        showMainView();
    tourBackup = ctl->snapshot();
    tourHasBackup = true;
    tour.setBounds (getLocalBounds());
    tour.toFront (true);
    tour.start (0);
}

void SnaralAudioProcessorEditor::finishTour (ui::OnboardingOverlay::Outcome)
{
    optionsModel.setFirstRunComplete (true);
    if (tourHasBackup)
    {
        // The tour promises not to change the patch; restore it if anything moved while it ran.
        const auto now = ctl->snapshot();
        if (now.v != tourBackup.v)
            proc.applySoundSnapshot (tourBackup);
        tourHasBackup = false;
    }
    grabKeyboardFocus();
}

// ---- options / state --------------------------------------------------------------------------------------------

void SnaralAudioProcessorEditor::optionChanged (const juce::Identifier&)
{
    if (! applyingState)
        persistOptions();
}

void SnaralAudioProcessorEditor::preferencesChanged() { applyPreferences(); }

void SnaralAudioProcessorEditor::applyPreferences()
{
    if (optionsModel.areTooltipsEnabled())
    {
        if (tooltipWindow == nullptr)
            tooltipWindow = std::make_unique<juce::TooltipWindow> (this, 600);
    }
    else
        tooltipWindow.reset();

    main.setSignalVisibility (optionsModel.isScopeVisible(), optionsModel.isCpuGaugeVisible());
    ui::ThemeManager::applyUiScale (*this, optionsModel.getUiScale());
    repaint();
}

void SnaralAudioProcessorEditor::persistOptions()
{
    ui::SettingsFile::save (optionsModel, settingsFile);   // failure is non-fatal: the project copy still holds the options
    auto& t = proc.uiTree();
    t.removeAllChildren (nullptr);
    t.appendChild (optionsModel.toValueTree(), nullptr);
    t.setProperty (viewProp, isAdvancedShown() ? 1 : 0, nullptr);
}

void SnaralAudioProcessorEditor::loadOptionsFromProject()
{
    const auto& t = proc.uiTree();
    if (! t.isValid() || t.getNumChildren() == 0)
        return;
    const auto c = t.getChild (0);
    if (c.isValid())
        optionsModel.setState (c);
}

void SnaralAudioProcessorEditor::controllerStateChanged()
{
    main.refreshLearnStates();
    advanced.refreshLearnStates();
    headerBar.refresh();

    const auto seq = proc.restoreSequence();
    if (seq != seenRestoreSequence)
    {
        seenRestoreSequence = seq;
        applyingState = true;
        loadOptionsFromProject();
        theme->refresh();
        applyPreferences();
        applyingState = false;
        if (static_cast<int> (proc.uiTree().getProperty (viewProp, 0)) == 1 && ! isAdvancedShown())
            showAdvancedView();
    }
}

void SnaralAudioProcessorEditor::updateStatusChips()
{
    // status line priority (macros-ui): overrun, ..., tempo unavailable while a sync mode is requested
    const auto rawOf = [this] (const char* id) { auto* v = proc.getParameters().getRawParameterValue (id); return v != nullptr ? v->load() : 0.0f; };
    const bool syncRequested = rawOf ("lfo_mode") >= 0.5f || (rawOf ("traj_enabled") >= 0.5f && rawOf ("traj_time_mode") >= 0.5f);
    const auto overruns = proc.diagnostics().overruns();
    if (overruns != seenOverruns)
    {
        seenOverruns = overruns;
        overrunChipUntilMs = juce::Time::getMillisecondCounter() + 5000u;
    }
    if (static_cast<std::int32_t> (overrunChipUntilMs - juce::Time::getMillisecondCounter()) > 0)
        status.setChip ("overrun", ui::Severity::Error, "Audio overrun: Snaral used more than the block time. Raise the buffer size or lower Oversampling.");
    else
        status.clearChip ("overrun");

    if (syncRequested && proc.diagnostics().syncFallbackActive())
        status.setChip ("tempo", ui::Severity::Warning, snaral::Lfo::fallbackMessage);
    else if (syncRequested && proc.diagnostics().tempoUnavailable())
        status.setChip ("tempo", ui::Severity::Warning, "Host tempo unavailable - keeping the last known tempo");
    else
        status.clearChip ("tempo");

    if (proc.diagnostics().droppedCommands() > 0 || proc.diagnostics().droppedMidiEvents() > 0)
        status.setChip ("dropped", ui::Severity::Warning, "Some events were dropped");
    else
        status.clearChip ("dropped");
}

void SnaralAudioProcessorEditor::timerCallback()
{
    ++tickCount;
    if (main.isVisible())
        main.tick();
    if (tickCount % 3 == 0)
    {
        if (advanced.isVisible())
            advanced.tick();
        updateStatusChips();
    }
    if (tourAutoPending && tickCount > 20 && isShowing() && card == nullptr && dialog == nullptr)
    {
        tourAutoPending = false;
        startTour();
    }
}
