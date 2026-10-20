#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

#include <array>
#include <memory>

class SnaralAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit SnaralAudioProcessorEditor (SnaralAudioProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    juce::Label title_;
    juce::Label macroHeading_;
    juce::Label status_;
    std::array<juce::Label, 5> macroLabels_;
    std::array<juce::Slider, 5> macroSliders_;
    std::array<std::unique_ptr<SliderAttachment>, 5> macroAttachments_;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SnaralAudioProcessorEditor)
};
