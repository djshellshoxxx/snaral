#include "Core/OfflineRenderer.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace snaral::render
{
double MidiPhrase::computedEndBeats() const
{
    if (endBeats >= 0.0)
        return endBeats;
    double e = 0.0;
    for (const auto& n : notes) e = std::max (e, n.startBeats + n.lengthBeats);
    for (const auto& c : ccs) e = std::max (e, c.beats);
    return e;
}

std::int64_t beatsToSample (double beats, double bpm, int sampleRate) noexcept
{
    return static_cast<std::int64_t> (std::llround (beats * 60.0 / bpm * static_cast<double> (sampleRate)));
}

std::int64_t exportFrames (const ExportSettings& s, const MidiPhrase& phrase)
{
    const double endSec = phrase.computedEndBeats() * 60.0 / s.bpm;
    return static_cast<std::int64_t> (std::llround ((endSec + s.tailSeconds) * static_cast<double> (s.sampleRate)));
}

std::string validateExport (const ExportSettings& s, const MidiPhrase& phrase, ExportStatus& status)
{
    status = ExportStatus::InvalidSettings;
    static const std::set<int> rates { 44100, 48000, 88200, 96000, 192000 };
    if (! std::isfinite (s.bpm) || s.bpm < 20.0 || s.bpm > 400.0) return "Tempo must be between 20 and 400 BPM.";
    if (rates.count (s.sampleRate) == 0) return "Sample rate must be 44100, 48000, 88200, 96000 or 192000 Hz.";
    if (s.channels != 1 && s.channels != 2) return "Channels must be mono or stereo.";
    if (! std::isfinite (s.tailSeconds) || s.tailSeconds < 0.0 || s.tailSeconds > 30.0) return "Tail must be between 0 and 30 seconds.";
    if (s.blockSize < 16 || s.blockSize > 8192) return "Block size out of range.";

    status = ExportStatus::InvalidPhrase;
    if (phrase.notes.empty()) return "The phrase contains no notes.";
    for (const auto& n : phrase.notes)
    {
        if (! std::isfinite (n.startBeats) || n.startBeats < 0.0 || ! std::isfinite (n.lengthBeats) || n.lengthBeats <= 0.0)
            return "The phrase contains a note with an invalid time.";
        if (n.note > 127 || n.velocity < 1 || n.velocity > 127 || n.channel > 15)
            return "The phrase contains a note with an invalid pitch, velocity or channel.";
    }
    for (const auto& c : phrase.ccs)
        if (! std::isfinite (c.beats) || c.beats < 0.0 || c.cc > 127 || c.value > 127 || c.channel > 15)
            return "The phrase contains an invalid controller event.";

    const double endSec = phrase.computedEndBeats() * 60.0 / s.bpm;
    if (! std::isfinite (endSec)) { status = ExportStatus::InvalidPhrase; return "The phrase length is not valid."; }
    if (endSec + s.tailSeconds > s.maxLengthSeconds)
    {
        status = ExportStatus::TooLong;
        return "The export would be longer than " + std::to_string (static_cast<int> (s.maxLengthSeconds)) + " seconds.";
    }
    status = ExportStatus::Ok;
    return {};
}

Timeline buildTimeline (const ExportSettings& s, const MidiPhrase& phrase)
{
    Timeline tl;
    const double endBeats = phrase.computedEndBeats();
    tl.phraseEndSample = beatsToSample (endBeats, s.bpm, s.sampleRate);
    tl.totalSamples = exportFrames (s, phrase);

    // order: 0 = note-off / pedal release, 1 = CC, 2 = note-on (so a same-sample retrigger works)
    int seq = 0;
    auto add = [&] (std::int64_t sample, int order, bool isCc, EngineMidiEvent ev, TimedCc cc = {}) {
        Timeline::Item it;
        it.sample = sample; it.order = order * 1000000 + seq++; it.isCc = isCc; it.ev = ev; it.cc = cc;
        tl.items.push_back (it);
    };
    for (const auto& n : phrase.notes)
    {
        EngineMidiEvent on;
        on.type = EngineMidiEvent::Type::NoteOn; on.channel = n.channel; on.data1 = n.note; on.data2 = n.velocity;
        add (beatsToSample (n.startBeats, s.bpm, s.sampleRate), 2, false, on);
        EngineMidiEvent off;
        off.type = EngineMidiEvent::Type::NoteOff; off.channel = n.channel; off.data1 = n.note; off.data2 = 0;
        const double offBeat = std::min (n.startBeats + n.lengthBeats, endBeats);
        add (std::max<std::int64_t> (beatsToSample (offBeat, s.bpm, s.sampleRate),
                                     beatsToSample (n.startBeats, s.bpm, s.sampleRate) + 1), 0, false, off);
    }
    bool pedalDown[16] = {};
    std::vector<TimedCc> ccs = phrase.ccs;
    std::stable_sort (ccs.begin(), ccs.end(), [] (const TimedCc& a, const TimedCc& b) { return a.beats < b.beats; });
    for (const auto& c : ccs)
    {
        EngineMidiEvent ev;
        ev.type = EngineMidiEvent::Type::ControlChange; ev.channel = c.channel; ev.data1 = c.cc; ev.data2 = c.value;
        add (beatsToSample (c.beats, s.bpm, s.sampleRate), 1, true, ev, c);
        if (c.cc == 64) pedalDown[c.channel] = c.value >= 64;
    }
    // Phrase end: release the sustain pedal on every channel where it is down (note-offs are already scheduled
    // no later than the end, so no note is held past it).
    for (int ch = 0; ch < 16; ++ch)
        if (pedalDown[ch])
        {
            EngineMidiEvent ev;
            ev.type = EngineMidiEvent::Type::ControlChange; ev.channel = static_cast<std::uint8_t> (ch); ev.data1 = 64; ev.data2 = 0;
            add (tl.phraseEndSample, 0, false, ev);
        }

    std::stable_sort (tl.items.begin(), tl.items.end(), [] (const Timeline::Item& a, const Timeline::Item& b) {
        return a.sample != b.sample ? a.sample < b.sample : a.order < b.order;
    });
    return tl;
}
} // namespace snaral::render
