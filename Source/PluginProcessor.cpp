#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <algorithm>
#include <cmath>

namespace
{
constexpr std::size_t maxMidiEventsPerBlock = 4096;

snaral::EngineMidiEvent makeEvent (snaral::EngineMidiEvent::Type type, const juce::MidiMessage& m) noexcept
{
    snaral::EngineMidiEvent e;
    e.type = type;
    e.channel = static_cast<std::uint8_t> (juce::jlimit (0, 15, m.getChannel() - 1));
    return e;
}
} // namespace

SnaralAudioProcessor::SnaralAudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
    , parameters_ (*this, nullptr, snaral::project::rootType, snaral::createParameterLayout())
    , presetLibrary_ (snaral::LibraryPaths::platformDefault())
{
    for (std::size_t i = 0; i < rawValues_.size(); ++i)
    {
        const auto id = snaral::parameterTable[i].id;
        rawValues_[i] = parameters_.getRawParameterValue (juce::String::fromUTF8 (id.data(), static_cast<int> (id.size())));
    }
    for (std::size_t i = 0; i < rawValues_.size(); ++i)
        snapshot_.v[i] = rawValues_[i] != nullptr ? rawValues_[i]->load() : snaral::parameterTable[i].defaultValue;
    candidate_ = snapshot_;

    midiEvents_.reserve (maxMidiEventsPerBlock + 1);
    undoHistory_.reset (snapshot_);

    // The timer applies latency changes and mirrors MIDI-learn CCs on the message thread. It only exists when a message
    // loop is already running (hosts, standalone); headless users call applyPendingLatency() / pumpMidiLearn() themselves.
    if (juce::MessageManager::getInstanceWithoutCreating() != nullptr && juce::MessageManager::getInstance()->isThisTheMessageThread())
        startTimerHz (30);
}

SnaralAudioProcessor::~SnaralAudioProcessor()
{
    stopTimer();
}

juce::AudioProcessorParameter* SnaralAudioProcessor::getBypassParameter() const
{
    return parameters_.getParameter ("bypass");
}

void SnaralAudioProcessor::readParameters (snaral::ParamSnapshot& out) noexcept
{
    snaral::readSnapshot (rawValues_, out);
}

void SnaralAudioProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    sampleRate_ = std::isfinite (sampleRate) && sampleRate > 1.0 ? sampleRate : 44100.0;
    engine_.prepare (sampleRate_, juce::jmax (1, maximumExpectedSamplesPerBlock));
    ccRouter_.reset();
    wasPlaying_ = false;

    const auto before = stateRestoreSequence_.load (std::memory_order_acquire);
    readParameters (candidate_);
    if ((before & 1U) == 0U && before == stateRestoreSequence_.load (std::memory_order_acquire))
        snapshot_ = candidate_;
    engine_.setParameters (snapshot_);

    engineLatency_ = engine_.latencySamples();
    latencyDirty_.store (false);
    requestedLatency_.store (engineLatency_);
    setLatencySamples (engineLatency_);
    prepared_ = true;
}

void SnaralAudioProcessor::releaseResources()
{
    prepared_ = false;
}

bool SnaralAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto output = layouts.getMainOutputChannelSet();
    return output == juce::AudioChannelSet::mono() || output == juce::AudioChannelSet::stereo();
}

double SnaralAudioProcessor::getTailLengthSeconds() const
{
    // Longest amplitude release plus the output chain latency and a margin for the mid-bus effects.
    const auto load = [this] (snaral::P::Index i) { auto* p = rawValues_[static_cast<std::size_t> (i)]; return p != nullptr ? static_cast<double> (p->load()) : 0.0; };
    const auto releaseMs = std::max (load (snaral::P::sub_amp_release_ms), load (snaral::P::mid_amp_release_ms));
    const auto latency = static_cast<double> (requestedLatency_.load (std::memory_order_relaxed)) / sampleRate_;
    return std::max (0.0, releaseMs) * 0.001 + latency + 0.1;
}

void SnaralAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    if (numChannels <= 0 || numSamples <= 0)
    {
        midi.clear();
        return;
    }
    if (! prepared_)
    {
        buffer.clear();
        return;
    }

    // 1) parameters: keep the previous coherent snapshot while a validated state restore is publishing
    const auto seqBefore = stateRestoreSequence_.load (std::memory_order_acquire);
    if ((seqBefore & 1U) == 0U)
    {
        readParameters (candidate_);
        const auto seqAfter = stateRestoreSequence_.load (std::memory_order_acquire);
        if (seqAfter == seqBefore)
            snapshot_ = candidate_;
    }
    ccRouter_.refresh (ccPublisher_);
    auto effective = snapshot_;
    ccRouter_.applyOverrides (effective);
    engine_.setParameters (effective);

    // 2) latency: request a change for the message thread (lock-free flag)
    const int latency = engine_.latencySamples();
    if (latency != engineLatency_)
    {
        engineLatency_ = latency;
        requestedLatency_.store (latency, std::memory_order_relaxed);
        latencyDirty_.store (true, std::memory_order_release);
    }

    // 3) host timing and transport
    snaral::HostTiming timing;
    bool playing = false;
    if (auto* head = getPlayHead())
    {
        if (const auto pos = head->getPosition())
        {
            playing = pos->getIsPlaying();
            if (const auto bpm = pos->getBpm())
            {
                timing.bpm = *bpm;
                timing.bpmValid = std::isfinite (*bpm) && *bpm > 0.0;
            }
            if (const auto ppq = pos->getPpqPosition())
            {
                timing.ppqPosition = *ppq;
                timing.ppqValid = std::isfinite (*ppq);
            }
        }
    }
    timing.playing = playing;
    engine_.setHostTiming (timing);
    pendingTransportStop_ = wasPlaying_ && ! playing; // play -> stop: TransportStop event at sample 0 of this block
    wasPlaying_ = playing;

    // 4) panic request from the UI
    if (panicRequested_.exchange (false, std::memory_order_acq_rel))
        engine_.panic();

    handleMidiAndRender (buffer, midi, numSamples);
    midi.clear();
}

void SnaralAudioProcessor::handleMidiAndRender (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi, int numSamples)
{
    using Type = snaral::EngineMidiEvent::Type;
    midiEvents_.clear();

    if (pendingTransportStop_)
    {
        pendingTransportStop_ = false;
        snaral::EngineMidiEvent stop;
        stop.type = Type::TransportStop;
        stop.sampleOffset = 0;
        midiEvents_.push_back (stop);
    }

    std::uint32_t dropped = 0;
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        snaral::EngineMidiEvent e;
        if (m.isNoteOn())
        {
            e = makeEvent (Type::NoteOn, m);
            e.data1 = static_cast<std::uint8_t> (m.getNoteNumber() & 127);
            e.data2 = static_cast<std::uint8_t> (juce::jlimit (1, 127, static_cast<int> (m.getVelocity())));
        }
        else if (m.isNoteOff (true))
        {
            e = makeEvent (Type::NoteOff, m);
            e.data1 = static_cast<std::uint8_t> (m.getNoteNumber() & 127);
        }
        else if (m.isPitchWheel())
        {
            e = makeEvent (Type::PitchBend, m);
            e.bend14 = static_cast<std::int16_t> (juce::jlimit (-8192, 8191, m.getPitchWheelValue() - 8192));
        }
        else if (m.isController())
        {
            const int cc = m.getControllerNumber();
            const int value = m.getControllerValue();
            if (ccRouter_.handleCc (juce::jlimit (0, 15, m.getChannel() - 1), cc, value))
                continue; // consumed by a MIDI-learn mapping
            e = makeEvent (Type::ControlChange, m);
            e.data1 = static_cast<std::uint8_t> (cc & 127);
            e.data2 = static_cast<std::uint8_t> (value & 127);
        }
        else if (m.isChannelPressure())
        {
            e = makeEvent (Type::ChannelPressure, m);
            e.data2 = static_cast<std::uint8_t> (m.getChannelPressureValue() & 127);
        }
        else if (m.isAftertouch())
        {
            e = makeEvent (Type::PolyPressure, m);
            e.data1 = static_cast<std::uint8_t> (m.getNoteNumber() & 127);
            e.data2 = static_cast<std::uint8_t> (m.getAfterTouchValue() & 127);
        }
        else
            continue;

        e.sampleOffset = juce::jlimit (0, numSamples - 1, meta.samplePosition);
        if (midiEvents_.size() >= maxMidiEventsPerBlock)
        {
            ++dropped;
            continue;
        }
        midiEvents_.push_back (e);
    }
    if (dropped > 0)
        engine_.diagnostics().addDroppedMidiEvents (dropped);

    // JUCE delivers buffers in time order; a stable sort keeps the guarantee even for hosts that do not (no allocation).
    if (! std::is_sorted (midiEvents_.begin(), midiEvents_.end(),
                          [] (const auto& a, const auto& b) { return a.sampleOffset < b.sampleOffset; }))
        std::stable_sort (midiEvents_.begin(), midiEvents_.end(),
                          [] (const auto& a, const auto& b) { return a.sampleOffset < b.sampleOffset; });

    float* left = buffer.getWritePointer (0);
    float* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    for (int ch = 2; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    int pos = 0;
    for (const auto& e : midiEvents_)
    {
        if (e.sampleOffset > pos)
        {
            engine_.render (left + pos, right != nullptr ? right + pos : nullptr, e.sampleOffset - pos);
            pos = e.sampleOffset;
        }
        engine_.handleMidi (e);
    }
    if (pos < numSamples)
        engine_.render (left + pos, right != nullptr ? right + pos : nullptr, numSamples - pos);
}

bool SnaralAudioProcessor::applyPendingLatency()
{
    if (! latencyDirty_.exchange (false, std::memory_order_acq_rel))
        return false;
    const int wanted = requestedLatency_.load (std::memory_order_relaxed);
    if (wanted == getLatencySamples())
        return false;
    setLatencySamples (wanted);
    return true;
}

void SnaralAudioProcessor::pumpMidiLearn()
{
    snaral::CcApplied applied;
    int guard = 0;
    while (guard++ < 512 && ccRouter_.appliedQueue().pop (applied))
    {
        if (applied.paramIndex < 0 || applied.paramIndex >= snaral::parameterCount)
            continue;
        const auto id = snaral::parameterTable[static_cast<std::size_t> (applied.paramIndex)].id;
        if (auto* p = parameters_.getParameter (juce::String::fromUTF8 (id.data(), static_cast<int> (id.size()))))
            p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, applied.normalised));
    }
}

void SnaralAudioProcessor::timerCallback()
{
    applyPendingLatency();
    pumpMidiLearn();
}

juce::AudioProcessorEditor* SnaralAudioProcessor::createEditor()
{
    return new SnaralAudioProcessorEditor (*this);
}

void SnaralAudioProcessor::getStateInformation (juce::MemoryBlock& destination)
{
    snaral::project::ProjectModel model;
    model.params = snaral::project::snapshotFromApvts (parameters_);
    model.variations = variations_;
    model.ab = ab_;
    model.presetName = presetInfo_.name;
    model.presetId = presetInfo_.id;
    model.presetModified = presetInfo_.modified;
    model.midiLearn = midiLearnTree_;
    model.ui = uiTree_;

    const std::lock_guard<std::mutex> lock (stateMutex_);
    snaral::project::encode (model, destination);
}

void SnaralAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0 || sizeInBytes > snaral::project::maxStateBytes)
        return;

    auto result = snaral::project::decode (data, sizeInBytes);
    if (! result.ok)
        return; // current state stays intact

    const std::lock_guard<std::mutex> lock (stateMutex_);
    {
        struct RestoreSequenceGuard
        {
            explicit RestoreSequenceGuard (std::atomic<std::uint64_t>& s) noexcept : sequence (s) { sequence.fetch_add (1, std::memory_order_acq_rel); }
            ~RestoreSequenceGuard() { sequence.fetch_add (1, std::memory_order_release); }
            std::atomic<std::uint64_t>& sequence;
        } guard (stateRestoreSequence_);

        parameters_.replaceState (snaral::project::toValueTree (result.model));
    }

    variations_ = result.model.variations;
    ab_ = result.model.ab;
    presetInfo_ = { result.model.presetName, result.model.presetId, result.model.presetModified };
    midiLearnTree_ = result.model.midiLearn;
    uiTree_ = result.model.ui;
    undoHistory_.reset (result.model.params);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SnaralAudioProcessor();
}
