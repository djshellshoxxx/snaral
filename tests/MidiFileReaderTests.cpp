// SMF reader tests (JUCE-free): valid type 0/1 files, running status, tempo handling, rejection set.
#include "TestSupport.h"

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "Core/MidiFileReader.h"

using namespace snaral;
using namespace snaral::midifile;
using Bytes = std::vector<std::uint8_t>;

namespace
{
void be16 (Bytes& b, unsigned v) { b.push_back (static_cast<std::uint8_t> (v >> 8)); b.push_back (static_cast<std::uint8_t> (v)); }
void be32 (Bytes& b, std::uint32_t v) { be16 (b, v >> 16); be16 (b, v & 0xFFFF); }
void tag (Bytes& b, const char* t) { for (int i = 0; i < 4; ++i) b.push_back (static_cast<std::uint8_t> (t[i])); }
void vlq (Bytes& b, std::uint32_t v)
{
    std::uint8_t tmp[5]; int n = 0;
    tmp[n++] = v & 0x7F;
    while ((v >>= 7) != 0) tmp[n++] = static_cast<std::uint8_t> ((v & 0x7F) | 0x80);
    while (n > 0) b.push_back (tmp[--n]);
}
Bytes header (int format, int tracks, unsigned division)
{
    Bytes b; tag (b, "MThd"); be32 (b, 6); be16 (b, static_cast<unsigned> (format)); be16 (b, static_cast<unsigned> (tracks)); be16 (b, division);
    return b;
}
void addTrack (Bytes& file, const Bytes& body)
{
    tag (file, "MTrk"); be32 (file, static_cast<std::uint32_t> (body.size()));
    file.insert (file.end(), body.begin(), body.end());
}
void endOfTrack (Bytes& t, std::uint32_t delta = 0) { vlq (t, delta); t.push_back (0xFF); t.push_back (0x2F); t.push_back (0); }
void tempo (Bytes& t, std::uint32_t delta, std::uint32_t micros)
{
    vlq (t, delta); t.push_back (0xFF); t.push_back (0x51); t.push_back (3);
    t.push_back (static_cast<std::uint8_t> (micros >> 16)); t.push_back (static_cast<std::uint8_t> (micros >> 8)); t.push_back (static_cast<std::uint8_t> (micros));
}
void noteOn (Bytes& t, std::uint32_t d, int ch, int n, int v) { vlq (t, d); t.push_back (static_cast<std::uint8_t> (0x90 | ch)); t.push_back (static_cast<std::uint8_t> (n)); t.push_back (static_cast<std::uint8_t> (v)); }
void noteOff (Bytes& t, std::uint32_t d, int ch, int n) { vlq (t, d); t.push_back (static_cast<std::uint8_t> (0x80 | ch)); t.push_back (static_cast<std::uint8_t> (n)); t.push_back (64); }

Bytes simpleFile()
{
    Bytes f = header (0, 1, 480);
    Bytes t;
    tempo (t, 0, 500000); // 120 bpm
    noteOn (t, 0, 0, 36, 100);
    noteOff (t, 480, 0, 36);
    noteOn (t, 0, 0, 43, 90);
    noteOff (t, 240, 0, 43);
    endOfTrack (t, 240);
    addTrack (f, t);
    return f;
}
bool near (double a, double b) { return std::fabs (a - b) < 1e-9; }
} // namespace

static void testValid()
{
    const auto f = simpleFile();
    const auto r = parse (f.data(), f.size());
    CHECK_MSG (r.ok, r.error);
    CHECK (r.phrase.notes.size() == 2);
    CHECK (near (r.phrase.notes[0].startBeats, 0.0) && near (r.phrase.notes[0].lengthBeats, 1.0) && r.phrase.notes[0].note == 36);
    CHECK (r.phrase.notes[0].velocity == 100);
    CHECK (near (r.phrase.notes[1].startBeats, 1.0) && near (r.phrase.notes[1].lengthBeats, 0.5) && r.phrase.notes[1].note == 43);
    CHECK (near (r.phrase.endBeats, 2.0)); // end-of-track marker keeps trailing time
    CHECK (r.info.hasTempo && near (r.info.bpm, 120.0) && ! r.info.tempoChanges);
    CHECK (near (r.info.lengthSeconds, 1.0));
    CHECK (r.info.format == 0 && r.info.ticksPerQuarter == 480);
}

static void testRunningStatusAndVelocityZero()
{
    Bytes f = header (1, 2, 96);
    Bytes conductor; tempo (conductor, 0, 400000); endOfTrack (conductor); addTrack (f, conductor);
    Bytes t;
    vlq (t, 0); t.push_back (0x91); t.push_back (40); t.push_back (100);      // note on ch2
    vlq (t, 96); t.push_back (40); t.push_back (0);                          // running status, velocity 0 = off
    vlq (t, 0); t.push_back (0xB1); t.push_back (1); t.push_back (64);       // CC1
    vlq (t, 0); t.push_back (0xC1); t.push_back (5);                         // program change (ignored)
    vlq (t, 0); t.push_back (0xF0); vlq (t, 2); t.push_back (0x01); t.push_back (0xF7); // sysex skipped
    endOfTrack (t);
    addTrack (f, t);
    const auto r = parse (f.data(), f.size());
    CHECK_MSG (r.ok, r.error);
    CHECK (r.phrase.notes.size() == 1 && r.phrase.notes[0].channel == 1 && near (r.phrase.notes[0].lengthBeats, 1.0));
    CHECK (r.phrase.ccs.size() == 1 && r.phrase.ccs[0].cc == 1 && r.phrase.ccs[0].value == 64);
    CHECK (near (r.info.bpm, 150.0));
    CHECK (r.info.format == 1 && r.info.trackCount == 2);
}

static void testTempoChangesAreFlaggedNotApplied()
{
    Bytes f = header (0, 1, 480);
    Bytes t; tempo (t, 0, 500000); noteOn (t, 0, 0, 36, 100); tempo (t, 480, 250000); noteOff (t, 480, 0, 36); endOfTrack (t);
    addTrack (f, t);
    const auto r = parse (f.data(), f.size());
    CHECK (r.ok && r.info.tempoChanges && near (r.info.bpm, 120.0) && near (r.phrase.endBeats, 2.0));
}

static void testNoTempoUsesDefault()
{
    Bytes f = header (0, 1, 480);
    Bytes t; noteOn (t, 0, 0, 36, 100); noteOff (t, 480, 0, 36); endOfTrack (t); addTrack (f, t);
    const auto r = parse (f.data(), f.size());
    CHECK (r.ok && ! r.info.hasTempo && near (r.info.bpm, 120.0));
}

static void testHangingNoteEndsAtTrackEnd()
{
    Bytes f = header (0, 1, 480);
    Bytes t; noteOn (t, 0, 0, 36, 100); endOfTrack (t, 960); addTrack (f, t);
    const auto r = parse (f.data(), f.size());
    CHECK (r.ok && near (r.phrase.notes[0].lengthBeats, 2.0));
}

static void testRejections()
{
    auto expectFail = [] (const Bytes& b, const char* needle) {
        const auto r = parse (b.data(), b.size());
        CHECK_MSG (! r.ok, needle);
        CHECK_MSG (r.error.find (needle) != std::string::npos, r.error + " (expected: " + needle + ")");
    };
    expectFail ({}, "empty");
    expectFail (Bytes { 'R', 'I', 'F', 'F', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }, "not a standard MIDI file");
    {   // SMPTE division
        Bytes f = header (0, 1, 0xE728); Bytes t; noteOn (t, 0, 0, 36, 100); endOfTrack (t); addTrack (f, t);
        expectFail (f, "SMPTE");
    }
    {   // type 2
        Bytes f = header (2, 1, 480); Bytes t; noteOn (t, 0, 0, 36, 100); endOfTrack (t); addTrack (f, t);
        expectFail (f, "type 2");
    }
    {   // no notes
        Bytes f = header (0, 1, 480); Bytes t; tempo (t, 0, 500000); endOfTrack (t); addTrack (f, t);
        expectFail (f, "no notes");
    }
    {   // truncated track
        auto f = simpleFile(); f.resize (f.size() - 6);
        expectFail (f, "truncated");
    }
    {   // bad data byte
        Bytes f = header (0, 1, 480); Bytes t; vlq (t, 0); t.push_back (0x90); t.push_back (0x90); t.push_back (0x10); endOfTrack (t); addTrack (f, t);
        expectFail (f, "invalid data byte");
    }
    {   // running status without a status byte
        Bytes f = header (0, 1, 480); Bytes t; vlq (t, 0); t.push_back (0x24); t.push_back (0x10); endOfTrack (t); addTrack (f, t);
        expectFail (f, "running status");
    }
    {   // too long: 2000 beats at 20 bpm = 6000 s
        Bytes f = header (0, 1, 480); Bytes t; tempo (t, 0, 3000000); noteOn (t, 0, 0, 36, 100); noteOff (t, 480 * 2000, 0, 36); endOfTrack (t); addTrack (f, t);
        expectFail (f, "longer than 600");
    }
    {   // over 4 MiB
        Bytes f (maxFileBytes + 1, 0);
        expectFail (f, "larger than 4 MB");
    }
    {   // five byte VLQ
        Bytes f = header (0, 1, 480); Bytes t { 0x81, 0x80, 0x80, 0x80, 0x00, 0x90, 36, 100 }; endOfTrack (t); addTrack (f, t);
        expectFail (f, "invalid time value");
    }
}

static void testRobustToGarbage()
{
    // every truncation point and a corrupted byte sweep must return (ok or error) without crashing
    const auto base = simpleFile();
    for (std::size_t n = 0; n <= base.size(); ++n)
        (void) parse (base.data(), n);
    snaral::testing::Rng rng (7);
    for (int i = 0; i < 2000; ++i)
    {
        auto f = base;
        f[static_cast<std::size_t> (rng.below (static_cast<int> (f.size())))] = static_cast<std::uint8_t> (rng.next());
        (void) parse (f.data(), f.size());
    }
}

int main()
{
    testValid();
    testRunningStatusAndVelocityZero();
    testTempoChangesAreFlaggedNotApplied();
    testNoTempoUsesDefault();
    testHangingNoteEndsAtTrackEnd();
    testRejections();
    testRobustToGarbage();
    std::printf ("MidiFileReaderTests passed\n");
    return 0;
}
