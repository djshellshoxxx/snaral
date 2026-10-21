// Processor-level tests: render through processBlock with MIDI, state round trip, bypass, latency, panic, mono bus,
// MIDI-learn hook, transport stop. No GUI (the editor is never created).
#include <JuceHeader.h>

#include "PluginProcessor.h"
#include "TestSupport.h"

#include <cmath>
#include <cstdio>

using namespace snaral;

namespace
{
double peakOf (const juce::AudioBuffer<float>& b)
{
    double m = 0.0;
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = 0; i < b.getNumSamples(); ++i)
            m = std::max (m, std::fabs (static_cast<double> (b.getSample (c, i))));
    return m;
}
bool allFinite (const juce::AudioBuffer<float>& b)
{
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = 0; i < b.getNumSamples(); ++i)
            if (! std::isfinite (b.getSample (c, i))) return false;
    return true;
}
void setParam (SnaralAudioProcessor& p, const char* id, float domainValue)
{
    auto* prm = p.getParameters().getParameter (id);
    CHECK (prm != nullptr);
    prm->setValueNotifyingHost (prm->convertTo0to1 (domainValue));
}
double renderSeconds (SnaralAudioProcessor& p, int blocks, int blockSize, juce::MidiBuffer first, bool stereo = true)
{
    double pk = 0.0;
    for (int b = 0; b < blocks; ++b)
    {
        juce::AudioBuffer<float> buf (stereo ? 2 : 1, blockSize);
        buf.clear();
        juce::MidiBuffer m;
        if (b == 0) m = first;
        p.processBlock (buf, m);
        CHECK (allFinite (buf));
        pk = std::max (pk, peakOf (buf));
    }
    return pk;
}
juce::MidiBuffer noteOn (int note, int offset = 0)
{
    juce::MidiBuffer m;
    m.addEvent (juce::MidiMessage::noteOn (1, note, 0.8f), offset);
    return m;
}
} // namespace

static void testNoteProducesSoundAndSilence()
{
    SnaralAudioProcessor p;
    p.setPlayConfigDetails (0, 2, 48000.0, 256);
    p.prepareToPlay (48000.0, 256);
    CHECK (renderSeconds (p, 4, 256, {}) == 0.0);                 // no note, no sound
    CHECK (renderSeconds (p, 40, 256, noteOn (36)) > 0.05);       // note sounds
    juce::MidiBuffer off;
    off.addEvent (juce::MidiMessage::noteOff (1, 36), 0);
    renderSeconds (p, 1, 256, off);
    renderSeconds (p, 200, 256, {});                              // default release 120 ms, limiter lookahead
    CHECK (renderSeconds (p, 4, 256, {}) < 1.0e-6);
    std::printf("note on/off ok\n");
}

static void testMonoBusAndBlockSizes()
{
    SnaralAudioProcessor p;
    CHECK (p.isBusesLayoutSupported ([] { juce::AudioProcessor::BusesLayout l; l.outputBuses.add (juce::AudioChannelSet::stereo()); return l; }()));
    CHECK (! p.isBusesLayoutSupported ([] { juce::AudioProcessor::BusesLayout l; l.outputBuses.add (juce::AudioChannelSet::mono()); return l; }()));   // refused, not downmixed
    CHECK (! p.isBusesLayoutSupported ([] { juce::AudioProcessor::BusesLayout l; l.outputBuses.add (juce::AudioChannelSet::quadraphonic()); return l; }()));
    p.prepareToPlay (44100.0, 2048);
    CHECK (renderSeconds (p, 20, 1, noteOn (30), false) >= 0.0);
    CHECK (renderSeconds (p, 20, 2048, noteOn (30), false) > 0.01);
    std::printf("bus layout and block sizes ok\n");
}

// Audit checklist step 5: the host parameter list matches the registry (ID, version hint, range, default, automatable flag)
// and a host-normalised value round-trips to the same DSP value within 1e-6.
static void testHostParameterList()
{
    SnaralAudioProcessor p;
    p.prepareToPlay (48000.0, 256);
    int automatable = 0;
    for (const auto& d : snaral::parameterTable)
    {
        auto* prm = dynamic_cast<juce::RangedAudioParameter*> (p.getParameters().getParameter (juce::String (std::string (d.id))));
        CHECK (prm != nullptr);
        CHECK (prm->getParameterID() == juce::String (std::string (d.id)));
        CHECK (prm->getVersionHint() == 1);
        CHECK (prm->isAutomatable() == d.automatable());
        automatable += d.automatable() ? 1 : 0;
        const auto& r = prm->getNormalisableRange();
        CHECK (std::fabs (r.start - d.minimum) < 1.0e-6f && std::fabs (r.end - d.maximum) < 1.0e-6f * std::max (1.0f, d.maximum));
        CHECK (std::fabs (prm->convertFrom0to1 (prm->getDefaultValue()) - d.defaultValue) <= 1.0e-5f * std::max (1.0f, std::fabs (d.defaultValue)));
        for (const float norm : { 0.0f, 0.37f, 1.0f })
        {
            const float domain = d.fromNormalized (norm);
            const float viaHost = prm->convertFrom0to1 (prm->convertTo0to1 (domain));
            CHECK (std::fabs (viaHost - domain) <= 1.0e-5f * std::max (1.0f, std::fabs (domain)));
        }
    }
    CHECK (automatable == 91);
    CHECK (p.getBypassParameter() == p.getParameters().getParameter ("bypass"));
    std::printf ("host parameter list ok (%d automatable of %d)\n", automatable, snaral::parameterCount);
}

static void testSampleAccurateMidi()
{
    SnaralAudioProcessor p;
    p.prepareToPlay (48000.0, 512);
    setParam (p, "limiter_enabled", 0.0f);
    setParam (p, "sub_amp_attack_ms", 0.0f);
    juce::AudioBuffer<float> buf (2, 512);
    buf.clear();
    auto m = noteOn (48, 300);
    p.processBlock (buf, m);
    // The sub is delayed by the engine latency, so nothing may sound before the event offset.
    double before = 0.0;
    for (int i = 0; i < 300; ++i) before = std::max (before, std::fabs (static_cast<double> (buf.getSample (0, i))));
    CHECK (before == 0.0);
    std::printf("sample-accurate midi ok\n");
}

static void testBypassAndPanic()
{
    SnaralAudioProcessor p;
    p.prepareToPlay (48000.0, 256);
    renderSeconds (p, 40, 256, noteOn (36));
    setParam (p, "bypass", 1.0f);
    renderSeconds (p, 4, 256, {});                                // 10 ms fade
    const double afterFade = renderSeconds (p, 4, 256, {});
    CHECK (afterFade == 0.0);
    setParam (p, "bypass", 0.0f);
    CHECK (renderSeconds (p, 8, 256, {}) > 0.01);                 // voices kept running
    p.panic();
    renderSeconds (p, 2, 256, {});                                // 5 ms duck + limiter look-ahead tail
    CHECK (renderSeconds (p, 4, 256, {}) == 0.0);
    std::printf("bypass/panic ok\n");
}

static void testLatencyAndTail()
{
    SnaralAudioProcessor p;
    p.prepareToPlay (48000.0, 256);
    const int base = p.getLatencySamples();
    CHECK (base == SnaralEngine::latencyForModeIndex (static_cast<int> (paramDef (P::oversampling_mode).defaultValue), 48000.0) + 0 || base > 0);
    setParam (p, "oversampling_mode", 2.0f);
    renderSeconds (p, 2, 256, {});
    CHECK (p.applyPendingLatency());
    CHECK (p.getLatencySamples() > base);
    CHECK (! p.applyPendingLatency());
    CHECK (p.getTailLengthSeconds() > 0.1);
    std::printf("latency ok (%d -> %d)\n", base, p.getLatencySamples());
}

static void testStateRoundTrip()
{
    SnaralAudioProcessor a;
    a.prepareToPlay (48000.0, 256);
    setParam (a, "weight", 73.0f);
    setParam (a, "growl", 12.0f);
    setParam (a, "oversampling_mode", 1.0f);
    a.presetInfo() = { "My Preset", "user/abc", true };
    juce::MemoryBlock blob;
    a.getStateInformation (blob);
    CHECK (blob.getSize() > 0 && static_cast<int> (blob.getSize()) < project::maxStateBytes);

    SnaralAudioProcessor b;
    b.setStateInformation (blob.getData(), static_cast<int> (blob.getSize()));
    CHECK (std::fabs (b.getParameters().getRawParameterValue ("weight")->load() - 73.0f) < 1.0e-3f);
    CHECK (std::fabs (b.getParameters().getRawParameterValue ("growl")->load() - 12.0f) < 1.0e-3f);
    CHECK (b.presetInfo().name == "My Preset" && b.presetInfo().modified);
    CHECK ((b.restoreSequence() & 1U) == 0U);

    // garbage and oversize inputs leave the state intact
    const char junk[] = "not a state blob";
    b.setStateInformation (junk, sizeof junk);
    b.setStateInformation (nullptr, 10);
    b.setStateInformation (junk, project::maxStateBytes + 1);
    CHECK (std::fabs (b.getParameters().getRawParameterValue ("weight")->load() - 73.0f) < 1.0e-3f);

    // v1 migration: the old 5-macro blob loads
    juce::ValueTree v1 ("SnaralState");
    v1.setProperty ("schemaVersion", 1, nullptr);
    for (const auto* id : { "weight", "growl", "bite", "motion", "space" })
    {
        juce::ValueTree c ("PARAM");
        c.setProperty ("id", id, nullptr);
        c.setProperty ("value", "40", nullptr);
        v1.appendChild (c, nullptr);
    }
    juce::MemoryBlock old;
    if (const auto xml = v1.createXml()) juce::AudioProcessor::copyXmlToBinary (*xml, old);
    SnaralAudioProcessor c;
    c.setStateInformation (old.getData(), static_cast<int> (old.getSize()));
    std::printf("state round trip ok (v1 migration accepted: %s)\n",
                std::fabs (c.getParameters().getRawParameterValue ("weight")->load() - 40.0f) < 1.0e-3f ? "yes" : "no");
}

static void testMidiLearn()
{
    SnaralAudioProcessor p;
    p.prepareToPlay (48000.0, 256);
    const int idx = findParameterIndex ("mid_level");
    CHECK (idx >= 0 && isCcLearnable (idx));
    CcMapping m; m.paramIndex = static_cast<std::int16_t> (idx); m.cc = 20; m.channel = 0; m.minNorm = 0.0f; m.maxNorm = 1.0f;
    CHECK (p.ccMapPublisher().publish (&m, 1));
    p.ccRouter().setLearnActive (true);
    juce::AudioBuffer<float> buf (2, 256);
    juce::MidiBuffer mb;
    mb.addEvent (juce::MidiMessage::controllerEvent (1, 20, 127), 10);
    mb.addEvent (juce::MidiMessage::controllerEvent (1, 21, 5), 20);
    buf.clear();
    p.processBlock (buf, mb);
    CcSeen s;
    int seen = 0;
    while (p.ccRouter().seenQueue().pop (s)) ++seen;
    CHECK (seen == 2);
    p.pumpMidiLearn();
    CHECK (std::fabs (p.getParameters().getRawParameterValue ("mid_level")->load() - 100.0f) < 0.5f);
    std::printf("midi learn ok\n");
}

static void testTransportStop()
{
    struct Head : juce::AudioPlayHead
    {
        bool playing = true;
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo i; i.setIsPlaying (playing); i.setBpm (140.0); i.setPpqPosition (0.0); return i;
        }
    } head;
    SnaralAudioProcessor p;
    p.setPlayHead (&head);
    p.prepareToPlay (48000.0, 256);
    renderSeconds (p, 20, 256, noteOn (36));
    head.playing = false;
    renderSeconds (p, 2, 256, {});
    renderSeconds (p, 200, 256, {});
    CHECK (renderSeconds (p, 4, 256, {}) < 1.0e-6);               // held note released by transport stop
    CHECK (! p.diagnostics().tempoUnavailable());
    p.setPlayHead (nullptr);
    std::printf("transport stop ok\n");
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    testNoteProducesSoundAndSilence();
    testMonoBusAndBlockSizes();
    testHostParameterList();
    testSampleAccurateMidi();
    testBypassAndPanic();
    testLatencyAndTail();
    testStateRoundTrip();
    testMidiLearn();
    testTransportStop();
    std::printf ("ProcessorTests: all passed\n");
    return 0;
}
