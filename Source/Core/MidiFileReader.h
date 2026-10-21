#pragma once

// Small standards-compliant Standard MIDI File (SMF type 0 / 1) reader for the offline WAV export dialog
// (docs/specs/cdl-offline-wav-export.md). JUCE-free. The result is a render::MidiPhrase in quarter-note beats; the file's tempo
// map is NOT applied (the export uses one constant tempo): info.bpm is the first tempo and info.tempoChanges tells the
// dialog to warn that later tempo changes are ignored.
//
// Rejected with a plain-language message: not an SMF, type 2, SMPTE time division, larger than 4 MiB, truncated or
// otherwise unparsable, no notes, or longer than ParseOptions::maxLengthSeconds at the first tempo.

#include <cstddef>
#include <cstdint>
#include <string>

#include "Core/OfflineRenderer.h"

namespace snaral::midifile
{
inline constexpr std::size_t maxFileBytes = 4u * 1024u * 1024u;

struct ParseOptions
{
    double maxLengthSeconds = 600.0;
    double defaultBpm = 120.0; // used when the file has no tempo event (SMF default)
};

struct FileInfo
{
    int format = 0;            // 0 or 1
    int trackCount = 0;
    int ticksPerQuarter = 0;
    double bpm = 120.0;        // first tempo in the file (or ParseOptions::defaultBpm)
    bool hasTempo = false;
    bool tempoChanges = false; // more than one distinct tempo: the export ignores all but the first
    int noteCount = 0;
    int ccCount = 0;
    double lengthSeconds = 0.0; // phrase end at info.bpm
};

struct ParseResult
{
    bool ok = false;
    std::string error;         // plain language, names the cause
    render::MidiPhrase phrase; // meaningful only when ok
    FileInfo info;
};

ParseResult parse (const std::uint8_t* data, std::size_t size, const ParseOptions& options = {});

inline ParseResult parse (const std::string& bytes, const ParseOptions& options = {})
{
    return parse (reinterpret_cast<const std::uint8_t*> (bytes.data()), bytes.size(), options);
}
} // namespace snaral::midifile
