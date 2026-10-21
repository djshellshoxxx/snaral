#pragma once

// Advanced view: layer solo/mute header, eight scrollable tabs generated from the parameter table (UI kit), the
// Trajectory editor with the DSP curve evaluator, the Quality latency readout and engine diagnostics.

#include "ControlRegistry.h"
#include "EditorController.h"
#include "UI/AdvancedTabs.h"
#include "UI/TrajectoryEditor.h"

namespace snaral::ui
{
class AdvancedView : public juce::Component
{
public:
    AdvancedView (EditorController&, ControlRegistry&);
    ~AdvancedView() override;

    TabbedAdvanced& tabs() noexcept { return tabbed; }
    TrajectoryEditor* trajectoryEditor() noexcept { return trajectory; }
    juce::TextButton& soloSubButton() noexcept { return soloSub; }
    juce::TextButton& soloMidButton() noexcept { return soloMid; }
    juce::TextButton& muteSubButton() noexcept { return muteSub; }
    juce::TextButton& muteMidButton() noexcept { return muteMid; }

    void tick();                 ///< 10 Hz: latency and diagnostics readouts, layer chip
    void refreshLearnStates();

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void setSolo (int mode);
    void updateSoloButtons (float rawChoice);
    class Diagnostics;

    EditorController& ctl;
    ControlRegistry& registry;
    TabbedAdvanced tabbed;
    juce::TextButton soloSub { "Solo Sub" }, soloMid { "Solo Mid" }, muteSub { "Mute Sub" }, muteMid { "Mute Mid" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> muteSubAttach, muteMidAttach;
    std::unique_ptr<juce::ParameterAttachment> soloAttach;
    TrajectoryEditor* trajectory = nullptr;
    juce::Label latencyLabel;
    Diagnostics* diagnostics = nullptr;
    juce::String chipText;
    juce::Rectangle<int> chipBounds;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AdvancedView)
};
} // namespace snaral::ui
