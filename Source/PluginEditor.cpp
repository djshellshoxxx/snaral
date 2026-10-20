#include "PluginEditor.h"

SnaralAudioProcessorEditor::SnaralAudioProcessorEditor (SnaralAudioProcessor& processor)
    : AudioProcessorEditor (&processor)
{
    setSize (480, 300);
    title_.setText ("SNARAL", juce::dontSendNotification);
    title_.setJustificationType (juce::Justification::centred);
    title_.setFont (juce::Font (32.0f, juce::Font::bold));
    title_.setColour (juce::Label::textColourId, juce::Colour::fromRGB (241, 235, 221));
    addAndMakeVisible (title_);
    status_.setText ("MIDI Sine Foundation  |  Development build", juce::dontSendNotification);
    status_.setJustificationType (juce::Justification::centred);
    status_.setColour (juce::Label::textColourId, juce::Colour::fromRGB (170, 178, 185));
    addAndMakeVisible (status_);
}

void SnaralAudioProcessorEditor::paint (juce::Graphics& graphics)
{
    graphics.fillAll (juce::Colour::fromRGB (24, 29, 34));
    graphics.setColour (juce::Colour::fromRGB (0, 211, 168));
    graphics.fillRoundedRectangle (getLocalBounds().toFloat().reduced (10.0f), 12.0f);
    graphics.setColour (juce::Colour::fromRGB (24, 29, 34));
    graphics.fillRoundedRectangle (getLocalBounds().toFloat().reduced (12.0f), 10.0f);
}

void SnaralAudioProcessorEditor::resized()
{
    title_.setBounds (20, 90, getWidth() - 40, 50);
    status_.setBounds (20, 150, getWidth() - 40, 28);
}
