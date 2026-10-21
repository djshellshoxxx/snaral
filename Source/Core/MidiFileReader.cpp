#include "Core/MidiFileReader.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace snaral::midifile
{
namespace
{
struct Cursor
{
    const std::uint8_t* p;
    std::size_t size;
    std::size_t pos = 0;
    bool ok = true;

    std::size_t left() const noexcept { return size - pos; }
    std::uint8_t u8()
    {
        if (pos >= size) { ok = false; return 0; }
        return p[pos++];
    }
    std::uint32_t u16() { const auto a = u8(); const auto b = u8(); return static_cast<std::uint32_t> ((a << 8) | b); }
    std::uint32_t u32() { const auto a = u16(); const auto b = u16(); return (a << 16) | b; }
    std::uint32_t vlq()
    {
        std::uint32_t v = 0;
        for (int i = 0; i < 4; ++i)
        {
            const auto b = u8();
            if (! ok) return 0;
            v = (v << 7) | (b & 0x7Fu);
            if ((b & 0x80u) == 0)
                return v;
        }
        ok = false; // more than 4 bytes: not a valid variable length quantity
        return 0;
    }
    void skip (std::size_t n)
    {
        if (n > left()) { ok = false; pos = size; return; }
        pos += n;
    }
};

struct OpenNote
{
    std::uint64_t startTick;
    std::uint8_t velocity;
};

struct RawNote { std::uint64_t start, end; std::uint8_t note, velocity, channel; };
struct RawCc { std::uint64_t tick; std::uint8_t cc, value, channel; };
struct RawTempo { std::uint64_t tick; double bpm; };

ParseResult fail (std::string message)
{
    ParseResult r;
    r.error = std::move (message);
    return r;
}
} // namespace

ParseResult parse (const std::uint8_t* data, std::size_t size, const ParseOptions& options)
{
    if (data == nullptr || size == 0)
        return fail ("The file is empty.");
    if (size > maxFileBytes)
        return fail ("The MIDI file is larger than 4 MB, which is more than Snaral will read.");
    if (size < 14 || std::string (reinterpret_cast<const char*> (data), 4) != "MThd")
        return fail ("This is not a standard MIDI file (.mid).");

    Cursor c { data, size };
    c.skip (4);
    const auto headerLen = c.u32();
    if (headerLen < 6 || headerLen > size - 8)
        return fail ("The MIDI file header is damaged.");
    const auto format = c.u16();
    const auto trackCount = c.u16();
    const auto division = c.u16();
    c.skip (headerLen - 6);
    if (! c.ok)
        return fail ("The MIDI file header is damaged.");
    if (format > 2)
        return fail ("This MIDI file has an unknown format number.");
    if (format == 2)
        return fail ("MIDI files of type 2 (independent sequences) are not supported. Save the file as type 0 or 1.");
    if ((division & 0x8000u) != 0)
        return fail ("This MIDI file uses SMPTE time (frames per second) instead of beats, which is not supported.");
    if (division == 0)
        return fail ("The MIDI file has a time division of zero.");
    if (trackCount == 0)
        return fail ("The MIDI file contains no tracks.");

    std::vector<RawNote> notes;
    std::vector<RawCc> ccs;
    std::vector<RawTempo> tempos;
    std::uint64_t lastTick = 0;
    int tracksRead = 0;

    while (c.left() >= 8 && tracksRead < static_cast<int> (trackCount))
    {
        const std::string tag (reinterpret_cast<const char*> (data + c.pos), 4);
        c.skip (4);
        const auto length = c.u32();
        if (length > c.left())
            return fail ("The MIDI file is truncated (a track is shorter than its header says).");
        if (tag != "MTrk")
        {
            c.skip (length); // unknown chunk types must be skipped
            continue;
        }
        ++tracksRead;

        Cursor t { data + c.pos, length };
        c.skip (length);

        std::uint64_t tick = 0;
        int running = -1;
        bool sawEnd = false;
        std::vector<OpenNote> open[16][128];

        while (t.left() > 0 && ! sawEnd)
        {
            const auto delta = t.vlq();
            if (! t.ok) return fail ("A MIDI track contains an invalid time value.");
            tick += delta;

            int status = t.u8();
            if (! t.ok) return fail ("The MIDI file ends in the middle of an event.");
            if (status < 0x80)
            {
                if (running < 0)
                    return fail ("The MIDI file uses running status before any status byte.");
                status = running;
                --t.pos; // the byte just read is the first data byte
            }
            else if (status < 0xF0)
                running = status;
            else
                running = -1; // system / meta messages cancel running status

            if (status == 0xFF)
            {
                const int type = t.u8();
                const auto len = t.vlq();
                if (! t.ok || len > t.left())
                    return fail ("The MIDI file contains a damaged meta event.");
                if (type == 0x51)
                {
                    if (len != 3)
                        return fail ("The MIDI file contains a damaged tempo event.");
                    const auto micros = (static_cast<std::uint32_t> (t.p[t.pos]) << 16) | (static_cast<std::uint32_t> (t.p[t.pos + 1]) << 8)
                                        | static_cast<std::uint32_t> (t.p[t.pos + 2]);
                    if (micros > 0)
                        tempos.push_back ({ tick, 60000000.0 / static_cast<double> (micros) });
                }
                t.skip (len);
                if (type == 0x2F)
                    sawEnd = true;
            }
            else if (status == 0xF0 || status == 0xF7)
            {
                const auto len = t.vlq();
                if (! t.ok || len > t.left())
                    return fail ("The MIDI file contains a damaged system exclusive message.");
                t.skip (len);
            }
            else if (status >= 0xF1)
            {
                return fail ("The MIDI file contains an unexpected system message.");
            }
            else
            {
                const int kind = status & 0xF0;
                const int ch = status & 0x0F;
                const bool oneData = kind == 0xC0 || kind == 0xD0;
                const auto d1 = t.u8();
                const auto d2 = oneData ? std::uint8_t { 0 } : t.u8();
                if (! t.ok)
                    return fail ("The MIDI file ends in the middle of an event.");
                if ((d1 & 0x80) != 0 || (d2 & 0x80) != 0)
                    return fail ("The MIDI file contains an invalid data byte.");

                if (kind == 0x90 && d2 > 0)
                    open[ch][d1].push_back ({ tick, d2 });
                else if (kind == 0x80 || kind == 0x90)
                {
                    auto& stack = open[ch][d1];
                    if (! stack.empty())
                    {
                        const auto n = stack.front(); // overlapping equal notes close first-in first-out
                        stack.erase (stack.begin());
                        notes.push_back ({ n.startTick, std::max (tick, n.startTick + 1), static_cast<std::uint8_t> (d1), n.velocity,
                                           static_cast<std::uint8_t> (ch) });
                    }
                }
                else if (kind == 0xB0)
                    ccs.push_back ({ tick, static_cast<std::uint8_t> (d1), d2, static_cast<std::uint8_t> (ch) });
            }
        }
        if (! sawEnd && t.left() > 0)
            return fail ("A MIDI track is damaged.");

        // notes still sounding at the end of the track end there
        for (int ch = 0; ch < 16; ++ch)
            for (int n = 0; n < 128; ++n)
                for (const auto& o : open[ch][n])
                    notes.push_back ({ o.startTick, std::max (tick, o.startTick + 1), static_cast<std::uint8_t> (n), o.velocity,
                                       static_cast<std::uint8_t> (ch) });
        lastTick = std::max (lastTick, tick);
    }

    if (tracksRead == 0)
        return fail ("The MIDI file contains no track data.");
    if (notes.empty())
        return fail ("The MIDI file contains no notes.");

    ParseResult r;
    std::sort (tempos.begin(), tempos.end(), [] (const RawTempo& a, const RawTempo& b) { return a.tick < b.tick; });
    r.info.format = static_cast<int> (format);
    r.info.trackCount = tracksRead;
    r.info.ticksPerQuarter = static_cast<int> (division);
    r.info.hasTempo = ! tempos.empty();
    r.info.bpm = tempos.empty() ? options.defaultBpm : tempos.front().bpm;
    for (const auto& t : tempos)
        if (std::abs (t.bpm - r.info.bpm) > 0.01)
            r.info.tempoChanges = true;

    const double ppq = static_cast<double> (division);
    std::sort (notes.begin(), notes.end(), [] (const RawNote& a, const RawNote& b) {
        if (a.start != b.start) return a.start < b.start;
        if (a.note != b.note) return a.note < b.note;
        return a.channel < b.channel;
    });
    std::uint64_t endTick = 0;
    for (const auto& n : notes)
    {
        render::TimedNote tn;
        tn.startBeats = static_cast<double> (n.start) / ppq;
        tn.lengthBeats = static_cast<double> (n.end - n.start) / ppq;
        tn.note = n.note;
        tn.velocity = n.velocity;
        tn.channel = n.channel;
        r.phrase.notes.push_back (tn);
        endTick = std::max (endTick, n.end);
    }
    std::stable_sort (ccs.begin(), ccs.end(), [] (const RawCc& a, const RawCc& b) { return a.tick < b.tick; });
    for (const auto& cc : ccs)
    {
        render::TimedCc tc;
        tc.beats = static_cast<double> (cc.tick) / ppq;
        tc.cc = cc.cc;
        tc.value = cc.value;
        tc.channel = cc.channel;
        r.phrase.ccs.push_back (tc);
        endTick = std::max (endTick, cc.tick);
    }
    endTick = std::max (endTick, lastTick); // an explicit end-of-track marker keeps trailing silence
    r.phrase.endBeats = static_cast<double> (endTick) / ppq;

    r.info.noteCount = static_cast<int> (notes.size());
    r.info.ccCount = static_cast<int> (ccs.size());
    r.info.lengthSeconds = r.phrase.endBeats * 60.0 / r.info.bpm;
    if (! std::isfinite (r.info.lengthSeconds) || r.info.lengthSeconds > options.maxLengthSeconds)
    {
        ParseResult bad;
        bad.error = "The MIDI phrase is longer than " + std::to_string (static_cast<int> (options.maxLengthSeconds))
                    + " seconds, which is the limit for an export.";
        return bad;
    }
    r.ok = true;
    return r;
}
} // namespace snaral::midifile
