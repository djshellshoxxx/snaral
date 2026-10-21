#pragma once

// Voice allocation / MIDI performance logic. Pure state machine: consumes EngineMidiEvent, emits VoiceCommands.
// JUCE-free, header-only, fixed-size storage, no allocation, no locks, no exceptions. Owner: engine-voice.
// Behaviour is documented in docs/specs/midi-voice.md ("Engine decisions") and performance-foundation.md.

#include "EngineTypes.h"

#include <algorithm>
#include <cstdint>

namespace snaral
{
struct VoiceCommand
{
    enum class Type : std::uint8_t
    {
        Start,    // (re)start a voice: restart envelopes/contour (retrigger==true). stolen==true if it replaced a sounding or still-releasing note.
        Retarget, // legato: change pitch of a gated voice, envelopes/contour keep running
        Release,  // normal ADSR release
        Kill      // immediate silence (engine uses Adsr::forceRelease(declickSamples) or hard kill)
    };

    Type type = Type::Start;
    std::uint8_t voice = 0;
    std::uint8_t note = 0;
    std::uint8_t velocity = 0;
    bool retrigger = false; // Start only; always true today (a Start always restarts), Retarget is the no-retrigger path
    bool glide = false;     // Start/Retarget: pitch may glide from the voice's current pitch (glide_ms applies). Mono only.
    bool stolen = false;    // Start only: voice was still sounding/releasing another note (poly steal)
};

/// Fixed-capacity command output. Cleared by the caller. One handle() call emits at most 8 commands (+ none beyond).
struct VoiceCommandList
{
    static constexpr int capacity = 32;
    VoiceCommand cmds[capacity] {};
    int count = 0;
    int dropped = 0;       // diagnostic, must stay 0
    bool declick = false;  // true when a Kill batch needs the 5 ms output de-click ramp

    void clear() noexcept { count = 0; declick = false; }
    void push (const VoiceCommand& c) noexcept
    {
        if (count < capacity) cmds[count++] = c; else ++dropped;
    }
    const VoiceCommand* begin() const noexcept { return cmds; }
    const VoiceCommand* end() const noexcept { return cmds + count; }
};

struct VoiceInfo
{
    enum class State : std::uint8_t { Free, Held, PedalHeld, Releasing };
    State state = State::Free;
    std::uint8_t note = 0;
    std::uint8_t velocity = 0;
    std::uint32_t age = 0; // start serial; lower = older
};

class VoiceAllocator
{
public:
    static constexpr int maxVoices = 8;
    static constexpr int declickMs = 5;

    struct Config
    {
        bool poly = false;   // voice_mode
        bool legato = false; // mono only
    };

    static int declickSamples (double sampleRate) noexcept { return std::max (1, static_cast<int> (sampleRate * 0.005 + 0.999999)); }

    /// Call when voice_mode/legato change. Switching mono<->poly performs a normal release of everything.
    void setConfig (const Config& c, VoiceCommandList& out) noexcept
    {
        if (c.poly != cfg_.poly) { cfg_ = c; allNotesOff (out); return; }
        cfg_ = c;
    }

    void handle (const EngineMidiEvent& e, VoiceCommandList& out) noexcept
    {
        switch (e.type)
        {
            case EngineMidiEvent::Type::NoteOn:
                if (e.data2 == 0) noteOff (e.data1, out); else noteOn (e.data1, e.data2, out);
                break;
            case EngineMidiEvent::Type::NoteOff: noteOff (e.data1, out); break;
            case EngineMidiEvent::Type::PitchBend: bend14_ = std::clamp<int> (e.bend14, -8192, 8191); break;
            case EngineMidiEvent::Type::ControlChange:
                if (e.data1 == 64) setPedal (e.data2 >= 64, out);
                else if (e.data1 == 120) allSoundOff (out);
                else if (e.data1 == 123) allNotesOff (out);
                break;
            case EngineMidiEvent::Type::AllNotesOff: allNotesOff (out); break;
            case EngineMidiEvent::Type::AllSoundOff: allSoundOff (out); break;
            case EngineMidiEvent::Type::TransportStop: pedal_ = false; allNotesOff (out); break;
            case EngineMidiEvent::Type::ChannelPressure:
            case EngineMidiEvent::Type::PolyPressure: break;
        }
    }

    /// Panic: All Sound Off + clear sustain state. Effective immediately (same sample); engine applies the de-click.
    void panic (VoiceCommandList& out) noexcept
    {
        pedal_ = false;
        allSoundOff (out);
    }

    /// Engine reports a voice's envelope has gone idle (so it becomes Free / preferred for allocation).
    void voiceFinished (int v) noexcept
    {
        if (v >= 0 && v < maxVoices && voices_[v].state == VoiceInfo::State::Releasing)
            voices_[v].state = VoiceInfo::State::Free;
    }

    const VoiceInfo& voice (int i) const noexcept { return voices_[i]; }
    bool pedalDown() const noexcept { return pedal_; }
    int bend14() const noexcept { return bend14_; }
    bool isPoly() const noexcept { return cfg_.poly; }
    int gatedCount() const noexcept
    {
        int n = 0;
        for (const auto& v : voices_) n += (v.state == VoiceInfo::State::Held || v.state == VoiceInfo::State::PedalHeld);
        return n;
    }
    int activeCount() const noexcept
    {
        int n = 0;
        for (const auto& v : voices_) n += (v.state != VoiceInfo::State::Free);
        return n;
    }
    int heldStackSize() const noexcept { return stackSize_; }

private:
    using State = VoiceInfo::State;
    struct StackEntry { std::uint8_t note, velocity; bool down; };

    static VoiceCommand cmd (VoiceCommand::Type t, int v, int note, int vel, bool retrig, bool glide, bool stolen = false) noexcept
    {
        VoiceCommand c;
        c.type = t; c.voice = static_cast<std::uint8_t> (v); c.note = static_cast<std::uint8_t> (note);
        c.velocity = static_cast<std::uint8_t> (vel); c.retrigger = retrig; c.glide = glide; c.stolen = stolen;
        return c;
    }

    void noteOn (int note, int vel, VoiceCommandList& out) noexcept
    {
        if (note > 127) return;
        vel = std::clamp (vel, 1, 127);
        if (cfg_.poly) polyNoteOn (note, vel, out); else monoNoteOn (note, vel, out);
    }

    void noteOff (int note, VoiceCommandList& out) noexcept
    {
        if (note > 127) return;
        if (cfg_.poly) polyNoteOff (note, out); else monoNoteOff (note, out);
    }

    void setPedal (bool down, VoiceCommandList& out) noexcept
    {
        if (down == pedal_) return;
        pedal_ = down;
        if (down) return;
        if (cfg_.poly)
        {
            for (int i = 0; i < maxVoices; ++i)
                if (voices_[i].state == State::PedalHeld) release (i, out);
        }
        else
        {
            int w = 0;
            for (int r = 0; r < stackSize_; ++r)
                if (stack_[r].down) stack_[w++] = stack_[r];
            stackSize_ = w;
            monoResolve (out);
        }
    }

    void release (int i, VoiceCommandList& out) noexcept
    {
        voices_[i].state = State::Releasing;
        out.push (cmd (VoiceCommand::Type::Release, i, voices_[i].note, voices_[i].velocity, false, false));
    }

    // ---- mono -------------------------------------------------------------------------------------------------
    // Stack holds one entry per pitch (a repeated note-on re-presses: old entry removed, new one on top).
    // Invariant: while the mono voice is gated, the sounding note == top of stack.
    void monoNoteOn (int note, int vel, VoiceCommandList& out) noexcept
    {
        removeFromStack (note);
        stack_[stackSize_++] = { static_cast<std::uint8_t> (note), static_cast<std::uint8_t> (vel), true };
        monoSound (stack_[stackSize_ - 1], out);
    }

    void monoSound (const StackEntry& e, VoiceCommandList& out) noexcept
    {
        VoiceInfo& v = voices_[0];
        const bool overlap = (v.state == State::Held);
        const bool legatoNow = overlap && cfg_.legato;
        out.push (cmd (legatoNow ? VoiceCommand::Type::Retarget : VoiceCommand::Type::Start, 0, e.note, e.velocity,
                       ! legatoNow, overlap));
        v.state = State::Held;
        v.note = e.note;
        v.velocity = e.velocity;
        if (! legatoNow) v.age = ++ageCounter_;
    }

    void monoNoteOff (int note, VoiceCommandList& out) noexcept
    {
        for (int i = 0; i < stackSize_; ++i)
        {
            if (stack_[i].note != note) continue;
            if (pedal_) { stack_[i].down = false; return; }
            removeAt (i);
            monoResolve (out);
            return;
        }
    }

    // After the stack changed: release if empty, return to the top entry if the sounding note was dropped.
    void monoResolve (VoiceCommandList& out) noexcept
    {
        VoiceInfo& v = voices_[0];
        if (v.state != State::Held) return;
        if (stackSize_ == 0) { release (0, out); return; }
        const StackEntry& top = stack_[stackSize_ - 1];
        if (top.note != v.note) monoSound (top, out);
    }

    void removeFromStack (int note) noexcept
    {
        for (int i = 0; i < stackSize_; ++i)
            if (stack_[i].note == note) { removeAt (i); return; }
    }

    void removeAt (int i) noexcept
    {
        for (int j = i; j + 1 < stackSize_; ++j) stack_[j] = stack_[j + 1];
        --stackSize_;
    }

    // ---- poly -------------------------------------------------------------------------------------------------
    // Allocation order: (1) voice already holding this pitch (re-press reuses it, so <=1 voice per pitch),
    // (2) lowest-index Free voice, (3) oldest Releasing voice, (4) oldest Held/PedalHeld voice. Age = start serial.
    void polyNoteOn (int note, int vel, VoiceCommandList& out) noexcept
    {
        int idx = -1;
        bool stolen = false;
        for (int i = 0; i < maxVoices; ++i)
            if (voices_[i].state != State::Free && voices_[i].note == note) { idx = i; break; }
        if (idx < 0)
            for (int i = 0; i < maxVoices; ++i)
                if (voices_[i].state == State::Free) { idx = i; break; }
        if (idx < 0)
        {
            std::uint32_t best = 0;
            for (int i = 0; i < maxVoices; ++i)
                if (voices_[i].state == State::Releasing && (idx < 0 || voices_[i].age < best)) { idx = i; best = voices_[i].age; }
            stolen = idx >= 0;
        }
        if (idx < 0)
        {
            std::uint32_t best = 0;
            for (int i = 0; i < maxVoices; ++i)
                if (idx < 0 || voices_[i].age < best) { idx = i; best = voices_[i].age; }
            stolen = true;
        }
        VoiceInfo& v = voices_[idx];
        v.state = State::Held;
        v.note = static_cast<std::uint8_t> (note);
        v.velocity = static_cast<std::uint8_t> (vel);
        v.age = ++ageCounter_;
        out.push (cmd (VoiceCommand::Type::Start, idx, note, vel, true, false, stolen));
    }

    void polyNoteOff (int note, VoiceCommandList& out) noexcept
    {
        for (int i = 0; i < maxVoices; ++i)
        {
            if (voices_[i].state != State::Held || voices_[i].note != note) continue;
            if (pedal_) voices_[i].state = State::PedalHeld; else release (i, out);
            return;
        }
    }

    // ---- global -----------------------------------------------------------------------------------------------
    void allNotesOff (VoiceCommandList& out) noexcept
    {
        stackSize_ = 0;
        for (int i = 0; i < maxVoices; ++i)
            if (voices_[i].state == State::Held || voices_[i].state == State::PedalHeld) release (i, out);
    }

    void allSoundOff (VoiceCommandList& out) noexcept
    {
        stackSize_ = 0;
        for (int i = 0; i < maxVoices; ++i)
        {
            if (voices_[i].state == State::Free) continue;
            out.push (cmd (VoiceCommand::Type::Kill, i, voices_[i].note, voices_[i].velocity, false, false));
            voices_[i].state = State::Free;
            out.declick = true;
        }
    }

    Config cfg_ {};
    VoiceInfo voices_[maxVoices] {};
    StackEntry stack_[128] {};
    int stackSize_ = 0;
    bool pedal_ = false;
    int bend14_ = 0;
    std::uint32_t ageCounter_ = 0;
};
} // namespace snaral
