#pragma once

// Built-in phrases for Offline WAV Export and standalone testing. JUCE-free, header-only.
// builtinPhrase() is the spec phrase (docs/specs/cdl-offline-wav-export.md); the other presets are extras.

#include <algorithm>
#include <string>
#include <vector>

#include "Core/OfflineRenderer.h"

namespace snaral::render
{
inline constexpr int defaultPhraseBpm = 174;

/// Eight quarter notes of C natural minor from `startNote` (24..84): +0 +2 +3 +5 +7 +8 +10 +12, length 0.9 beat.
inline MidiPhrase builtinPhrase (int startNote = 36, int velocity = 100)
{
    static constexpr int steps[8] = { 0, 2, 3, 5, 7, 8, 10, 12 };
    startNote = std::clamp (startNote, 24, 84);
    velocity = std::clamp (velocity, 1, 127);
    MidiPhrase p;
    for (int i = 0; i < 8; ++i)
    {
        TimedNote n;
        n.startBeats = i; n.lengthBeats = 0.9;
        n.note = static_cast<std::uint8_t> (std::min (127, startNote + steps[i]));
        n.velocity = static_cast<std::uint8_t> (velocity);
        p.notes.push_back (n);
    }
    return p;
}

/// 4 bars (4/4) of a sub riff on notes 36 / 38 / 41, 16th-note grid, held notes with gaps.
inline MidiPhrase fourBarRiffPhrase (int velocity = 100)
{
    struct Step { double beat; double len; int note; };
    static constexpr Step bar[] = { { 0.0, 1.5, 36 }, { 1.5, 0.5, 36 }, { 2.0, 0.75, 38 }, { 3.0, 0.5, 41 }, { 3.5, 0.5, 38 } };
    MidiPhrase p;
    for (int b = 0; b < 4; ++b)
        for (const auto& s : bar)
        {
            TimedNote n;
            n.startBeats = b * 4.0 + s.beat; n.lengthBeats = s.len; n.note = static_cast<std::uint8_t> (s.note);
            n.velocity = static_cast<std::uint8_t> (std::clamp (velocity, 1, 127));
            p.notes.push_back (n);
        }
    p.endBeats = 16.0;
    return p;
}

/// One long note (8 beats) for sustain / envelope checks.
inline MidiPhrase sustainedNotePhrase (int note = 36, int velocity = 100)
{
    MidiPhrase p;
    p.notes.push_back ({ 0.0, 8.0, static_cast<std::uint8_t> (std::clamp (note, 0, 127)), static_cast<std::uint8_t> (std::clamp (velocity, 1, 127)), 0 });
    return p;
}

struct ExportPhrasePreset
{
    std::string id;
    std::string name;
    int defaultBpm;
    MidiPhrase phrase;
};

inline std::vector<ExportPhrasePreset> exportPhrasePresets()
{
    return {
        { "minor-scale", "C minor scale, 8 notes", defaultPhraseBpm, builtinPhrase() },
        { "riff-4bar", "Sub riff, 4 bars (36 / 38 / 41)", 140, fourBarRiffPhrase() },
        { "sustain", "Single sustained note", 140, sustainedNotePhrase() },
    };
}
} // namespace snaral::render
