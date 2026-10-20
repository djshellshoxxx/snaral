#pragma once

#include <JuceHeader.h>
#include "Engine/SineVoice.h"

class SnaralAudioProcessor final : public juce::AudioProcessor
{
public:
    SnaralAudioProcessor();

    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.15; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

private:
    class Voice final : public juce::SynthesiserVoice
    {
    public:
        bool canPlaySound (juce::SynthesiserSound* sound) override;
        void startNote (int midiNote, float velocity, juce::SynthesiserSound*, int) override;
        void stopNote (float, bool allowTailOff) override;
        void pitchWheelMoved (int) override {}
        void controllerMoved (int, int) override {}
        void renderNextBlock (juce::AudioBuffer<float>&, int startSample, int numSamples) override;

    private:
        snaral::SineVoice oscillator_;
    };

    class Sound final : public juce::SynthesiserSound
    {
    public:
        bool appliesToNote (int) override { return true; }
        bool appliesToChannel (int) override { return true; }
    };

    juce::Synthesiser synth_;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SnaralAudioProcessor)
};
