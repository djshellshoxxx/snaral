#pragma once

#include <JuceHeader.h>
#include "Engine/SineVoice.h"
#include "Parameters/MacroStateValidation.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>

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
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void refreshMacroSnapshot() noexcept;
    static bool validateStateTree (const juce::ValueTree& candidate);
    static bool parseStateValue (const juce::var& input, float& output);

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

    juce::AudioProcessorValueTreeState parameters_;
    std::array<std::atomic<float>*, snaral::macroParameterSpecs.size()> rawMacroValues_ {};
    std::array<float, snaral::macroParameterSpecs.size()> activeMacroValues_ {};
    std::atomic<std::uint64_t> stateRestoreSequence_ { 0 };
    std::mutex stateMutex_;
    juce::Synthesiser synth_;
    double sampleRate_ = 44100.0;
    float currentWeightGain_ = 0.0f;
    float targetWeightGain_ = 0.0f;
    float weightGainStep_ = 0.0f;
    int weightGainSamplesRemaining_ = 0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SnaralAudioProcessor)
};
