#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

#include <array>
#include <functional>
#include <memory>

class SnaralWaveform final : public juce::Component, private juce::Timer
{
public:
    explicit SnaralWaveform (SnaralAudioProcessor&);
    void paint (juce::Graphics&) override;
    void setAnimationsEnabled (bool);
    void mouseUp (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
private:
    void timerCallback() override;
    SnaralAudioProcessor& processor_;
    std::array<float, 512> samples_ {};
    std::array<float, 2048> fftBuffer_ {};
    juce::dsp::FFT fft_ { 10 };
    bool animationsEnabled_ = true;
    bool spectrumView_ = false;
};

class SnaralKeyboardPanel final : public juce::Component
{
public:
    SnaralKeyboardPanel (juce::MidiKeyboardState&, std::function<void()> onClose);
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    bool isCollapsed() const noexcept { return collapsed_; }
    void collapse() { setCollapsed (true); }
private:
    void setCollapsed (bool);
    juce::MidiKeyboardComponent keyboard_;
    juce::TextButton collapseButton_ { "Keyboard" };
    juce::TextButton closeButton_ { "×" };
    juce::ComponentDragger dragger_;
    juce::ComponentBoundsConstrainer constrainer_;
    std::function<void()> onClose_;
    bool collapsed_ = false;
};

class SnaralAudioProcessorEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SnaralAudioProcessorEditor (SnaralAudioProcessor&);
    ~SnaralAudioProcessorEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    SnaralAudioProcessor& processor_;
    void togglePlayback();
    void updateTheme();
    void importMidiFile();
    void exportMidiFile();
    void timerCallback() override;

    juce::Label title_;
    juce::Label macroHeading_;
    juce::Label status_;
    juce::TextButton playButton_ { "▶  PLAY" };
    juce::TextButton keyboardButton_ { "KEYBOARD" };
    juce::TextButton animationButton_ { "ANIMATION: ON" };
    juce::TextButton themeButton_ { "THEME: EMBER" };
    juce::TextButton importMidiButton_ { "MIDI IN" };
    juce::TextButton exportMidiButton_ { "MIDI OUT" };
    juce::Slider pitchBendSlider_;
    juce::Label pitchBendLabel_;
    std::array<juce::Label, 5> macroLabels_;
    std::array<juce::Slider, 5> macroSliders_;
    std::array<std::unique_ptr<SliderAttachment>, 5> macroAttachments_;
    std::unique_ptr<SnaralWaveform> waveform_;
    std::unique_ptr<SnaralKeyboardPanel> keyboardPanel_;
    std::unique_ptr<juce::FileChooser> midiFileChooser_;
    juce::MidiMessageSequence importedMidi_;
    int importedMidiEventIndex_ = 0;
    double importedMidiStartTimeMs_ = 0.0;
    bool importedMidiPlaying_ = false;
    int themeIndex_ = 0;
    int auditionNote_ = 48;
    bool auditioning_ = false;
    bool keyboardShown_ = false;
    bool animationsEnabled_ = true;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SnaralAudioProcessorEditor)
};
