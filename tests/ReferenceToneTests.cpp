#include "TestSupport.h"

#include <cmath>
#include <iostream>
#include <vector>

#include "Core/ReferenceTone.h"

using namespace snaral;
using namespace snaral::testing;

namespace
{
void testPluginNeverEmits()
{
    ReferenceTone t (ReferenceTone::Mode::Plugin);
    CHECK (! t.setEnabled (true) && ! t.enabled());
    t.prepare (48000.0);
    std::vector<float> l (4800, 0.0f), r (4800, 0.0f);
    t.process (l.data(), r.data(), 4800);
    for (float v : l) CHECK (v == 0.0f);
    for (float v : r) CHECK (v == 0.0f);
    ReferenceTone allowed (ReferenceTone::Mode::Plugin, true);
    CHECK (allowed.setEnabled (true));   // explicit future opt-in only
}

void testStandaloneOffByDefault()
{
    ReferenceTone t (ReferenceTone::Mode::Standalone);
    CHECK (! t.enabled() && ! t.active());
    t.prepare (48000.0);
    std::vector<float> l (480, 0.0f), r (480, 0.0f);
    t.process (l.data(), r.data(), 480);
    for (float v : l) CHECK (v == 0.0f);
}

void testLevelFrequencyAndFades()
{
    for (double sr : { 44100.0, 48000.0, 96000.0 })
        for (float db : { -12.0f, -18.0f, -6.0f })
        {
            ReferenceTone t (ReferenceTone::Mode::Standalone);
            t.prepare (sr);
            t.setLevelDb (db);
            CHECK (t.setEnabled (true));
            const int n = static_cast<int> (sr);   // 1 s
            std::vector<float> l (static_cast<std::size_t> (n), 0.0f), r (static_cast<std::size_t> (n), 0.0f);
            for (int off = 0; off < n; off += 480)
                t.process (l.data() + off, r.data() + off, std::min (480, n - off));
            // fade-in: no step at the start, ramps up
            CHECK (std::abs (l[0]) < 0.01f);
            // steady-state peak and RMS after the 20 ms fade
            const int from = static_cast<int> (sr * 0.05);
            double peak = 0, sum = 0; int cnt = 0;
            for (int i = from; i < n; ++i) { peak = std::max (peak, static_cast<double> (std::abs (l[static_cast<std::size_t> (i)]))); sum += l[static_cast<std::size_t> (i)] * l[static_cast<std::size_t> (i)]; ++cnt; }
            const double peakDb = 20.0 * std::log10 (peak);
            CHECK_MSG (std::abs (peakDb - db) < 0.1, std::to_string (peakDb));
            const double rmsDb = 20.0 * std::log10 (std::sqrt (sum / cnt));
            CHECK (std::abs (rmsDb - (db - 3.0103)) < 0.1);
            // frequency by zero crossings (rising)
            int crossings = 0;
            for (int i = from + 1; i < n; ++i) if (l[static_cast<std::size_t> (i - 1)] < 0 && l[static_cast<std::size_t> (i)] >= 0) ++crossings;
            const double f = crossings / ((n - from) / sr);
            CHECK (std::abs (f - 440.0) < 2.0);
            for (int i = 0; i < n; ++i) CHECK (l[static_cast<std::size_t> (i)] == r[static_cast<std::size_t> (i)]);

            // stop: click-free fade out to exact silence
            t.stop();
            std::vector<float> l2 (static_cast<std::size_t> (n / 2), 0.0f), r2 (l2.size(), 0.0f);
            t.process (l2.data(), r2.data(), static_cast<int>(l2.size()));
            float maxStep = 0;
            for (std::size_t i = 1; i < l2.size(); ++i) maxStep = std::max (maxStep, std::abs (l2[i] - l2[i - 1]));
            CHECK (maxStep < 0.1f * std::pow (10.0f, db / 20.0f) * 3.0f);   // no jump larger than a fraction of the tone amplitude
            CHECK (l2.back() == 0.0f && ! t.active());
        }
}

void testAdditiveAndAlias()
{
    ReferenceTone t (ReferenceTone::Mode::Standalone);
    t.prepare (48000.0);
    t.setEnabled (true);
    std::vector<float> buf (4800, 0.25f);
    t.process (buf.data(), buf.data(), 4800);       // aliased channels: tone added once
    std::vector<float> a (4800, 0.25f), b (4800, 0.25f);
    ReferenceTone u (ReferenceTone::Mode::Standalone);
    u.prepare (48000.0); u.setEnabled (true);
    u.process (a.data(), b.data(), 4800);
    for (int i = 0; i < 4800; ++i) CHECK (buf[static_cast<std::size_t> (i)] == a[static_cast<std::size_t> (i)] && a[static_cast<std::size_t> (i)] == b[static_cast<std::size_t> (i)]);
    t.setLevelDb (std::nanf (""));
    CHECK (t.levelDb() == ReferenceTone::defaultLevelDb);
    t.setLevelDb (10.0f);
    CHECK (t.levelDb() == 0.0f);
}
} // namespace

int main()
{
    testPluginNeverEmits();
    testStandaloneOffByDefault();
    testLevelFrequencyAndFades();
    testAdditiveAndAlias();
    std::cout << "ReferenceToneTests OK\n";
    return 0;
}
