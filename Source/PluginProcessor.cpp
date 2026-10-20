#include "PluginProcessor.h"
#include "PluginEditor.h"

SnaralAudioProcessor::SnaralAudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    for (int i = 0; i < 8; ++i)
        synth_.addVoice (new Voice());
    synth_.addSound (new Sound());
}

void SnaralAudioProcessor::prepareToPlay (double sampleRate, int)
{
    synth_.setCurrentPlaybackSampleRate (sampleRate);
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
    buffer.clear();
    synth_.renderNextBlock (buffer, midi, 0, buffer.getNumSamples());
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
    destination.reset();
}

void SnaralAudioProcessor::setStateInformation (const void*, int) {}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SnaralAudioProcessor();
}
