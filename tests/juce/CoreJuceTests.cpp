// JUCE-dependent checks for Source/Core: (1) files written by wav::WavFileWriter are decoded by JUCE's own
// WavAudioFormat reader (an independent implementation), (2) the MIDI-learn map survives a juce::ValueTree /
// XML project-state round trip and malformed maps are rejected. Plain main(), no GUI.

#include <cmath>
#include <iostream>

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_data_structures/juce_data_structures.h>

#include "Core/MidiLearn.h"
#include "Core/WavWriter.h"
#include "../TestSupport.h"

using namespace snaral;
using namespace snaral::testing;

namespace
{
void testJuceReadsOurWav()
{
    juce::AudioFormatManager mgr;
    mgr.registerBasicFormats();
    TempDir dir;
    const int frames = 12345;
    struct Case { wav::SampleFormat f; int bits; bool isFloat; };
    for (Case c : { Case { wav::SampleFormat::Pcm16, 16, false }, Case { wav::SampleFormat::Pcm24, 24, false }, Case { wav::SampleFormat::Float32, 32, true } })
        for (int rate : { 44100, 48000, 96000, 192000 })
            for (int chans : { 1, 2 })
            {
                std::vector<std::vector<float>> d (static_cast<std::size_t> (chans), std::vector<float> (frames));
                for (int ch = 0; ch < chans; ++ch)
                    for (int i = 0; i < frames; ++i)
                        d[static_cast<std::size_t> (ch)][static_cast<std::size_t> (i)] = 0.6f * std::sin (0.01f * static_cast<float> (i) * static_cast<float> (ch + 1));
                const float* ptr[2] = { d[0].data(), d.back().data() };
                const auto path = dir.path() / "j.wav";
                CHECK (wav::writeWavFile (path, { rate, chans, c.f }, ptr, frames, { "Snaral 0.2.0", "Preset A", "2026-10-20" }).status == wav::Status::Ok);
                std::unique_ptr<juce::AudioFormatReader> reader (mgr.createReaderFor (juce::File (path.string())));
                CHECK (reader != nullptr);
                CHECK (reader->sampleRate == rate && static_cast<int> (reader->numChannels) == chans && reader->lengthInSamples == frames);
                CHECK (static_cast<int> (reader->bitsPerSample) == c.bits && reader->usesFloatingPointData == c.isFloat);
                juce::AudioBuffer<float> buf (chans, frames);
                CHECK (reader->read (&buf, 0, frames, 0, true, true));
                const float tol = c.bits == 16 ? 1.5f / 32768.0f : (c.bits == 24 ? 1.0f / 8388608.0f : 0.0f);
                for (int ch = 0; ch < chans; ++ch)
                    for (int i = 0; i < frames; i += 11)
                        CHECK (std::abs (buf.getSample (ch, i) - d[static_cast<std::size_t> (ch)][static_cast<std::size_t> (i)]) <= tol + 1e-7f);
                // metadata present
                const auto md = reader->metadataValues;
                bool found = false;
                for (auto& k : md.getAllKeys()) found |= md[k].contains ("Snaral 0.2.0");
                CHECK (found);
            }
}

juce::ValueTree toTree (const midi::MidiMapData& data)
{
    juce::ValueTree t ("MIDI_MAP");
    for (const auto& m : data.mappings)
    {
        juce::ValueTree e ("MAP");
        e.setProperty ("target", juce::String (std::string (parameterTable[static_cast<std::size_t> (m.target)].id)), nullptr);
        e.setProperty ("cc", m.cc, nullptr);
        e.setProperty ("channel", m.channel, nullptr);
        e.setProperty ("min", m.min, nullptr);
        e.setProperty ("max", m.max, nullptr);
        e.setProperty ("invert", m.invert, nullptr);
        t.appendChild (e, nullptr);
    }
    return t;
}

bool fromTree (const juce::ValueTree& t, midi::MidiMapData& out)
{
    out = {};
    for (auto e : t)
    {
        midi::MidiMapping m;
        m.target = findParameterIndex (e["target"].toString().toStdString());
        m.cc = static_cast<int> (e["cc"]);
        m.channel = static_cast<int> (e["channel"]);
        m.min = static_cast<float> (static_cast<double> (e["min"]));
        m.max = static_cast<float> (static_cast<double> (e["max"]));
        m.invert = static_cast<bool> (e["invert"]);
        out.mappings.push_back (m);
    }
    return midi::validateMappings (out.mappings).empty();
}

void testMidiMapValueTree()
{
    midi::MidiMapData d;
    midi::MidiMapping a; a.target = P::bite; a.cc = 74;
    midi::MidiMapping b; b.target = P::weight; b.cc = 1; b.channel = 5; b.min = 0.1f; b.max = 0.9f; b.invert = true;
    d.mappings = { a, b };

    // ValueTree -> XML -> ValueTree
    const auto xml = toTree (d).toXmlString();
    const auto back = juce::ValueTree::fromXml (xml);
    midi::MidiMapData got;
    CHECK (fromTree (back, got) && got.mappings.size() == 2);
    CHECK (got.mappings[1].channel == 5 && got.mappings[1].invert && std::abs (got.mappings[1].min - 0.1f) < 1e-6f);

    // text form stored as one string property survives XML too
    juce::ValueTree holder ("MIDILEARN");
    holder.setProperty ("map", juce::String (midi::toText (d)), nullptr);
    const auto restored = juce::ValueTree::fromXml (holder.toXmlString());
    const auto parsed = midi::fromText (restored["map"].toString().toStdString());
    CHECK (parsed.ok && parsed.data.mappings.size() == 2 && parsed.data.mappings[0] == a);

    // malformed maps are rejected: duplicate pair, bad target, 17th entry, min > max
    auto bad = toTree (d);
    bad.getChild (1).setProperty ("target", "bite", nullptr);
    CHECK (! fromTree (bad, got));
    bad = toTree (d); bad.getChild (1).setProperty ("cc", 74, nullptr); bad.getChild (1).setProperty ("channel", 0, nullptr);
    CHECK (! fromTree (bad, got));
    bad = toTree (d); bad.getChild (1).setProperty ("target", "not_a_param", nullptr);
    CHECK (! fromTree (bad, got));
    bad = toTree (d); bad.getChild (1).setProperty ("min", 0.95, nullptr);
    CHECK (! fromTree (bad, got));
    bad = toTree (d);
    const auto ids = midi::learnableParameters();
    for (int i = 2; i < 17; ++i)
    {
        juce::ValueTree e ("MAP");
        e.setProperty ("target", juce::String (std::string (parameterTable[static_cast<std::size_t> (ids[static_cast<std::size_t> (i + 3)])].id)), nullptr);
        e.setProperty ("cc", 10 + i, nullptr); e.setProperty ("channel", 0, nullptr);
        e.setProperty ("min", 0.0, nullptr); e.setProperty ("max", 1.0, nullptr); e.setProperty ("invert", false, nullptr);
        bad.appendChild (e, nullptr);
    }
    CHECK (bad.getNumChildren() == 17 && ! fromTree (bad, got));
}
} // namespace

int main()
{
    testJuceReadsOurWav();
    testMidiMapValueTree();
    std::cout << "CoreJuceTests passed\n";
    return 0;
}
