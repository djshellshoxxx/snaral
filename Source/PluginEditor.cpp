#include "PluginEditor.h"
#include "Parameters/ParameterRegistry.h"

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

    macroHeading_.setText ("MACRO CONTROLS", juce::dontSendNotification);
    macroHeading_.setJustificationType (juce::Justification::centredLeft);
    macroHeading_.setFont (juce::Font (12.0f, juce::Font::bold));
    macroHeading_.setColour (juce::Label::textColourId, juce::Colour::fromRGB (142, 153, 169));
    addAndMakeVisible (macroHeading_);

    for (std::size_t i = 0; i < snaral::macroParameterSpecs.size(); ++i)
    {
        const auto& spec = snaral::macroParameterSpecs[i];
        const auto id = juce::String::fromUTF8 (spec.id.data(), static_cast<int> (spec.id.size()));
        const auto name = juce::String::fromUTF8 (spec.displayName.data(), static_cast<int> (spec.displayName.size()));

        auto& label = macroLabels_[i];
        label.setText (name.toUpperCase(), juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setFont (juce::Font (12.0f, juce::Font::bold));
        label.setColour (juce::Label::textColourId, juce::Colour::fromRGB (216, 221, 228));
        label.setTitle (name + " macro");
        addAndMakeVisible (label);

        auto& slider = macroSliders_[i];
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 72, 22);
        slider.setTextValueSuffix (" %");
        slider.setNumDecimalPlacesToDisplay (0);
        slider.setRange (spec.minimum, spec.maximum, 1.0);
        slider.setDoubleClickReturnValue (true, spec.defaultValue);
        slider.setWantsKeyboardFocus (true);
        slider.setTitle (name);
        slider.setDescription (name + " macro control. Double-click to reset to its default value.");
        slider.setColour (juce::Slider::rotarySliderFillColourId, juce::Colour::fromRGB (232, 83, 42));
        slider.setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour::fromRGB (52, 61, 74));
        slider.setColour (juce::Slider::thumbColourId, juce::Colour::fromRGB (245, 247, 250));
        slider.setColour (juce::Slider::textBoxTextColourId, juce::Colour::fromRGB (230, 232, 236));
        slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colour::fromRGB (24, 29, 37));
        slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colour::fromRGB (52, 61, 74));
        addAndMakeVisible (slider);
        macroAttachments_[i] = std::make_unique<SliderAttachment> (processor.getParameters(), id, slider);
    }

    status_.setText ("Sub + Mid engine  |  Development build", juce::dontSendNotification);
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

    auto panel = getLocalBounds().toFloat().reduced (20.0f);
    panel.removeFromTop (68.0f);
    panel.removeFromBottom (52.0f);
    graphics.setColour (juce::Colour::fromRGB (20, 24, 31));
    graphics.fillRoundedRectangle (panel, 10.0f);
    graphics.setColour (juce::Colour::fromRGB (42, 48, 58));
    graphics.drawRoundedRectangle (panel, 10.0f, 1.0f);
}

void SnaralAudioProcessorEditor::resized()
{
    title_.setBounds (20, 8, 240, 36);
    auto content = getLocalBounds().reduced (30, 68);
    status_.setBounds (20, getHeight() - 42, getWidth() - 40, 24);
    macroHeading_.setBounds (content.removeFromTop (24));
    content.removeFromTop (12);

    const auto columnWidth = content.getWidth() / static_cast<int> (macroSliders_.size());
    for (std::size_t i = 0; i < macroSliders_.size(); ++i)
    {
        auto column = content.removeFromLeft (i + 1 == macroSliders_.size() ? content.getWidth() : columnWidth);
        column = column.reduced (6, 0);
        macroLabels_[i].setBounds (column.removeFromTop (24));
        column.removeFromTop (8);
        macroSliders_[i].setBounds (column);
    }
}
