#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class SnaralAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit SnaralAudioProcessorEditor (SnaralAudioProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::Label title_;
    juce::Label status_;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SnaralAudioProcessorEditor)
};
