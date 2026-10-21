// Light QA over the factory library (docs/specs/qa-plan.md): every factory preset is rendered offline through the real
// SnaralEngine with the Core OfflineRenderer (174 bpm bass phrase, 24-bit-equivalent float render, no normalisation).
// Per preset it checks: finite samples, not silent, peak below full scale, no DC, sane loudness, tail decays to silence,
// sub layer sample-identical L/R (sub solo render), and prints one table row (copy it into docs/release/QA_REPORT.md).
// Run `snaral_factoryrenderqatests` directly to see the table.
#include "Core/OfflineRenderer.h"
#include "Engine/SnaralEngine.h"
#include "State/Pure/FactoryPresets.h"

#include <cmath>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace snaral;
using namespace snaral::render;

namespace
{
int failures = 0;

void fail (const std::string& preset, const std::string& what)
{
    std::fprintf (stderr, "FAIL %-22s %s\n", preset.c_str(), what.c_str());
    ++failures;
}

double db (double lin) { return 20.0 * std::log10 (std::max (lin, 1.0e-12)); }

MidiPhrase phrase()
{
    MidiPhrase p;
    // beats: root, octave jump, fifth, a short stab, then a held root at full velocity and a two-note overlap (poly / legato)
    p.notes = { { 0.0, 1.5, 36, 100, 0 }, { 2.0, 0.5, 48, 90, 0 }, { 2.5, 1.0, 43, 110, 0 }, { 4.0, 0.25, 36, 127, 0 },
                { 4.5, 2.0, 38, 120, 0 }, { 5.5, 1.5, 45, 100, 0 }, { 8.0, 1.0, 60, 100, 0 } };
    p.endBeats = 9.0;
    return p;
}
}

int main()
{
    const auto& set = factoryPresets();
    if (! set.problems.empty() || set.presets.size() < 24)
    {
        std::fprintf (stderr, "factory set invalid (%zu presets, %zu problems)\n", set.presets.size(), set.problems.size());
        return 1;
    }
    ExportSettings s;
    s.bpm = 174.0;
    s.sampleRate = 48000;
    s.tailSeconds = 1.5;

    std::printf ("%-3s %-20s %8s %8s %8s %9s %8s %6s %s\n", "#", "preset", "peak dB", "rms dB", "crest", "dc", "tail dB", "lat", "notes");
    int index = 0;
    for (const auto& f : set.presets)
    {
        ++index;
        const std::string& name = f.data.name;
        SnaralEngine eng;
        OfflineRenderer<SnaralEngine> r (eng);
        const auto out = r.renderToMemory (f.data.params, phrase(), s);
        if (out.status != ExportStatus::Ok) { fail (name, "render failed: " + out.message); continue; }

        const std::size_t n = out.left.size();
        double peak = 0.0, sumSq = 0.0, sum = 0.0, tailSq = 0.0;
        std::size_t active = 0, tailN = 0;
        const std::size_t tailStart = n > 4800 ? n - 4800 : 0; // last 100 ms
        bool finite = true;
        for (std::size_t i = 0; i < n; ++i)
        {
            const double l = out.left[i], rr = out.right[i];
            if (! std::isfinite (l) || ! std::isfinite (rr)) { finite = false; break; }
            peak = std::max ({ peak, std::fabs (l), std::fabs (rr) });
            const double m = 0.5 * (l + rr);
            sum += m;
            if (std::fabs (m) > 1.0e-4) { sumSq += m * m; ++active; }
            if (i >= tailStart) { tailSq += m * m; ++tailN; }
        }
        if (! finite) { fail (name, "NaN or Inf in output"); continue; }
        const double rms = active ? std::sqrt (sumSq / static_cast<double> (active)) : 0.0;
        const double dc = sum / static_cast<double> (std::max<std::size_t> (1, n));
        const double tail = std::sqrt (tailSq / static_cast<double> (std::max<std::size_t> (1, tailN)));

        // sub layer must be centred whatever the preset does to the mid (solo sub render)
        auto subOnly = f.data.params;
        subOnly[P::layer_solo] = 1.0f;
        SnaralEngine e2;
        OfflineRenderer<SnaralEngine> r2 (e2);
        const auto sub = r2.renderToMemory (subOnly, phrase(), s);
        bool centred = sub.status == ExportStatus::Ok && sub.left.size() == sub.right.size();
        for (std::size_t i = 0; centred && i < sub.left.size(); ++i)
            centred = sub.left[i] == sub.right[i];

        std::string notes;
        if (peak > 1.0) { notes += "CLIP "; fail (name, "peak above full scale: " + std::to_string (peak)); }
        else if (peak > 0.99) { notes += "near-full-scale "; }
        if (active < n / 20) { notes += "SILENT "; fail (name, "mostly silent"); }
        if (std::fabs (dc) > 0.01) { notes += "DC "; fail (name, "DC offset " + std::to_string (dc)); }
        if (db (rms) < -30.0 || db (rms) > -3.0) { notes += "LEVEL "; fail (name, "active RMS " + std::to_string (db (rms)) + " dBFS outside -30..-3"); }
        if (db (peak) < -24.0) { notes += "QUIET "; fail (name, "peak below -24 dBFS"); }
        if (db (tail) > -60.0) { notes += "TAIL "; fail (name, "tail not silent after 1.5 s: " + std::to_string (db (tail)) + " dB"); }
        if (! centred) { notes += "SUB-NOT-CENTRED "; fail (name, "sub layer L != R"); }
        std::printf ("%-3d %-20s %8.2f %8.2f %8.2f %9.5f %8.1f %6d %s\n", index, name.c_str(), db (peak), db (rms),
                     db (peak) - db (rms), dc, db (tail), out.latencyTrimmed, notes.c_str());
    }
    // ---- latency trim must follow the patch's oversampling mode (offline render alignment, QA bug Q6)
    {
        long onset[3] = { 0, 0, 0 };
        for (int mode = 0; mode < 3; ++mode)
        {
            auto patch = set.presets.front().data.params;
            patch[P::oversampling_mode] = static_cast<float> (mode);
            patch[P::drive_amount] = 0.0f;
            patch[P::layer_solo] = 1.0f;
            patch[P::sub_amp_attack_ms] = 0.0f;
            MidiPhrase one;
            one.notes = { { 0.0, 2.0, 48, 127, 0 } };
            one.endBeats = 2.0;
            SnaralEngine eng;
            OfflineRenderer<SnaralEngine> r (eng);
            const auto out = r.renderToMemory (patch, one, s);
            if (out.status != ExportStatus::Ok) { fail ("latency trim", "render failed"); continue; }
            if (out.latencyTrimmed != SnaralEngine::latencyFor (static_cast<OversamplingMode> (mode), 48000.0))
                fail ("latency trim", "mode " + std::to_string (mode) + " trimmed " + std::to_string (out.latencyTrimmed) + " samples, engine reports "
                                          + std::to_string (SnaralEngine::latencyFor (static_cast<OversamplingMode> (mode), 48000.0)));
            std::size_t i = 0;
            while (i < out.left.size() && std::fabs (out.left[i]) < 1.0e-3f) ++i;
            onset[mode] = static_cast<long> (i);
        }
        if (std::labs (onset[1] - onset[0]) > 2 || std::labs (onset[2] - onset[0]) > 2)
            fail ("latency trim", "note onset moves with oversampling: off " + std::to_string (onset[0]) + ", x2 " + std::to_string (onset[1]) + ", x4 " + std::to_string (onset[2]));
        std::printf ("latency trim: onset off/x2/x4 = %ld / %ld / %ld samples\n", onset[0], onset[1], onset[2]);
    }

    // ---- qa-plan Stage C matrix: every preset at notes 24/36/48/60 and velocities 64/127 (single held note, 2 s tail)
    // checks: finite, peak at or below the limiter ceiling + 0.1 dB, sub-solo L == R, sub-solo click bound (largest
    // sample-to-sample jump below 0.25 of the peak); CSV rows are written to $SNARAL_QA_CSV when set.
    std::FILE* csv = nullptr;
    if (const char* path = std::getenv ("SNARAL_QA_CSV")) csv = std::fopen (path, "w");
    if (csv) std::fputs ("preset,note,vel,rate,block,peak_dbfs,rms_dbfs,dc,max_jump,pass\n", csv);
    int renders = 0;
    for (const auto& f : set.presets)
        for (int note : { 24, 36, 48, 60 })
            for (int vel : { 64, 127 })
            {
                MidiPhrase one;
                one.notes = { { 0.0, 4.0, static_cast<std::uint8_t> (note), static_cast<std::uint8_t> (vel), 0 } };
                one.endBeats = 4.0;   // 1.38 s at 174 bpm
                ExportSettings es = s;
                es.tailSeconds = 2.0;
                SnaralEngine eng;
                OfflineRenderer<SnaralEngine> r (eng);
                const auto out = r.renderToMemory (f.data.params, one, es);
                const std::string tag = f.data.name + " note " + std::to_string (note) + " vel " + std::to_string (vel);
                bool pass = out.status == ExportStatus::Ok;
                double peak = 0.0, sumSq = 0.0, mean = 0.0, jump = 0.0;
                const std::size_t n = out.left.size();
                const std::size_t steadyFrom = std::min<std::size_t> (n, 14400), steadyTo = std::min<std::size_t> (n, 62400); // 0.3 s to 1.3 s of the held note
                double wSum = 0.0;
                for (std::size_t i = 0; pass && i < n; ++i)
                {
                    const double l = out.left[i], rr = out.right[i];
                    if (! std::isfinite (l) || ! std::isfinite (rr)) { pass = false; fail (tag, "non-finite sample"); break; }
                    peak = std::max ({ peak, std::fabs (l), std::fabs (rr) });
                    if (i >= steadyFrom && i < steadyTo)
                    {
                        // Hann-weighted mean: a bass period does not divide the window, a plain mean would leak the fundamental
                        const double w = 0.5 - 0.5 * std::cos (2.0 * 3.14159265358979323846 * static_cast<double> (i - steadyFrom) / static_cast<double> (steadyTo - steadyFrom));
                        sumSq += l * l; mean += w * l; wSum += w;
                    }
                    if (i > 0) jump = std::max (jump, std::fabs (l - out.left[i - 1]));
                }
                const double ceiling = std::pow (10.0, (f.data.params[P::limiter_ceiling_db] + 0.1) / 20.0);
                if (pass && f.data.params.flag (P::limiter_enabled) && peak > ceiling) { pass = false; fail (tag, "peak " + std::to_string (db (peak)) + " dBFS above ceiling + 0.1 dB"); }
                const double steadyN = static_cast<double> (std::max<std::size_t> (1, steadyTo - steadyFrom));
                const double dcMean = wSum > 0.0 ? mean / wSum : 0.0;
                if (pass && std::fabs (dcMean) > 1.0e-3) { pass = false; fail (tag, "DC " + std::to_string (dcMean)); }
                if (pass && vel == 127)
                {
                    auto subOnly = f.data.params;
                    subOnly[P::layer_solo] = 1.0f;
                    SnaralEngine e2;
                    OfflineRenderer<SnaralEngine> r2 (e2);
                    const auto sub = r2.renderToMemory (subOnly, one, es);
                    double sp = 0.0, sj = 0.0;
                    bool same = sub.status == ExportStatus::Ok && sub.left.size() == sub.right.size();
                    for (std::size_t i = 0; same && i < sub.left.size(); ++i)
                    {
                        same = sub.left[i] == sub.right[i];
                        sp = std::max (sp, static_cast<double> (std::fabs (sub.left[i])));
                        if (i > 0) sj = std::max (sj, static_cast<double> (std::fabs (sub.left[i] - sub.left[i - 1])));
                    }
                    if (! same) { pass = false; fail (tag, "sub layer L != R"); }
                    else if (sp > 1.0e-4 && sj > 0.25 * sp) { pass = false; fail (tag, "sub click: jump " + std::to_string (sj) + " vs peak " + std::to_string (sp)); }
                }
                if (csv)
                    std::fprintf (csv, "%s,%d,%d,48000,512,%.2f,%.2f,%.6f,%.4f,%s\n", f.data.name.c_str(), note, vel, db (peak),
                                  db (std::sqrt (sumSq / steadyN)), dcMean, jump, pass ? "pass" : "FAIL");
                ++renders;
            }
    if (csv) std::fclose (csv);
    std::printf ("matrix: %d renders (27 presets x 4 notes x 2 velocities)\n", renders);

    if (failures) { std::fprintf (stderr, "%d QA failure(s)\n", failures); return 1; }
    std::puts ("FactoryRenderQaTests passed");
    return 0;
}
