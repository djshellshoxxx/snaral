#include "PluginEditor.h"

SnaralAudioProcessorEditor::SnaralAudioProcessorEditor (SnaralAudioProcessor& processor)
    : AudioProcessorEditor (&processor)
{
    setSize (900, 560);
    setResizable (true, true);
    setResizeLimits (720, 480, 1440, 900);
    title_.setText ("SNARAL", juce::dontSendNotification);
    title_.setJustificationType (juce::Justification::centredLeft);
    title_.setFont (juce::Font (20.0f, juce::Font::bold));
    title_.setColour (juce::Label::textColourId, juce::Colour::fromRGB (230, 232, 236));
    addAndMakeVisible (title_);
    status_.setText ("MIDI Sine Foundation  |  Development build", juce::dontSendNotification);
    status_.setJustificationType (juce::Justification::centred);
    status_.setColour (juce::Label::textColourId, juce::Colour::fromRGB (138, 146, 158));
    addAndMakeVisible (status_);
}

void SnaralAudioProcessorEditor::paint (juce::Graphics& graphics)
{
    graphics.fillAll (juce::Colour::fromRGB (14, 17, 22));
    graphics.setColour (juce::Colour::fromRGB (232, 83, 42));
    graphics.drawLine (0.0f, 12.0f, 12.0f, 0.0f, 2.0f);
    graphics.setColour (juce::Colour::fromRGB (42, 48, 58));
    graphics.drawHorizontalLine (52, 20.0f, static_cast<float> (getWidth() - 20));
}

void SnaralAudioProcessorEditor::resized()
{
    title_.setBounds (20, 8, 240, 36);
    status_.setBounds (20, 225, getWidth() - 40, 28);
}
