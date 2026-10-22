#pragma once

#include <JuceHeader.h>
#include "Engine/SineVoice.h"
#include "Parameters/MacroStateValidation.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>

class SnaralAudioProcessor final : public juce::AudioProcessor, private juce::MidiKeyboardState::Listener
{
public:
    SnaralAudioProcessor();

    juce::AudioProcessorValueTreeState& getParameters() noexcept { return parameters_; }
    const juce::AudioProcessorValueTreeState& getParameters() const noexcept { return parameters_; }
    juce::MidiKeyboardState& getKeyboardState() noexcept { return keyboardState_; }
    void copyVisualizationSamples (std::array<float, 512>& destination) const noexcept;
    void enqueueUiPitchBend (float semitones) noexcept;
    juce::MidiMessageSequence createRecordedMidiSequence() const;

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
    struct UiMidiEvent;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void refreshMacroSnapshot() noexcept;
    static bool validateStateTree (const juce::ValueTree& candidate);
    static bool parseStateValue (const juce::var& input, float& output);
    void handleNoteOn (juce::MidiKeyboardState*, int channel, int note, float velocity) override;
    void handleNoteOff (juce::MidiKeyboardState*, int channel, int note, float velocity) override;
    void enqueueUiNote (bool noteOn, int channel, int note, float velocity) noexcept;
    void recordUiEvent (const UiMidiEvent&) noexcept;

    class Voice final : public juce::SynthesiserVoice
    {
    public:
        bool canPlaySound (juce::SynthesiserSound* sound) override;
        void startNote (int midiNote, float velocity, juce::SynthesiserSound*, int) override;
        void stopNote (float, bool allowTailOff) override;
        void pitchWheelMoved (int value) override { oscillator_.setPitchWheel (value); }
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
    juce::MidiKeyboardState keyboardState_;
    struct UiMidiEvent
    {
        enum class Type : std::uint8_t { noteOn, noteOff, pitchBend };
        int channel = 1;
        int note = 60;
        float value = 0.0f;
        Type type = Type::noteOff;
    };
    std::array<UiMidiEvent, 1024> uiMidiEvents_ {};
    std::atomic<std::uint32_t> uiMidiWrite_ { 0 };
    std::atomic<std::uint32_t> uiMidiRead_ { 0 };
    std::atomic<bool> uiMidiOverflow_ { false };
    juce::MidiBuffer uiMidiBuffer_;
    std::array<std::atomic<std::uint64_t>, 4096> recordedUiEvents_ {};
    std::atomic<std::uint32_t> recordedUiEventCount_ { 0 };
    std::array<std::atomic<float>*, snaral::macroParameterSpecs.size()> rawMacroValues_ {};
    std::array<float, snaral::macroParameterSpecs.size()> activeMacroValues_ {};
    std::atomic<std::uint64_t> stateRestoreSequence_ { 0 };
    std::array<std::atomic<float>, 512> visualizationSamples_ {};
    std::atomic<std::uint64_t> visualizationWriteCount_ { 0 };
    std::mutex stateMutex_;
    juce::Synthesiser synth_;
    double sampleRate_ = 44100.0;
    float currentWeightGain_ = 0.0f;
    float targetWeightGain_ = 0.0f;
    float weightGainStep_ = 0.0f;
    int weightGainSamplesRemaining_ = 0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SnaralAudioProcessor)
};
