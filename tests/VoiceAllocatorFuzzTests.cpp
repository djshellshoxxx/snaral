// Random MIDI event streams against VoiceAllocator invariants (mono + poly, legato, pedal, panic, config flips).
#include "TestSupport.h"

#include <set>

#include "Engine/VoiceAllocator.h"

using namespace snaral;
using namespace snaral::testing;

namespace
{
void checkInvariants (const VoiceAllocator& va, const VoiceCommandList& out, bool poly)
{
    CHECK (out.dropped == 0);
    CHECK (va.activeCount() <= VoiceAllocator::maxVoices);
    CHECK (va.gatedCount() <= va.activeCount());
    if (! poly)
    {
        CHECK (va.gatedCount() <= 1);
        for (int v = 1; v < VoiceAllocator::maxVoices; ++v)
            CHECK (va.voice (v).state == VoiceInfo::State::Free || va.voice (v).state == VoiceInfo::State::Releasing); // leftovers of a poly->mono switch only release
    }
    std::set<int> gatedNotes;
    for (int v = 0; v < VoiceAllocator::maxVoices; ++v)
    {
        const auto& vi = va.voice (v);
        if (vi.state == VoiceInfo::State::Held || vi.state == VoiceInfo::State::PedalHeld)
            CHECK (gatedNotes.insert (vi.note).second); // at most one gated voice per pitch
    }
    for (const auto& c : out)
    {
        CHECK (c.voice < VoiceAllocator::maxVoices);
        CHECK (c.note <= 127);
        if (c.type == VoiceCommand::Type::Start) CHECK (c.velocity >= 1 && c.velocity <= 127);
    }
}

void run (std::uint64_t seed)
{
    Rng rng (seed);
    VoiceAllocator va;
    VoiceCommandList out;
    bool poly = rng.below (2) == 0;
    va.setConfig ({ poly, rng.below (2) == 0 }, out);
    out.clear();
    for (int i = 0; i < 4000; ++i)
    {
        EngineMidiEvent e;
        const int r = rng.below (100);
        e.channel = static_cast<std::uint8_t> (rng.below (16));
        e.data1 = static_cast<std::uint8_t> (rng.below(128));
        e.data2 = static_cast<std::uint8_t> (rng.below(128));
        if (r < 40) { e.type = EngineMidiEvent::Type::NoteOn; e.data1 = static_cast<std::uint8_t> (30 + rng.below (14)); }
        else if (r < 70) { e.type = EngineMidiEvent::Type::NoteOff; e.data1 = static_cast<std::uint8_t> (30 + rng.below (14)); }
        else if (r < 78) { e.type = EngineMidiEvent::Type::ControlChange; e.data1 = 64; }
        else if (r < 82) { e.type = EngineMidiEvent::Type::ControlChange; e.data1 = static_cast<std::uint8_t> (rng.below (128)); }
        else if (r < 85) { e.type = EngineMidiEvent::Type::PitchBend; e.bend14 = static_cast<std::int16_t> (static_cast<int> (rng.below (40000)) - 20000); }
        else if (r < 88) e.type = EngineMidiEvent::Type::AllNotesOff;
        else if (r < 90) e.type = EngineMidiEvent::Type::AllSoundOff;
        else if (r < 91) e.type = EngineMidiEvent::Type::TransportStop;
        else if (r < 94) { va.setConfig ({ poly = rng.below (2) == 0, rng.below (2) == 0 }, out); checkInvariants (va, out, poly); out.clear(); continue; }
        else if (r < 95) { va.panic (out); checkInvariants (va, out, poly); out.clear(); continue; }
        else if (r < 98) { va.voiceFinished (rng.below (12) - 2); continue; }
        else e.type = EngineMidiEvent::Type::ChannelPressure;
        va.handle (e, out);
        checkInvariants (va, out, poly);
        out.clear();
    }
    // release everything: no gated voices may remain
    EngineMidiEvent pedalUp; pedalUp.type = EngineMidiEvent::Type::ControlChange; pedalUp.data1 = 64; pedalUp.data2 = 0;
    for (int n = 0; n < 128; ++n)
    {
        EngineMidiEvent off; off.type = EngineMidiEvent::Type::NoteOff; off.data1 = static_cast<std::uint8_t> (n);
        va.handle (off, out); out.clear();
    }
    va.handle (pedalUp, out);
    checkInvariants (va, out, poly);
    CHECK (va.gatedCount() == 0);
    CHECK (va.heldStackSize() == 0);
}
} // namespace

int main()
{
    for (std::uint64_t seed = 1; seed <= 300; ++seed)
        run (seed);
    std::puts ("VoiceAllocatorFuzzTests passed");
    return 0;
}
