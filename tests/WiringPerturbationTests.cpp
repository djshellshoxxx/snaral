// Audit checklist steps 1, 2, 4 (docs/specs/audit-checklist.md), JUCE-free:
//  1. registry walk: every parameter ID matches exactly one row of the checklist parameter table (and vice versa),
//     and the table's "In presets" / "Host automation" columns agree with the registry flags;
//  2. wiring perturbation: starting from the audit patch, every ID is moved to min / midpoint / max and the offline
//     render must differ by more than -90 dB (RMS of the difference relative to the reference) in at least one of the
//     scenarios the parameter needs (two notes, pitch bend, velocity, ...);
//  4. preset coverage: every enable switch is on in at least one factory preset.
// Prints one line per ID so the result can be pasted into docs/release/AUDIT.md.
#include "EngineOutputTestUtil.h"
#include "EngineRig.h"
#include "State/Pure/FactoryPresets.h"

#include <cstdio>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#ifndef SNARAL_SOURCE_DIR
#error "SNARAL_SOURCE_DIR must be defined"
#endif

using namespace snaral;

namespace
{
constexpr double sr = 48000.0;
constexpr double thresholdDb = -90.0;

struct Row { std::vector<std::string> ids; bool inPresets = true; bool automation = true; };

std::vector<Row> loadTable()
{
    std::ifstream f (std::string (SNARAL_SOURCE_DIR) + "/docs/specs/audit-checklist.md");
    std::string line;
    bool in = false;
    std::vector<Row> rows;
    while (std::getline (f, line))
    {
        if (line.rfind ("## Parameter table", 0) == 0) { in = true; continue; }
        if (in && line.rfind ("## ", 0) == 0) break;
        if (! in || line.size() < 2 || line[0] != '|' || line.rfind ("|---", 0) == 0 || line.rfind ("| ID(s)", 0) == 0) continue;
        std::vector<std::string> cols;
        std::stringstream ss (line);
        std::string c;
        std::getline (ss, c, '|');
        while (std::getline (ss, c, '|')) cols.push_back (c);
        CHECK (cols.size() >= 5);
        Row r;
        std::size_t p = 0;
        while ((p = cols[0].find ('`', p)) != std::string::npos)
        {
            const auto e = cols[0].find ('`', p + 1);
            r.ids.push_back (cols[0].substr (p + 1, e - p - 1));
            p = e + 1;
        }
        r.inPresets = cols[3].find ("yes") != std::string::npos;
        r.automation = cols[4].find ("yes") != std::string::npos;
        rows.push_back (r);
    }
    return rows;
}

bool matches (const std::string& pattern, std::string_view id)
{
    if (! pattern.empty() && pattern.back() == '*') return id.substr (0, pattern.size() - 1) == std::string_view (pattern).substr (0, pattern.size() - 1);
    return id == pattern;
}

struct Scenario { std::string name; std::function<void (rig::Rig&)> run; };

std::vector<float> renderScenario (const ParamSnapshot& patch, int which)
{
    rig::Rig r (sr, 256);
    r.p = patch;
    HostTiming ht; ht.bpm = 174.0; ht.bpmValid = true; ht.playing = true; ht.ppqValid = true; ht.ppqPosition = 0.0;
    r.eng.setHostTiming (ht);
    const int n = static_cast<int> (sr * 0.6);
    using namespace rig;
    switch (which)
    {
        case 0: r.run (n, 256, { { 0, noteOnEv (36, 110) } }); break;
        case 1: r.run (n, 256, { { 0, noteOnEv (60, 110) } }); break;
        case 2: r.run (n, 256, { { 0, noteOnEv (36, 110) }, { n / 4, noteOnEv (43, 110) }, { n / 2, noteOffEv (36) } }); break;
        case 3: r.run (n, 256, { { 0, noteOnEv (60, 50) } }); break;
        case 4: r.run (n, 256, { { 0, noteOnEv (36, 110) }, { n / 4, bendEv (16383) } }); break;
        case 5: r.run (n, 256, { { 0, noteOnEv (69, 110) } }); break;
        case 6: r.run (n, 256, { { 0, noteOnEv (36, 110) }, { n / 3, noteOffEv (36) } }); break; // release stages
        case 7: r.run (n, 256, { { 0, noteOnEv (60, 110) }, { n / 3, noteOnEv (67, 110) } }); break; // second note retriggers the LFO
        default: break;
    }
    return r.L; // sub is centered; the L/R difference is checked separately by the invariant tests
}

std::vector<float> stereoCat (const ParamSnapshot& patch, int which)
{
    rig::Rig r (sr, 256);
    r.p = patch;
    HostTiming ht; ht.bpm = 174.0; ht.bpmValid = true; ht.playing = true; ht.ppqValid = true;
    r.eng.setHostTiming (ht);
    using namespace rig;
    const int n = static_cast<int> (sr * 0.6);
    r.run (n, 256, { { 0, noteOnEv (which == 0 ? 36 : 60, 110) } });
    std::vector<float> o = r.L;
    o.insert (o.end(), r.R.begin(), r.R.end());
    return o;
}

// Cross-cutting invariant: the sub layer is sample-identical L/R for every parameter move (sub-solo render, notes 36 and 60).
bool subCentred (const ParamSnapshot& patch)
{
    for (int note : { 36, 60 })
    {
        rig::Rig r (sr, 256);
        r.p = patch;
        r.p[P::layer_solo] = 1.0f;
        HostTiming ht; ht.bpm = 174.0; ht.bpmValid = true; ht.playing = true; ht.ppqValid = true;
        r.eng.setHostTiming (ht);
        r.run (static_cast<int> (sr * 0.3), 256, { { 0, rig::noteOnEv (note, 110) } });
        if (! rig::identical (r.L, r.R)) return false;
    }
    return true;
}

ParamSnapshot auditPatch()
{
    ParamSnapshot p;
    p[P::weight] = 70; p[P::growl] = 70; p[P::bite] = 40; p[P::motion] = 70; p[P::space] = 60;
    p[P::mid_source_a] = 2; p[P::mid_source_b] = 3; p[P::mid_blend] = 50;
    p[P::lfo_mode] = 1; p[P::lfo_depth] = 60; p[P::lfo_retrigger] = 1; p[P::lfo_phase] = 0;
    p[P::contour_depth] = 60;
    p[P::furnace_enabled] = 1; p[P::furnace_env_amount] = 50; p[P::furnace_lfo_amount] = 50;
    p[P::fracture_enabled] = 1;
    p[P::traj_enabled] = 1; p[P::traj_dest] = 2;
    p[P::anchor_enabled] = 1; p[P::anchor_note] = 48; p[P::anchor_strength] = 60;
    p[P::noise_level] = 20; p[P::metal_level] = 20;
    p[P::drive_amount] = 40;
    p[P::velocity_to_level] = 0; p[P::velocity_to_attack] = 0;
    return p;
}

// Per-parameter precondition tweaks applied to BOTH reference and variant ("Needs" column of the checklist).
void needs (const std::string& id, ParamSnapshot& p, std::vector<int>& scenarios)
{
    scenarios = { 0, 1 };
    if (id == "mid_pulse_width") { p[P::mid_source_a] = 1; p[P::mid_blend] = 0; }
    else if (id == "mid_table_position") { p[P::mid_blend] = 0; }
    else if (id == "voice_mode" || id == "legato" || id == "glide_ms") { p[P::voice_mode] = (id == "voice_mode") ? 0.0f : 0.0f; scenarios = { 2 }; if (id == "glide_ms") p[P::legato] = 1; }
    else if (id == "sub_amp_decay_ms") p[P::sub_amp_sustain] = 40;
    else if (id == "sub_amp_release_ms" || id == "mid_amp_release_ms") scenarios = { 6 };
    else if (id == "contour_release_ms") { p[P::traj_enabled] = 0; scenarios = { 6 }; }
    else if (id == "contour_depth") p[P::traj_enabled] = 0; // the trajectory replaces the contour on its destination (documented)
    else if (id == "lfo_rate_hz") p[P::lfo_mode] = 0;       // sync mode ignores the free rate (documented)
    else if (id == "lfo_retrigger") { p[P::lfo_mode] = 0; p[P::voice_mode] = 1; scenarios = { 7 }; }
    else if (id == "pitch_bend_range") scenarios = { 4 };
    else if (id == "velocity_to_level") { p[P::velocity_to_attack] = 0; scenarios = { 3 }; }
    else if (id == "velocity_to_attack") { p[P::mid_amp_attack_ms] = 100; p[P::sub_amp_attack_ms] = 100; scenarios = { 3 }; }
    else if (id == "master_tune_hz" || id == "transpose_semitones") scenarios = { 5 };
    else if (id == "limiter_enabled" || id == "limiter_ceiling_db") { p[P::output_trim_db] = 12; p[P::limiter_ceiling_db] = id == "limiter_ceiling_db" ? -12.0f : -1.0f; }
    else if (id == "traj_length_div") p[P::traj_time_mode] = 1;
    else if (id == "oversampling_mode") { p[P::drive_amount] = 100; p[P::drive_type] = 1; p[P::fracture_drive_high] = 100; }
    else if (id == "traj_loop") p[P::traj_length_ms] = 100;
    else if (id == "sub_waveform") { p[P::weight] = 100; }
    else if (id == "lfo_phase") { p[P::lfo_retrigger] = 1; }
    else if (id == "anchor_enabled" || id == "anchor_note" || id == "anchor_strength") { p[P::anchor_enabled] = 1; if (id == "anchor_enabled") p[P::anchor_enabled] = 1; }
}
}

int main()
{
    // ---- 1. registry walk
    const auto rows = loadTable();
    std::map<int, int> hits;
    for (const auto& row : rows)
        for (const auto& pat : row.ids)
        {
            bool any = false;
            for (int i = 0; i < parameterCount; ++i)
                if (matches (pat, parameterTable[static_cast<std::size_t> (i)].id))
                {
                    any = true; ++hits[i];
                    const auto& d = parameterTable[static_cast<std::size_t> (i)];
                    if (d.inPreset() != row.inPresets) { std::fprintf (stderr, "table/registry preset flag mismatch: %s\n", std::string (d.id).c_str()); return 1; }
                    if (d.automatable() != row.automation) { std::fprintf (stderr, "table/registry automation flag mismatch: %s\n", std::string (d.id).c_str()); return 1; }
                }
            if (! any) { std::fprintf (stderr, "table row without registry id: %s\n", pat.c_str()); return 1; }
        }
    for (int i = 0; i < parameterCount; ++i)
        if (hits[i] != 1) { std::fprintf (stderr, "id %s matched %d table rows\n", std::string (parameterTable[static_cast<std::size_t> (i)].id).c_str(), hits[i]); return 1; }
    std::printf ("registry walk: %d ids each matched exactly once\n", parameterCount);

    // ---- 2. perturbation
    int unwired = 0;
    const double thr = std::pow (10.0, thresholdDb / 20.0);
    for (int i = 0; i < parameterCount; ++i)
    {
        const auto& d = parameterTable[static_cast<std::size_t> (i)];
        const std::string id (d.id);
        ParamSnapshot base = auditPatch();
        std::vector<int> scenarios;
        needs (id, base, scenarios);
        double best = 1.0e-30;
        std::string via;
        const float mid = d.type == ParamType::Float ? d.fromNormalized (0.5f) : d.clampToDomain (std::round (d.minimum + 0.5f * (d.maximum - d.minimum)));
        std::vector<float> values { d.minimum, mid, d.maximum };
        // reference is the patch at the registry default for this parameter (audit patch overrides stay).
        ParamSnapshot ref = base;
        if (id != "bypass" && id != "layer_solo" && id != "sub_mute" && id != "mid_mute") {}
        for (int sc : scenarios)
        {
            const auto a = (id == "space") ? stereoCat (ref, sc) : renderScenario (ref, sc);
            for (float v : values)
            {
                if (v == ref[static_cast<P::Index> (i)]) continue;
                ParamSnapshot q = ref; q[static_cast<P::Index> (i)] = v;
                const auto b = (id == "space") ? stereoCat (q, sc) : renderScenario (q, sc);
                const double rel = rig::diffRmsRel (b, a);
                if (rel > best) { best = rel; via = "scenario " + std::to_string (sc) + " value " + std::to_string (v); }
            }
        }
        if (id != "layer_solo")
            for (float v : values)
            {
                ParamSnapshot q = ref; q[static_cast<P::Index> (i)] = v;
                if (! subCentred (q)) { std::fprintf (stderr, "sub not centred with %s = %f\n", id.c_str(), static_cast<double> (v)); return 1; }
            }
        const bool ok = best > thr;
        std::printf ("%-24s %s  diff %7.1f dB  (%s)\n", id.c_str(), ok ? "WIRED  " : "UNWIRED", 20.0 * std::log10 (best), via.c_str());
        if (! ok) ++unwired;
    }
    if (unwired) { std::fprintf (stderr, "%d UNWIRED parameter(s)\n", unwired); return 1; }

    // ---- 4. enable switches used by at least one factory preset
    const auto& set = factoryPresets();
    for (P::Index sw : { P::furnace_enabled, P::fracture_enabled, P::traj_enabled, P::anchor_enabled })
    {
        bool used = false;
        for (const auto& f : set.presets) used = used || f.data.params.flag (sw);
        if (! used) { std::fprintf (stderr, "NO-PRESET: %s never on in factory set\n", std::string (paramDef (sw).id).c_str()); return 1; }
    }
    std::puts ("WiringPerturbationTests passed");
    return 0;
}
