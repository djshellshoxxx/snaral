#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters/MacroMappings.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace
{
constexpr auto stateTreeType = "SnaralState";
constexpr auto stateVersionProperty = "schemaVersion";
constexpr auto maximumStateBytes = 64 * 1024;
constexpr auto parameterSmoothingSeconds = 0.01;
}

SnaralAudioProcessor::SnaralAudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
    , parameters_ (*this, nullptr, stateTreeType, createParameterLayout())
{
    keyboardState_.addListener (this);
    uiMidiBuffer_.ensureSize (32768);
    for (std::size_t i = 0; i < snaral::macroParameterSpecs.size(); ++i)
    {
        const auto& spec = snaral::macroParameterSpecs[i];
        const auto parameterID = juce::String::fromUTF8 (spec.id.data(), static_cast<int> (spec.id.size()));
        rawMacroValues_[i] = parameters_.getRawParameterValue (parameterID);
        activeMacroValues_[i] = spec.defaultNormalized();
    }

    for (int i = 0; i < 8; ++i)
        synth_.addVoice (new Voice());
    synth_.addSound (new Sound());
}

juce::AudioProcessorValueTreeState::ParameterLayout SnaralAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (const auto& spec : snaral::macroParameterSpecs)
    {
        const auto parameterID = juce::String::fromUTF8 (spec.id.data(), static_cast<int> (spec.id.size()));
        const auto displayName = juce::String::fromUTF8 (spec.displayName.data(), static_cast<int> (spec.displayName.size()));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { parameterID, 1 }, displayName,
            juce::NormalisableRange<float> { spec.minimum, spec.maximum }, spec.defaultValue,
            juce::AudioParameterFloatAttributes().withLabel ("%")));
    }
    return layout;
}

void SnaralAudioProcessor::prepareToPlay (double sampleRate, int)
{
    synth_.setCurrentPlaybackSampleRate (sampleRate);
    sampleRate_ = std::isfinite (sampleRate) && sampleRate > 1.0 ? sampleRate : 44100.0;
    refreshMacroSnapshot();
    currentWeightGain_ = snaral::weightSubMultiplier (activeMacroValues_[0]);
    targetWeightGain_ = currentWeightGain_;
    weightGainStep_ = 0.0f;
    weightGainSamplesRemaining_ = 0;
}

void SnaralAudioProcessor::releaseResources() {}

bool SnaralAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto output = layouts.getMainOutputChannelSet();
    return output == juce::AudioChannelSet::mono() || output == juce::AudioChannelSet::stereo();
}

void SnaralAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    refreshMacroSnapshot();
    buffer.clear();
    if (uiMidiOverflow_.exchange (false, std::memory_order_acq_rel))
    {
        for (int channel = 1; channel <= 16; ++channel)
            synth_.allNotesOff (channel, false);
        uiMidiRead_.store (uiMidiWrite_.load (std::memory_order_acquire), std::memory_order_release);
    }
    synth_.renderNextBlock (buffer, midi, 0, buffer.getNumSamples());

    uiMidiBuffer_.clear();
    auto read = uiMidiRead_.load (std::memory_order_relaxed);
    const auto write = uiMidiWrite_.load (std::memory_order_acquire);
    while (read != write)
    {
        const auto event = uiMidiEvents_[read % uiMidiEvents_.size()];
        juce::MidiMessage message;
        if (event.type == UiMidiEvent::Type::pitchBend)
        {
            const auto semitones = juce::jlimit (-2.0f, 2.0f, event.value);
            const auto wheel = semitones < 0.0f
                                   ? 8192 + juce::roundToInt (semitones * 4096.0f)
                                   : 8192 + juce::roundToInt (semitones * 4095.5f);
            message = juce::MidiMessage::pitchWheel (event.channel, wheel);
        }
        else if (event.type == UiMidiEvent::Type::noteOn)
            message = juce::MidiMessage::noteOn (event.channel, event.note, event.value);
        else
            message = juce::MidiMessage::noteOff (event.channel, event.note);
        uiMidiBuffer_.addEvent (message, 0);
        ++read;
    }
    uiMidiRead_.store (read, std::memory_order_release);
    synth_.renderNextBlock (buffer, uiMidiBuffer_, 0, buffer.getNumSamples());

    // The current sine-only bootstrap is treated as the temporary sub signal.
    // Weight will remain sub-only when the separate mid engine is introduced.
    const auto nextWeightGain = snaral::weightSubMultiplier (activeMacroValues_[0]);
    if (nextWeightGain != targetWeightGain_)
    {
        targetWeightGain_ = nextWeightGain;
        weightGainSamplesRemaining_ = juce::jmax (1, static_cast<int> (sampleRate_ * parameterSmoothingSeconds));
        weightGainStep_ = (targetWeightGain_ - currentWeightGain_) / static_cast<float> (weightGainSamplesRemaining_);
    }

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        if (weightGainSamplesRemaining_ > 0)
        {
            currentWeightGain_ += weightGainStep_;
            if (--weightGainSamplesRemaining_ == 0)
                currentWeightGain_ = targetWeightGain_;
        }

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            buffer.setSample (channel, sample, buffer.getSample (channel, sample) * currentWeightGain_);

        const auto writeCount = visualizationWriteCount_.load (std::memory_order_relaxed);
        const auto monoSample = buffer.getNumChannels() > 0 ? buffer.getSample (0, sample) : 0.0f;
        visualizationSamples_[static_cast<std::size_t> (writeCount % visualizationSamples_.size())]
            .store (monoSample, std::memory_order_relaxed);
        visualizationWriteCount_.store (writeCount + 1, std::memory_order_release);
    }
}

void SnaralAudioProcessor::handleNoteOn (juce::MidiKeyboardState*, int channel, int note, float velocity)
{
    recordUiEvent ({ channel, note, velocity, UiMidiEvent::Type::noteOn });
    enqueueUiNote (true, channel, note, velocity);
}

void SnaralAudioProcessor::handleNoteOff (juce::MidiKeyboardState*, int channel, int note, float velocity)
{
    recordUiEvent ({ channel, note, velocity, UiMidiEvent::Type::noteOff });
    enqueueUiNote (false, channel, note, velocity);
}

void SnaralAudioProcessor::enqueueUiNote (bool noteOn, int channel, int note, float velocity) noexcept
{
    const auto write = uiMidiWrite_.load (std::memory_order_relaxed);
    const auto read = uiMidiRead_.load (std::memory_order_acquire);
    if (write - read >= uiMidiEvents_.size())
    {
        uiMidiOverflow_.store (true, std::memory_order_release);
        return;
    }
    uiMidiEvents_[write % uiMidiEvents_.size()] = { channel, note, velocity,
                                                    noteOn ? UiMidiEvent::Type::noteOn : UiMidiEvent::Type::noteOff };
    uiMidiWrite_.store (write + 1, std::memory_order_release);
}

void SnaralAudioProcessor::enqueueUiPitchBend (float semitones) noexcept
{
    const auto write = uiMidiWrite_.load (std::memory_order_relaxed);
    const auto read = uiMidiRead_.load (std::memory_order_acquire);
    if (write - read >= uiMidiEvents_.size())
    {
        uiMidiOverflow_.store (true, std::memory_order_release);
        return;
    }
    const auto bend = juce::jlimit (-2.0f, 2.0f, semitones);
    const auto event = UiMidiEvent { 1, 0, bend, UiMidiEvent::Type::pitchBend };
    recordUiEvent (event);
    uiMidiEvents_[write % uiMidiEvents_.size()] = event;
    uiMidiWrite_.store (write + 1, std::memory_order_release);
}

void SnaralAudioProcessor::recordUiEvent (const UiMidiEvent& event) noexcept
{
    constexpr std::uint32_t historyCapacity = 4096;
    const auto index = recordedUiEventCount_.fetch_add (1, std::memory_order_relaxed);
    auto value = juce::jlimit (0, 127, juce::roundToInt (event.value * 127.0f));
    if (event.type == UiMidiEvent::Type::pitchBend)
        value = juce::jlimit (0, 127, juce::roundToInt ((event.value + 2.0f) * 31.75f));
    const auto type = static_cast<std::uint64_t> (event.type);
    const auto millis = static_cast<std::uint32_t> (juce::Time::getMillisecondCounter());
    const auto packed = (static_cast<std::uint64_t> (millis) << 20)
                      | (type << 18)
                      | (static_cast<std::uint64_t> (value) << 11)
                      | (static_cast<std::uint64_t> (juce::jlimit (1, 16, event.channel) - 1) << 7)
                      | static_cast<std::uint64_t> (juce::jlimit (0, 127, event.note));
    recordedUiEvents_[index % historyCapacity].store (packed, std::memory_order_relaxed);
}

juce::MidiMessageSequence SnaralAudioProcessor::createRecordedMidiSequence() const
{
    juce::MidiMessageSequence sequence;
    constexpr std::uint32_t historyCapacity = 4096;
    const auto end = recordedUiEventCount_.load (std::memory_order_acquire);
    const auto begin = end > historyCapacity ? end - historyCapacity : 0;
    std::uint32_t firstMillis = 0;
    bool hasTimestamp = false;
    for (auto index = begin; index < end; ++index)
    {
        const auto packed = recordedUiEvents_[index % historyCapacity].load (std::memory_order_relaxed);
        const auto millis = static_cast<std::uint32_t> (packed >> 20);
        if (! hasTimestamp) { firstMillis = millis; hasTimestamp = true; }
        const auto elapsedSeconds = static_cast<double> (static_cast<std::uint32_t> (millis - firstMillis)) / 1000.0;
        const auto type = static_cast<unsigned> ((packed >> 18) & 0x3U);
        const auto value = static_cast<int> ((packed >> 11) & 0x7fU);
        const auto channel = static_cast<int> ((packed >> 7) & 0xfU) + 1;
        const auto note = static_cast<int> (packed & 0x7fU);
        juce::MidiMessage message;
        if (type == static_cast<unsigned> (UiMidiEvent::Type::noteOn))
            message = juce::MidiMessage::noteOn (channel, note, static_cast<juce::uint8> (value));
        else if (type == static_cast<unsigned> (UiMidiEvent::Type::noteOff))
            message = juce::MidiMessage::noteOff (channel, note);
        else
        {
            const auto bend = value < 64 ? value * 8192 / 64 : 8192 + (value - 64) * 8191 / 63;
            message = juce::MidiMessage::pitchWheel (channel, bend);
        }
        sequence.addEvent (message.withTimeStamp (elapsedSeconds));
    }
    sequence.sort();
    return sequence;
}

void SnaralAudioProcessor::copyVisualizationSamples (std::array<float, 512>& destination) const noexcept
{
    const auto end = visualizationWriteCount_.load (std::memory_order_acquire);
    const auto begin = end > destination.size() ? end - destination.size() : 0;
    const auto available = static_cast<std::size_t> (end - begin);
    const auto padding = destination.size() - available;
    std::fill (destination.begin(), destination.begin() + static_cast<std::ptrdiff_t> (padding), 0.0f);
    for (std::size_t i = 0; i < available; ++i)
        destination[padding + i] = visualizationSamples_[(begin + i) % visualizationSamples_.size()]
                                       .load (std::memory_order_relaxed);
}

void SnaralAudioProcessor::refreshMacroSnapshot() noexcept
{
    // Keep the previous coherent values while a validated APVTS state restore is in progress.
    const auto sequenceBefore = stateRestoreSequence_.load (std::memory_order_acquire);
    if ((sequenceBefore & 1U) != 0U)
        return;

    std::array<float, snaral::macroParameterSpecs.size()> candidate {};
    for (std::size_t i = 0; i < rawMacroValues_.size(); ++i)
    {
        const auto rawValue = rawMacroValues_[i] != nullptr
                                  ? rawMacroValues_[i]->load (std::memory_order_relaxed)
                                  : snaral::macroParameterSpecs[i].defaultValue;
        candidate[i] = snaral::macroParameterSpecs[i].domainToNormalized (rawValue);
    }

    const auto sequenceAfter = stateRestoreSequence_.load (std::memory_order_acquire);
    if (sequenceBefore == sequenceAfter && (sequenceAfter & 1U) == 0U)
        activeMacroValues_ = candidate;
}

bool SnaralAudioProcessor::Voice::canPlaySound (juce::SynthesiserSound* sound)
{
    return dynamic_cast<Sound*> (sound) != nullptr;
}

void SnaralAudioProcessor::Voice::startNote (int midiNote, float velocity, juce::SynthesiserSound*, int)
{
    oscillator_.prepare (getSampleRate());
    oscillator_.noteOn (midiNote, velocity);
}

void SnaralAudioProcessor::Voice::stopNote (float, bool allowTailOff)
{
    if (allowTailOff)
        oscillator_.noteOff (0.08);
    else
        oscillator_.noteOff (0.0);
    if (! oscillator_.isActive())
        clearCurrentNote();
}

void SnaralAudioProcessor::Voice::renderNextBlock (juce::AudioBuffer<float>& output, int startSample, int numSamples)
{
    for (int sample = 0; sample < numSamples; ++sample)
    {
        const auto value = oscillator_.renderSample();
        for (int channel = 0; channel < output.getNumChannels(); ++channel)
            output.addSample (channel, startSample + sample, value);
        if (! oscillator_.isActive())
        {
            clearCurrentNote();
            break;
        }
    }
}

juce::AudioProcessorEditor* SnaralAudioProcessor::createEditor()
{
    return new SnaralAudioProcessorEditor (*this);
}

void SnaralAudioProcessor::getStateInformation (juce::MemoryBlock& destination)
{
    const std::lock_guard<std::mutex> lock (stateMutex_);
    auto state = parameters_.copyState();
    state.setProperty (stateVersionProperty, 1, nullptr);

    if (const auto xml = state.createXml())
        copyXmlToBinary (*xml, destination);
    else
        destination.reset();
}

bool SnaralAudioProcessor::parseStateValue (const juce::var& input, float& output)
{
    const auto text = input.toString().toStdString();
    if (text.empty())
        return false;

    char* end = nullptr;
    const auto parsed = std::strtof (text.c_str(), &end);
    if (end == text.c_str() || end == nullptr || *end != '\0' || ! std::isfinite (parsed))
        return false;

    output = parsed;
    return true;
}

bool SnaralAudioProcessor::validateStateTree (const juce::ValueTree& candidate)
{
    if (! candidate.hasType (stateTreeType)
        || candidate.getProperty (stateVersionProperty).toString() != "1"
        || candidate.getNumChildren() != static_cast<int> (snaral::macroParameterSpecs.size()))
        return false;

    snaral::MacroStateCandidate values;
    for (const auto child : candidate)
    {
        if (! child.hasType ("PARAM"))
            return false;

        const auto id = child.getProperty ("id").toString().toStdString();
        float value = 0.0f;
        if (! parseStateValue (child.getProperty ("value"), value) || ! values.assign (id, value))
            return false;
    }

    return values.isComplete();
}

void SnaralAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0 || sizeInBytes > maximumStateBytes)
        return;

    const auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr)
        return;

    const auto candidate = juce::ValueTree::fromXml (*xml);
    if (! validateStateTree (candidate))
        return;

    const std::lock_guard<std::mutex> lock (stateMutex_);
    struct RestoreSequenceGuard
    {
        explicit RestoreSequenceGuard (std::atomic<std::uint64_t>& sequenceIn) noexcept : sequence (sequenceIn)
        {
            sequence.fetch_add (1, std::memory_order_acq_rel);
        }

        ~RestoreSequenceGuard()
        {
            sequence.fetch_add (1, std::memory_order_release);
        }

        std::atomic<std::uint64_t>& sequence;
    } sequenceGuard (stateRestoreSequence_);

    parameters_.replaceState (candidate);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SnaralAudioProcessor();
}
