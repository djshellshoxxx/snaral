#pragma once

#include "Theme.h"
#include "UiOptions.h"

namespace snaral::ui
{
/// Options page (baseline section 5, cdl-theme-options.md): theme, tooltips, animations (reduced motion), UI scale, signal
/// display and CPU gauge visibility, rerun onboarding, about/version, and ONLY when running standalone: audio/MIDI device
/// selection (injected component) and a manually started/stopped 440 Hz reference tone at -12 dBFS. In a plug-in the
/// panel states that audio settings are controlled by the host and shows no ineffective controls.
///
/// All preferences go through the OptionsModel; the panel never touches audio parameters or project state. The panel scrolls
/// when it is taller than its bounds (720x480 windows).
class OptionsPanel : public juce::Component,
                     private OptionsModel::Listener
{
public:
    struct Config
    {
        bool standalone = false;                                   ///< show device + reference tone sections
        juce::String productName = "Snaral";
        juce::String versionText;                                  ///< "0.2.0-beta.1 (build ...)"
        juce::String formatText;                                   ///< e.g. "VST3" / "Standalone"; shown in About
        /// Creates the audio/MIDI device selector (e.g. an AudioDeviceSelectorComponent). Only used when standalone.
        std::function<std::unique_ptr<juce::Component>()> createDeviceSelector;
        /// Called when the user starts / stops the reference tone (standalone only). The editor owns the audio side.
        std::function<void (bool running)> onReferenceTone;
        std::function<void()> onRerunTour;                         ///< "Run introduction again"
        std::function<void()> onOpenHelp;                          ///< optional "Open help" button
        std::function<void (const juce::String& url)> onOpenUrl;   ///< default: URL::launchInDefaultBrowser
        std::function<void()> onClose;                             ///< optional Close button
    };

    OptionsPanel (OptionsModel&, Config);
    ~OptionsPanel() override;

    OptionsModel& getModel() noexcept { return options; }
    const Config& getConfig() const noexcept { return config; }

    /// Reference tone state. Stopping is always possible; closing the panel with the tone running stops it.
    bool isReferenceToneRunning() const noexcept { return toneRunning; }
    void setReferenceToneRunning (bool running, bool notify = true);

    /// Preferred height of the whole content (the panel scrolls when its bounds are smaller).
    int getContentHeight() const;
    juce::Viewport& getViewport() noexcept { return viewport; }

    /// UI scale values offered in the selector.
    static const std::vector<float>& scaleChoices();

    // Controls are public so editors/tests can focus or inspect them.
    juce::ComboBox& getThemeCombo() noexcept;
    juce::ComboBox& getScaleCombo() noexcept;
    juce::ToggleButton& getTooltipsToggle() noexcept;
    juce::ToggleButton& getAnimationsToggle() noexcept;
    juce::ToggleButton& getScopeToggle() noexcept;
    juce::ToggleButton& getCpuToggle() noexcept;
    juce::Button* getReferenceToneButton() noexcept;

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    class Content;
    void optionChanged (const juce::Identifier&) override;

    OptionsModel& options;
    Config config;
    bool toneRunning = false;
    juce::Viewport viewport;
    std::unique_ptr<Content> content;
    std::unique_ptr<juce::Button> closeButton;
};
} // namespace snaral::ui
