#ifdef NDEBUG
#undef NDEBUG
#endif
#include "Engine/VoiceAllocator.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <iostream>

using namespace snaral;
using T = EngineMidiEvent::Type;
using C = VoiceCommand::Type;

static EngineMidiEvent ev (T t, int d1 = 0, int d2 = 0, int offset = 0)
{
    EngineMidiEvent e;
    e.type = t;
    e.data1 = static_cast<std::uint8_t> (d1);
    e.data2 = static_cast<std::uint8_t> (d2);
    e.sampleOffset = offset;
    return e;
}
static EngineMidiEvent on (int n, int v = 100) { return ev (T::NoteOn, n, v); }
static EngineMidiEvent off (int n) { return ev (T::NoteOff, n, 0); }
static EngineMidiEvent cc (int c, int v) { return ev (T::ControlChange, c, v); }

struct Rig
{
    VoiceAllocator a;
    VoiceCommandList out;
    explicit Rig (bool poly = false, bool legato = false)
    {
        a.setConfig ({ poly, legato }, out);
        out.clear();
    }
    const VoiceCommandList& send (const EngineMidiEvent& e)
    {
        out.clear();
        a.handle (e, out);
        return out;
    }
};

int main()
{
    // ===== mono, legato off: last-note priority with held-note stack return, always retrigger =====
    {
        Rig r;
        auto o = r.send (on (60, 90));
        assert (o.count == 1 && o.cmds[0].type == C::Start && o.cmds[0].voice == 0 && o.cmds[0].note == 60
                && o.cmds[0].velocity == 90 && o.cmds[0].retrigger && ! o.cmds[0].glide && ! o.cmds[0].stolen);
        o = r.send (on (64, 80));
        assert (o.count == 1 && o.cmds[0].type == C::Start && o.cmds[0].note == 64 && o.cmds[0].retrigger && o.cmds[0].glide);
        o = r.send (on (67, 70));
        assert (o.count == 1 && o.cmds[0].note == 67);
        o = r.send (off (64));                            // not on top: nothing happens
        assert (o.count == 0 && r.a.voice (0).note == 67);
        o = r.send (off (67));                            // return to 60 (64 already released)
        assert (o.count == 1 && o.cmds[0].type == C::Start && o.cmds[0].note == 60 && o.cmds[0].velocity == 90
                && o.cmds[0].retrigger && o.cmds[0].glide);
        o = r.send (off (60));
        assert (o.count == 1 && o.cmds[0].type == C::Release && o.cmds[0].voice == 0);
        assert (r.a.gatedCount() == 0 && r.a.heldStackSize() == 0);
        o = r.send (off (60));                            // stray note-off
        assert (o.count == 0);
        // new note after release: fresh start, no glide
        o = r.send (on (62));
        assert (o.cmds[0].type == C::Start && ! o.cmds[0].glide);
    }

    // ===== note ordering: stack return order is by press order =====
    {
        Rig r;
        r.send (on (60)); r.send (on (62)); r.send (on (64));
        auto o = r.send (off (64));
        assert (o.cmds[0].note == 62);
        o = r.send (off (62));
        assert (o.cmds[0].note == 60);
        // release a lower note first, return skips it
        r.send (on (62)); r.send (on (64));
        r.send (off (62));
        o = r.send (off (64));
        assert (o.cmds[0].note == 60);
    }

    // ===== mono legato: no retrigger on overlap, Retarget with glide; returns also legato =====
    {
        Rig r (false, true);
        auto o = r.send (on (60, 90));
        assert (o.cmds[0].type == C::Start && ! o.cmds[0].glide);                 // first note: retrigger, no glide
        o = r.send (on (64, 80));
        assert (o.count == 1 && o.cmds[0].type == C::Retarget && ! o.cmds[0].retrigger && o.cmds[0].glide
                && o.cmds[0].note == 64 && o.cmds[0].velocity == 80);
        o = r.send (off (60));                                                     // non-top release: nothing
        assert (o.count == 0);
        r.send (on (60, 91));
        o = r.send (off (60));                                                     // return to 64, legato
        assert (o.count == 1 && o.cmds[0].type == C::Retarget && o.cmds[0].note == 64 && o.cmds[0].glide);
        o = r.send (off (64));
        assert (o.count == 1 && o.cmds[0].type == C::Release);
        // after full release (voice in release, not overlapping): legato note retriggers
        o = r.send (on (65));
        assert (o.cmds[0].type == C::Start && o.cmds[0].retrigger && ! o.cmds[0].glide);
        // legato toggled at runtime is honoured on the next note
        VoiceCommandList cl;
        r.a.setConfig ({ false, false }, cl);
        assert (cl.count == 0);
        o = r.send (on (67));
        assert (o.cmds[0].type == C::Start && o.cmds[0].glide);
    }

    // ===== repeated note-on for a held pitch: one instance per pitch, deterministic =====
    {
        Rig r;
        r.send (on (60, 50));
        auto o = r.send (on (60, 99));
        assert (o.count == 1 && o.cmds[0].type == C::Start && o.cmds[0].retrigger && o.cmds[0].velocity == 99);
        assert (r.a.heldStackSize() == 1);
        o = r.send (off (60));
        assert (o.count == 1 && o.cmds[0].type == C::Release);
        Rig l (false, true);
        l.send (on (60));
        o = l.send (on (60, 70));
        assert (o.cmds[0].type == C::Retarget && l.a.heldStackSize() == 1);
        o = l.send (off (60));
        assert (o.cmds[0].type == C::Release);
        // re-press moves the pitch to the top of the stack
        Rig s;
        s.send (on (60)); s.send (on (64)); s.send (on (60));
        o = s.send (off (60));
        assert (o.cmds[0].note == 64);
    }

    // ===== NoteOn velocity 0 == NoteOff; out-of-range notes ignored; same-offset events follow input order =====
    {
        Rig r;
        r.send (on (60));
        auto o = r.send (ev (T::NoteOn, 60, 0));
        assert (o.count == 1 && o.cmds[0].type == C::Release);
        o = r.send (ev (T::NoteOn, 200, 100));
        assert (o.count == 0);
        // offsets 0 / midpoint / end: the allocator applies events strictly in the order handed to it
        for (const int offset : { 0, 128, 255 })
        {
            Rig q;
            q.send (ev (T::NoteOn, 60, 100, offset));
            auto a1 = q.send (ev (T::NoteOff, 60, 0, offset));
            assert (a1.count == 1 && a1.cmds[0].type == C::Release && q.a.gatedCount() == 0);
            Rig q2;                                                                // off-then-on at same offset => sounding
            q2.send (ev (T::NoteOff, 60, 0, offset));
            q2.send (ev (T::NoteOn, 60, 100, offset));
            assert (q2.a.gatedCount() == 1);
        }
    }

    // ===== poly: 8 voices sustain, deterministic stealing =====
    {
        Rig r (true);
        for (int i = 0; i < 8; ++i)
        {
            auto o = r.send (on (40 + i));
            assert (o.count == 1 && o.cmds[0].type == C::Start && o.cmds[0].voice == i && ! o.cmds[0].stolen && ! o.cmds[0].glide);
        }
        assert (r.a.gatedCount() == 8);
        // ninth, nothing released: steal oldest active (voice 0, note 40)
        auto o = r.send (on (60));
        assert (o.cmds[0].voice == 0 && o.cmds[0].stolen && o.cmds[0].note == 60);
        // now oldest active is voice 1. Release voices 5 then 3 (3 is older? age: voice3 started before voice5)
        r.send (off (45));                                   // voice 5 -> releasing
        r.send (off (43));                                   // voice 3 -> releasing
        o = r.send (on (61));                                // oldest RELEASED by age = voice 3, not oldest active (1)
        assert (o.cmds[0].voice == 3 && o.cmds[0].stolen);
        o = r.send (on (62));                                // remaining released voice 5
        assert (o.cmds[0].voice == 5 && o.cmds[0].stolen);
        o = r.send (on (63));                                // no released left: oldest active = voice 1 (note 41)
        assert (o.cmds[0].voice == 1 && o.cmds[0].stolen);
        // engine reports a finished voice: preferred over stealing, lowest index first
        r.send (off (46)); r.send (off (47));                // voices 6, 7 releasing
        r.a.voiceFinished (7); r.a.voiceFinished (6);
        o = r.send (on (70));
        assert (o.cmds[0].voice == 6 && ! o.cmds[0].stolen);
        o = r.send (on (71));
        assert (o.cmds[0].voice == 7 && ! o.cmds[0].stolen);
        assert (r.a.activeCount() == 8 && r.a.gatedCount() == 8);
    }

    // ===== poly: pitch re-press reuses its voice (<=1 voice per pitch); note-off matches by pitch =====
    {
        Rig r (true);
        r.send (on (50)); r.send (on (52));
        auto o = r.send (on (50, 33));
        assert (o.cmds[0].voice == 0 && o.cmds[0].retrigger && ! o.cmds[0].stolen && o.cmds[0].velocity == 33);
        o = r.send (off (50));
        assert (o.count == 1 && o.cmds[0].type == C::Release && o.cmds[0].voice == 0);
        o = r.send (on (50));                                 // pitch still releasing: reuse voice 0
        assert (o.cmds[0].voice == 0 && ! o.cmds[0].stolen);
    }

    // ===== sustain pedal (spec cases 1-5) =====
    {
        // 1. CC64 down delays release; pedal-up releases pedal-held notes (poly + mono)
        for (const bool poly : { true, false })
        {
            Rig r (poly);
            r.send (cc (64, 63));                              // 63 = up
            assert (! r.a.pedalDown());
            r.send (cc (64, 64));                              // 64 = down
            assert (r.a.pedalDown());
            r.send (on (60));
            auto o = r.send (off (60));
            assert (o.count == 0 && r.a.gatedCount() == 1);
            o = r.send (cc (64, 0));
            assert (o.count == 1 && o.cmds[0].type == C::Release && o.cmds[0].note == 60);
            assert (r.a.gatedCount() == 0);
            o = r.send (cc (64, 0));                           // repeated pedal-up: nothing
            assert (o.count == 0);
        }
        // 2. notes physically held through pedal-up continue sounding
        {
            Rig r (true);
            r.send (on (60)); r.send (on (64));
            r.send (cc (64, 127));
            r.send (off (60));
            auto o = r.send (cc (64, 0));
            assert (o.count == 1 && o.cmds[0].note == 60 && o.cmds[0].type == C::Release);
            assert (r.a.gatedCount() == 1 && r.a.voice (1).state == VoiceInfo::State::Held);
            o = r.send (off (64));
            assert (o.count == 1 && o.cmds[0].note == 64);
        }
        {   // mono: held note under pedal-held top is returned to on pedal-up
            Rig r;
            r.send (on (60)); r.send (cc (64, 100)); r.send (on (64)); r.send (off (64));
            assert (r.a.voice (0).note == 64);                 // pedal keeps the voice on 64
            auto o = r.send (cc (64, 0));
            assert (o.count == 1 && o.cmds[0].type == C::Start && o.cmds[0].note == 60);
            o = r.send (off (60));
            assert (o.cmds[0].type == C::Release);
        }
        {   // mono: everything lifted under pedal -> single release on pedal-up
            Rig r;
            r.send (cc (64, 127)); r.send (on (60)); r.send (on (64)); r.send (off (64)); r.send (off (60));
            assert (r.a.gatedCount() == 1);
            auto o = r.send (cc (64, 0));
            assert (o.count == 1 && o.cmds[0].type == C::Release && r.a.heldStackSize() == 0);
        }
        // pedal-held note re-pressed in poly: same voice, key down again, pedal-up does not release it
        {
            Rig r (true);
            r.send (cc (64, 127)); r.send (on (60)); r.send (off (60));
            auto o = r.send (on (60));
            assert (o.cmds[0].voice == 0 && o.cmds[0].retrigger);
            o = r.send (cc (64, 0));
            assert (o.count == 0 && r.a.gatedCount() == 1);
        }
        // 3. CC123 normal release (pedal-held notes too), CC120 immediate silence + de-click flag
        {
            Rig r (true);
            r.send (on (60)); r.send (on (62)); r.send (cc (64, 127)); r.send (off (62));
            auto o = r.send (cc (123, 0));
            assert (o.count == 2 && o.cmds[0].type == C::Release && o.cmds[1].type == C::Release && ! o.declick);
            assert (r.a.gatedCount() == 0 && r.a.activeCount() == 2);
            assert (r.a.pedalDown());                          // CC123 does not touch the pedal
            r.send (on (64));
            o = r.send (cc (120, 0));
            assert (o.count == 3 && o.declick);
            for (const auto& c : o) assert (c.type == C::Kill);
            assert (r.a.activeCount() == 0 && r.a.pedalDown()); // CC120 does not clear the pedal; panic does
            o = r.send (cc (120, 0));
            assert (o.count == 0 && ! o.declick);
            o = r.send (ev (T::AllNotesOff));
            assert (o.count == 0);
        }
        // 4. panic: everything silent in the same call, pedal cleared, de-click budget <= 5 ms
        for (const bool poly : { true, false })
        {
            Rig r (poly);
            r.send (cc (64, 127));
            for (int i = 0; i < 6; ++i) r.send (on (50 + i));
            r.send (off (51));
            r.out.clear();
            r.a.panic (r.out);
            assert (r.out.declick && r.out.count >= 1);
            for (const auto& c : r.out) assert (c.type == C::Kill);
            assert (r.a.activeCount() == 0 && ! r.a.pedalDown() && r.a.heldStackSize() == 0);
            for (const double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
                assert (VoiceAllocator::declickSamples (sr) / sr <= 0.005 + 1.0 / sr);
            // after panic a note-off releases normally (sustain state cleared)
            r.send (on (60));
            auto o = r.send (off (60));
            assert (o.count == 1 && o.cmds[0].type == C::Release);
        }
        // transport stop: clears pedal-held and held notes, safe release path, pedal state cleared
        for (const bool poly : { true, false })
        {
            Rig r (poly);
            r.send (on (60)); r.send (cc (64, 127)); r.send (on (62)); r.send (off (62));
            auto o = r.send (ev (T::TransportStop));
            assert (o.count >= 1 && ! o.declick);
            for (const auto& c : o) assert (c.type == C::Release);
            assert (r.a.gatedCount() == 0 && ! r.a.pedalDown());
        }
    }

    // ===== mode switch releases everything =====
    {
        Rig r (true);
        r.send (on (60)); r.send (on (64));
        VoiceCommandList o;
        r.a.setConfig ({ false, false }, o);
        assert (o.count == 2 && o.cmds[0].type == C::Release);
        assert (r.a.gatedCount() == 0);
        r.send (on (67));
        assert (r.a.gatedCount() == 1 && r.out.cmds[0].voice == 0);
    }

    // ===== pitch bend state =====
    {
        Rig r;
        EngineMidiEvent b = ev (T::PitchBend);
        b.bend14 = -8192; r.send (b); assert (r.a.bend14() == -8192);
        b.bend14 = 8191; r.send (b); assert (r.a.bend14() == 8191);
        b.bend14 = 0; r.send (b); assert (r.a.bend14() == 0);
    }

    // ===== randomized fuzz: 100k events per configuration; model checks every event =====
    {
        std::uint32_t s = 0xC0FFEEu;
        auto rnd = [&s] { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; };
        struct Cfg { bool poly, legato; };
        long events = 0;
        for (const Cfg cfg : { Cfg { false, false }, Cfg { false, true }, Cfg { true, false } })
        {
            Rig r (cfg.poly, cfg.legato);
            bool keys[128] = {};
            bool shadowGated[8] = {}, shadowActive[8] = {};
            bool pedal = false;
            for (int n = 0; n < 100000; ++n, ++events)
            {
                const auto pick = rnd() % 100;
                const int note = 40 + static_cast<int> (rnd() % 14);
                EngineMidiEvent e;
                bool isPanic = false;
                if (pick < 34) { e = on (note, static_cast<int> (rnd() % 128)); }
                else if (pick < 68) { e = off (note); }
                else if (pick < 80) { const int v = static_cast<int> (rnd() % 128); e = cc (64, v); }
                else if (pick < 84) e = ev (T::AllNotesOff);
                else if (pick < 86) e = cc (123, 0);
                else if (pick < 88) e = cc (120, 0);
                else if (pick < 89) e = ev (T::AllSoundOff);
                else if (pick < 90) e = ev (T::TransportStop);
                else if (pick < 91) isPanic = true;
                else if (pick < 95) { e = ev (T::PitchBend); e.bend14 = static_cast<std::int16_t> (static_cast<int> (rnd() % 16384) - 8192); }
                else if (pick < 97) e = ev (T::ChannelPressure, 0, static_cast<int> (rnd() % 128));
                else
                {
                    const int v = static_cast<int> (rnd() % 8);
                    r.a.voiceFinished (v);
                    if (r.a.voice (v).state == VoiceInfo::State::Free) shadowActive[v] = false;
                    continue;
                }

                r.out.clear();
                if (isPanic) { r.a.panic (r.out); pedal = false; }
                else r.a.handle (e, r.out);
                assert (r.out.dropped == 0 && r.out.count <= 8);

                // physical key / pedal model
                if (! isPanic)
                {
                    if (e.type == T::NoteOn) keys[e.data1] = e.data2 > 0;
                    else if (e.type == T::NoteOff) keys[e.data1] = false;
                    else if (e.type == T::ControlChange && e.data1 == 64) pedal = e.data2 >= 64;
                    else if (e.type == T::TransportStop) pedal = false;
                }

                // commands applied to a shadow engine; must be self-consistent
                for (const auto& c : r.out)
                {
                    assert (c.voice < 8);
                    switch (c.type)
                    {
                        case C::Start: shadowGated[c.voice] = true; shadowActive[c.voice] = true; break;
                        case C::Retarget: assert (shadowGated[c.voice]); break;
                        case C::Release: assert (shadowGated[c.voice]); shadowGated[c.voice] = false; break;
                        case C::Kill: shadowGated[c.voice] = false; shadowActive[c.voice] = false; break;
                    }
                }
                int seenPitch[128] = {};
                for (int v = 0; v < 8; ++v)
                {
                    const auto& vi = r.a.voice (v);
                    const bool gated = vi.state == VoiceInfo::State::Held || vi.state == VoiceInfo::State::PedalHeld;
                    assert (gated == shadowGated[v]);
                    if (vi.state == VoiceInfo::State::Free) assert (! shadowGated[v]);
                    if (shadowActive[v]) assert (vi.state != VoiceInfo::State::Free);
                    if (vi.state == VoiceInfo::State::PedalHeld) assert (pedal && cfg.poly);
                    if (cfg.poly && vi.state != VoiceInfo::State::Free) assert (++seenPitch[vi.note] == 1);
                    if (cfg.poly && vi.state == VoiceInfo::State::Held) assert (keys[vi.note]);
                    if (cfg.poly && vi.state == VoiceInfo::State::PedalHeld) assert (! keys[vi.note]);
                    if (gated && ! cfg.poly) assert (keys[vi.note] || pedal);
                    if (v > 0 && ! cfg.poly) assert (! gated);
                }
                if (! cfg.poly)
                    assert ((r.a.heldStackSize() > 0) == (r.a.voice (0).state == VoiceInfo::State::Held));
                assert (r.a.gatedCount() <= (cfg.poly ? 8 : 1));
                assert (r.a.bend14() >= -8192 && r.a.bend14() <= 8191);
            }
            // no stuck notes: lift every key, pedal up -> nothing gated, every Start has a matching Release/Kill
            for (int nn = 0; nn < 128; ++nn) r.send (off (nn));
            r.send (cc (64, 0));
            assert (r.a.gatedCount() == 0 && r.a.heldStackSize() == 0);
            for (int v = 0; v < 8; ++v) assert (r.a.voice (v).state != VoiceInfo::State::Held
                                                && r.a.voice (v).state != VoiceInfo::State::PedalHeld);
        }
        assert (events == 300000);
    }

    // ===== fuzz with mode switches interleaved =====
    {
        std::uint32_t s = 99u;
        auto rnd = [&s] { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; };
        Rig r;
        for (int n = 0; n < 100000; ++n)
        {
            const auto pick = rnd() % 100;
            r.out.clear();
            if (pick < 3) r.a.setConfig ({ (rnd() & 1) != 0, (rnd() & 1) != 0 }, r.out);
            else if (pick < 45) r.a.handle (on (36 + static_cast<int> (rnd() % 20), 1 + static_cast<int> (rnd() % 127)), r.out);
            else if (pick < 85) r.a.handle (off (36 + static_cast<int> (rnd() % 20)), r.out);
            else if (pick < 93) r.a.handle (cc (64, static_cast<int> (rnd() % 128)), r.out);
            else if (pick < 95) r.a.handle (ev (T::TransportStop), r.out);
            else r.a.voiceFinished (static_cast<int> (rnd() % 8));
            assert (r.out.dropped == 0);
            assert (r.a.gatedCount() <= 8);
        }
        for (int nn = 0; nn < 128; ++nn) r.send (off (nn));
        r.send (cc (64, 0));
        assert (r.a.gatedCount() == 0);
    }

    std::cout << "VoiceAllocatorTests passed\n";
    return 0;
}
