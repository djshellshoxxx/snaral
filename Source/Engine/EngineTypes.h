#pragma once

// Shared, JUCE-free engine interface types. Owner: foundation. Extend by appending only.

#include <cstdint>

namespace snaral
{
struct HostTiming
{
    double bpm = 120.0;       // quarter notes per minute
    bool bpmValid = false;    // false => LFO/Trajectory sync fall back to free rate
    bool playing = false;
    bool ppqValid = false;
    double ppqPosition = 0.0; // quarter-note position of the block start
};

struct EngineMidiEvent
{
    enum class Type : std::uint8_t
    {
        NoteOn, NoteOff, PitchBend, ControlChange, ChannelPressure, PolyPressure,
        AllNotesOff, AllSoundOff, TransportStop
    };

    Type type = Type::NoteOn;
    std::uint8_t channel = 0;   // 0-15
    std::uint8_t data1 = 0;     // note number or CC number
    std::uint8_t data2 = 0;     // velocity or CC value (0-127)
    std::int16_t bend14 = 0;    // PitchBend: -8192..8191
    int sampleOffset = 0;       // within the current block; events are passed sorted ascending
};

/// Per-block modulation values handed to bus-level mid effects (mid layer only).
struct MidModulation
{
    float contour = 0.0f;   // 0..1 normalised timbre contour (bus average of active voices)
    float lfo = 0.0f;       // -1..1 LFO (post Motion scaling is applied by the effect via amount params)
};
} // namespace snaral
