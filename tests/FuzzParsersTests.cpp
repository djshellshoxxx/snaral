// Randomised robustness test for every JUCE-free parser that reads untrusted text: JSON, preset files, pack
// manifests, MIDI-learn maps, options files, paste text, numbers. Deterministic seeds; meant to be run under
// ASan/UBSan (cmake -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined"). Malformed, truncated and huge inputs must
// be rejected (or sanitised) without crashing, and anything accepted must be internally valid.
#include "TestSupport.h"

#include <cstring>
#include <string>

#include "Core/CoreOptions.h"
#include "Core/MidiLearn.h"
#include "Core/Randomizer.h"
#include "State/Pure/Json.h"
#include "State/Pure/NumberText.h"
#include "State/Pure/PresetFormat.h"
#include "State/Pure/PresetPack.h"

using namespace snaral;
using namespace snaral::testing;

namespace
{
const char* const interesting[] = { "\"", "\\", "\\u", "\\ud800", "\\udc00\\ud800", "{", "}", "[", "]", ",", ":", "-", "e", "E+", ".",
                                    "1e999", "-1e999", "nan", "NaN", "Infinity", "0x10", "00", "1.", "\xFF", "\xC0\x80", "\xED\xA0\x80",
                                    "\xF4\x90\x80\x80", "\0", "\n", "\r\n", "null", "true", "false", "99999999999999999999999" };

std::string mutate (const std::string& seed, Rng& rng)
{
    std::string s = seed;
    const int ops = 1 + rng.below (6);
    for (int k = 0; k < ops; ++k)
    {
        switch (rng.below (8))
        {
            case 0: if (! s.empty()) s[static_cast<std::size_t> (rng.below (static_cast<int> (s.size())))] ^= static_cast<char> (1 << rng.below (8)); break;
            case 1: if (! s.empty()) s.erase (static_cast<std::size_t> (rng.below (static_cast<int> (s.size()))), static_cast<std::size_t> (1 + rng.below (40))); break;
            case 2: s.insert (static_cast<std::size_t> (rng.below (static_cast<int> (s.size()) + 1)), 1, static_cast<char> (rng.below (256))); break;
            case 3: { const char* t = interesting[rng.below (static_cast<int> (sizeof (interesting) / sizeof (*interesting)))];
                      s.insert (static_cast<std::size_t> (rng.below (static_cast<int> (s.size()) + 1)), t, std::strlen (t) + (t[0] == 0 ? 1u : 0u)); break; }
            case 4: if (! s.empty()) s.resize (static_cast<std::size_t> (rng.below (static_cast<int> (s.size())))); break;
            case 5: if (s.size() > 4) { const auto a = static_cast<std::size_t> (rng.below (static_cast<int> (s.size()) - 2));
                                        const auto len = static_cast<std::size_t> (1 + rng.below (static_cast<int> (s.size() - a - 1)));
                                        s.insert (static_cast<std::size_t> (rng.below (static_cast<int> (s.size()))), s.substr (a, len)); } break;
            case 6: s += std::string (static_cast<std::size_t> (rng.below (300)), static_cast<char> (rng.below (256))); break;
            default: if (! s.empty()) s[static_cast<std::size_t> (rng.below (static_cast<int> (s.size())))] = static_cast<char> (rng.below (256)); break;
        }
    }
    return s;
}

void checkPreset (const std::string& text)
{
    const auto r = parsePreset (text);
    if (r.ok)
    {
        CHECK (r.errors.empty());
        CHECK (snapshotIsValid (r.data.params));
        std::vector<Issue> issues;
        CHECK (validatePresetMetadata (r.data, issues));
        std::string out;
        CHECK (writePreset (r.data, out)); // anything accepted can be written back and re-read identically
        const auto again = parsePreset (out);
        CHECK (again.ok);
        CHECK (soundParamsEqual (r.data.params, again.data.params));
        CHECK (again.data.name == r.data.name && again.data.tags == r.data.tags);
    }
    else
        CHECK (! r.errors.empty());
}

void checkManifest (const std::string& text)
{
    const auto r = parsePackManifest (text);
    if (r.ok)
    {
        CHECK (r.errors.empty());
        CHECK (validPackId (r.manifest.packId));
        CHECK (r.manifest.presetCount == static_cast<int> (r.manifest.files.size()));
        CHECK (r.manifest.files.size() <= maxPackPresets);
        for (const auto& f : r.manifest.files)
            CHECK (validPackFileName (f));
    }
    else
        CHECK (! r.errors.empty());
}

void checkMidiMap (const std::string& text)
{
    const auto r = midi::fromText (text);
    if (r.ok)
    {
        CHECK (midi::validateMappings (r.data.mappings).empty());
        CHECK (r.data.mappings.size() <= 16);
        const auto back = midi::fromText (midi::toText (r.data));
        CHECK (back.ok && back.data.mappings.size() == r.data.mappings.size());
        auto table = midi::MidiLearnTable::build (r.data.mappings);
        CHECK (table != nullptr);
    }
    else
        CHECK (r.data.mappings.empty());
}

void checkOptions (const std::string& text)
{
    const auto r = optionsFromText (text);
    CHECK (r.options.validate().empty()); // always usable
}

void checkPaste (const std::string& text, Rng& rng)
{
    const int idx = rng.below (parameterCount);
    const auto r = rnd::parsePasteText (idx, text);
    if (r.ok)
    {
        const auto& d = parameterTable[static_cast<std::size_t> (idx)];
        CHECK (std::isfinite (r.value) && r.value >= d.minimum * 0.999f - 1e-3f && r.value <= d.maximum * 1.001f + 1e-3f);
    }
}

void checkNumbers (const std::string& text)
{
    float f = 0; double d = 0; long long i = 0; std::uint32_t seed = 0;
    if (text::parseFloat (text, f)) CHECK (std::isfinite (f));
    if (text::parseDouble (text, d)) CHECK (std::isfinite (d));
    (void) text::parseInt (text, i);
    (void) rnd::seedFromHex (text, seed);
}

void checkJson (const std::string& text)
{
    json::Value v;
    json::ParseError e;
    json::ParseOptions o;
    o.maxBytes = 1 << 20;
    o.maxDepth = 8;
    if (json::parse (text, v, e, o))
        CHECK (e.message.empty());
    else
        CHECK (! e.message.empty());
}

void runAll (const std::string& s, Rng& rng)
{
    checkJson (s); checkPreset (s); checkManifest (s); checkMidiMap (s); checkOptions (s); checkPaste (s, rng); checkNumbers (s);
}

std::string readFile (const std::string& path)
{
    std::FILE* f = std::fopen (path.c_str(), "rb");
    CHECK_MSG (f != nullptr, path);
    std::string s;
    char buf[4096];
    std::size_t n;
    while ((n = std::fread (buf, 1, sizeof buf, f)) > 0)
        s.append (buf, n);
    std::fclose (f);
    return s;
}
} // namespace

int main()
{
    const std::string src = SNARAL_SOURCE_DIR;
    std::vector<std::string> presetSeeds;
    for (const char* f : { "tests/fixtures/preset_v1_full.snaralpreset", "tests/fixtures/preset_v1_spec_example.snaralpreset",
                           "tests/fixtures/preset_v1_unknown_params.snaralpreset", "Resources/presets/12_neuro_growl.json" })
        presetSeeds.push_back (readFile (src + "/" + f));
    { std::string out; PresetData d; d.name = "Fuzz"; d.tags = { "a", "b" }; CHECK (writePreset (d, out)); presetSeeds.push_back (out); }

    PackManifest m;
    m.packId = "fuzz.pack"; m.name = "Fuzz"; m.version = "1.0.0"; m.author = "x"; m.license = "CC0";
    m.minPluginVersion = "0.2.0"; m.files = { "a.snaralpreset", "b.snaralpreset" }; m.presetCount = 2;
    std::string manifestText;
    CHECK (writePackManifest (m, manifestText));
    const std::string midiText = "snaral-midimap 1\nmap target=weight cc=20 ch=0 min=0 max=1 invert=0\nmap target=growl cc=21 ch=3 min=0.25 max=0.75 invert=1\n";
    const std::string optText = "snaral-options 1\ntheme=cdl\ntooltips=1\nanimations=0\nscale=1.25\nonboarding_seen=1\n";

    // seeds themselves must be accepted
    for (const auto& p : presetSeeds) CHECK (parsePreset (p).ok);
    CHECK (parsePackManifest (manifestText).ok);
    CHECK (midi::fromText (midiText).ok);
    CHECK (optionsFromText (optText).headerOk);

    Rng rng (0xF00D);
    constexpr int iterations = 12000;
    for (int it = 0; it < iterations; ++it)
    {
        const auto& p = presetSeeds[static_cast<std::size_t> (rng.below (static_cast<int> (presetSeeds.size())))];
        runAll (mutate (p, rng), rng);
        runAll (mutate (manifestText, rng), rng);
        runAll (mutate (midiText, rng), rng);
        runAll (mutate (optText, rng), rng);
        if (it % 4 == 0) // purely random short garbage
        {
            std::string g (static_cast<std::size_t> (rng.below (64)), ' ');
            for (auto& c : g) c = static_cast<char> (rng.below (256));
            runAll (g, rng);
        }
    }

    // every strict prefix (at least two bytes short) of valid documents is rejected
    for (const auto& p : presetSeeds)
        for (std::size_t len = 0; len + 2 <= p.size(); len += (p.size() > 3000 ? 7 : 1))
            CHECK (! parsePreset (std::string_view (p).substr (0, len)).ok);
    for (std::size_t len = 0; len + 2 <= manifestText.size(); ++len)
        CHECK (! parsePackManifest (std::string_view (manifestText).substr (0, len)).ok);

    // hostile shapes
    {
        const std::string deepA(100000, '['), deepO = [] { std::string s; for (int i = 0; i < 50000; ++i) s += "{\"a\":"; return s; }();
        runAll (deepA, rng); runAll (deepO, rng);
        runAll (std::string (3 * 1024 * 1024, 'x'), rng);                       // oversize
        runAll ("{\"format\":\"snaral-preset\",\"x\":\"" + std::string (900 * 1024, 'a') + "\"}", rng); // big string, under the cap
        std::string many = "{\"format\":\"snaral-preset\",\"schema_version\":1,\"parameters\":{";
        for (int i = 0; i < 40000; ++i) many += "\"k" + std::to_string (i) + "\":1,";
        many += "\"weight\":5},\"plugin\":{},\"preset\":{}}";
        runAll (many, rng);
        std::string longNum = "{\"schema_version\":" + std::string (100000, '9') + "}";
        runAll (longNum, rng);
        std::string manyMaps = "snaral-midimap 1\n";
        for (int i = 0; i < 100000; ++i) manyMaps += "map target=weight cc=20 ch=0 min=0 max=1 invert=0\n";
        runAll (manyMaps, rng);
        runAll (std::string ("snaral-midimap 1\nmap ") + std::string (500000, 'z'), rng);
        runAll (std::string ("snaral-options 1\ntheme=") + std::string (500000, 'z'), rng);
    }
    std::puts ("FuzzParsersTests passed");
    return 0;
}
