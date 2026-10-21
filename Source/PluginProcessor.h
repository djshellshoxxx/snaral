#pragma once

#include <JuceHeader.h>

#include "Core/MidiLearn.h"
#include "Core/ReferenceTone.h"
#include "Engine/CcHook.h"
#include "Engine/EngineDiagnostics.h"
#include "Engine/SnaralEngine.h"
#include "Parameters/JuceParameterLayout.h"
#include "Parameters/ParameterTable.h"
#include "State/ProjectState.h"
#include "State/Pure/PresetBrowserModel.h"
#include "State/Pure/PresetLibrary.h"
#include "State/Pure/UndoHistory.h"
#include "State/Pure/VariationBank.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

/// Host-facing processor. Owns the APVTS (built from snaral::createParameterLayout), the JUCE-free SnaralEngine and the
/// message-thread services (preset library, undo history, variation bank, A/B compare, MIDI-learn map publisher).
///
/// Threading: processBlock runs on the audio thread and touches only atomics, the engine and audio-thread members.
/// Everything documented "message thread" below must not be called from the audio thread.
///
/// MIDI learn (interface for the UI/services side; transport is snaral::CcMapPublisher / CcRouter from Engine/CcHook.h):
///   1. message thread: ccMapPublisher().publish (maps, n) installs the CC -> parameter table (validated, <= 16 entries).
///   2. message thread: ccRouter().setLearnActive (true); poll ccRouter().seenQueue().pop (CcSeen&) to see incoming CCs.
///   3. audio thread (processBlock): non-reserved CCs are offered to the router; a mapped CC is consumed (not forwarded to
///      the engine) and overrides the parameter for the next blocks until the parameter itself moves.
///   4. message thread: pumpMidiLearn() (called by an internal 30 Hz timer when a message loop exists, or manually) drains
///      ccRouter().appliedQueue() and mirrors each value to the host parameter via setValueNotifyingHost.
class SnaralAudioProcessor final : public juce::AudioProcessor, private juce::Timer
{
public:
    SnaralAudioProcessor();
    ~SnaralAudioProcessor() override;

    juce::AudioProcessorValueTreeState& getParameters() noexcept { return parameters_; }
    const juce::AudioProcessorValueTreeState& getParameters() const noexcept { return parameters_; }

    // ---- juce::AudioProcessor ----
    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;
    juce::AudioProcessorParameter* getBypassParameter() const override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- UI-facing API ----
    /// Any thread. Voices ramp to silence in 5 ms and effect tails are flushed at the start of the next block.
    void panic() noexcept { panicRequested_.store (true, std::memory_order_release); }

    /// Lock-free meters, scope tap, voice count, CPU load, tempo flags, latency (any thread).
    const snaral::EngineDiagnostics& diagnostics() const noexcept { return engine_.diagnostics(); }

    /// Message-thread services owned by the processor (not thread safe; message thread only).
    snaral::PresetLibrary& presetLibrary() noexcept { return presetLibrary_; }
    snaral::UndoHistory& undoHistory() noexcept { return undoHistory_; }
    snaral::VariationBank& variationBank() noexcept { return variations_; }
    snaral::AbCompare& abCompare() noexcept { return ab_; }

    /// Loaded-preset bookkeeping stored in the project state (message thread).
    struct PresetInfo { std::string name, id; bool modified = false; };
    PresetInfo& presetInfo() noexcept { return presetInfo_; }
    /// Opaque state subtrees owned by the MIDI-learn feature and the editor (message thread).
    juce::ValueTree& midiLearnTree() noexcept { return midiLearnTree_; }
    juce::ValueTree& uiTree() noexcept { return uiTree_; }

    /// MIDI-learn transport (see class comment).
    snaral::CcMapPublisher& ccMapPublisher() noexcept { return ccPublisher_; }
    snaral::CcRouter& ccRouter() noexcept { return ccRouter_; }
    void pumpMidiLearn();

    /// Message thread: applies a latency change requested by the audio thread (also done by the internal timer).
    /// Returns true if setLatencySamples was called.
    bool applyPendingLatency();

    // ---- editor-facing additions (message thread unless stated) ----
    /// Preset browser model shared by every editor instance (loaded preset, dirty flag, audition state).
    snaral::PresetBrowserModel& presetBrowser() noexcept { return presetBrowser_; }

    /// Publishes `snapshot` to the host parameters as ONE block-boundary update (restore-sequence guard). With
    /// soundOnly (default) utility parameters (bypass, solo, mutes) keep their current values.
    void applySoundSnapshot (const snaral::ParamSnapshot& snapshot, bool soundOnly = true);

    /// Plays or releases a note from the UI (preset audition, virtual keys). Message thread; consumed by the next block.
    void queueUiNote (int note, int velocity, bool on) noexcept;

    /// Standalone 440 Hz reference tone (refuses to enable in plug-in mode). The editor's Panic action also calls referenceTone().stop().
    snaral::ReferenceTone& referenceTone() noexcept { return *referenceTone_; }

    /// MIDI-learn mappings (stored as text in the MIDILEARN child of the project state and published to the audio
    /// thread). setMidiMappings validates the whole list and changes nothing on failure.
    std::vector<snaral::midi::MidiMapping> midiMappings() const;
    bool setMidiMappings (const std::vector<snaral::midi::MidiMapping>& mappings, std::string* error = nullptr);

    /// Runs `fn` with state restore excluded (used by state restore and tests); see ProjectState.h.
    std::uint64_t restoreSequence() const noexcept { return stateRestoreSequence_.load (std::memory_order_acquire); }

private:
    void timerCallback() override;
    void readParameters (snaral::ParamSnapshot& out) noexcept;
    void handleMidiAndRender (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi, int numSamples);

    juce::AudioProcessorValueTreeState parameters_;
    std::array<std::atomic<float>*, static_cast<std::size_t> (snaral::parameterCount)> rawValues_ {};

    void publishMidiMappingsFromTree();

    snaral::SnaralEngine engine_;
    snaral::ParamSnapshot snapshot_;      // last coherent snapshot (audio thread)
    snaral::ParamSnapshot candidate_;     // scratch (audio thread)
    snaral::CcMapPublisher ccPublisher_;
    snaral::CcRouter ccRouter_;
    std::vector<snaral::EngineMidiEvent> midiEvents_; // reserved in prepareToPlay, capacity fixed
    std::vector<int> midiOffsets_;
    bool wasPlaying_ = false;
    bool pendingTransportStop_ = false;
    bool prepared_ = false;
    double sampleRate_ = 44100.0;

    struct UiNote { std::uint8_t note = 0, velocity = 0; bool on = false; };
    snaral::SpscRing<UiNote, 64> uiNotes_;
    std::unique_ptr<snaral::ReferenceTone> referenceTone_;

    std::atomic<bool> panicRequested_ { false };
    std::atomic<int> requestedLatency_ { 0 };
    std::atomic<bool> latencyDirty_ { false };
    int engineLatency_ = 0;               // audio thread: last latency reported by the engine

    std::atomic<std::uint64_t> stateRestoreSequence_ { 0 };
    std::mutex stateMutex_;

    snaral::PresetLibrary presetLibrary_;
    snaral::PresetBrowserModel presetBrowser_ { presetLibrary_ };
    snaral::UndoHistory undoHistory_;
    snaral::VariationBank variations_;
    snaral::AbCompare ab_;
    PresetInfo presetInfo_;
    juce::ValueTree midiLearnTree_ { snaral::project::midiLearnType };
    juce::ValueTree uiTree_ { snaral::project::uiType };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SnaralAudioProcessor)
};
