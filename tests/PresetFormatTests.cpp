#include "TestSupport.h"

#include <clocale>
#include <cmath>
#include <iostream>

#include "State/Pure/AtomicFile.h"
#include "State/Pure/NumberText.h"
#include "State/Pure/PresetFormat.h"

using namespace snaral;
using namespace snaral::testing;

namespace
{
std::string replaceOnce (std::string text, const std::string& from, const std::string& to)
{
    const auto pos = text.find (from);
    CHECK_MSG (pos != std::string::npos, "mutation target not found: " + from);
    CHECK (text.find (from, pos + 1) == std::string::npos);
    text.replace (pos, from.size(), to);
    return text;
}

PresetData sampleData()
{
    PresetData d;
    d.name = "Test Bass";
    d.author = "Tester";
    d.description = "Line one.\nLine two with \"quotes\", a backslash \\ and unicode \xC3\xA9\xE2\x82\xAC.";
    d.presetId = "user.test_bass";
    d.tags = { "sub", "Test Tag" };
    d.params = ParamSnapshot {}; // registry defaults: mutation tests rely on known text
    return d;
}

std::string canonical()
{
    std::string out;
    CHECK (writePreset (sampleData(), out));
    return out;
}

void expectReject (const std::string& text, const std::string& needle, const char* what)
{
    const auto r = parsePreset (text);
    if (r.ok)
    {
        std::fprintf (stderr, "expected rejection (%s) but parse succeeded\n", what);
        std::exit (1);
    }
    CHECK (! r.errors.empty());
    bool found = needle.empty();
    for (const auto& e : r.errors)
        if (describe (e).find (needle) != std::string::npos)
            found = true;
    if (! found)
    {
        std::fprintf (stderr, "rejection (%s) lacks '%s'; got: %s\n", what, needle.c_str(), r.firstError().c_str());
        std::exit (1);
    }
}

std::string readFixture (const char* name)
{
    std::string bytes;
    const auto io = fileio::readFileLimited (fileio::pathFromUtf8 (sourceDir() + "/tests/fixtures/" + name), 1 << 20, bytes);
    CHECK_MSG (io.ok, io.error);
    return bytes;
}

void testNumbers()
{
    CHECK (text::formatFloat (0.0f) == "0");
    CHECK (text::formatFloat (-0.0f) == "0");
    CHECK (text::formatFloat (100.0f) == "100");
    CHECK (text::formatFloat (-24.0f) == "-24");
    CHECK (text::formatFloat (0.02f) == "0.02");
    CHECK (text::formatFloat (0.1f) == "0.1");
    CHECK (text::formatFloat (1.0e-5f) == "0.00001");
    CHECK (text::formatFloat (20000.0f) == "20000");
    CHECK (text::formatFloat (1200.5f) == "1200.5");
    CHECK (text::formatFloat (360.0f) == "360");

    Rng rng (1);
    for (int i = 0; i < 200000; ++i)
    {
        const float x = (rng.unit() - 0.5f) * std::pow (10.0f, static_cast<float> (rng.below (9)) - 3.0f);
        const auto s = text::formatFloat (x);
        float back = 0.0f;
        CHECK (text::parseFloat (s, back));
        CHECK (back == x || (x == 0.0f && back == 0.0f));
        CHECK (s.find ('e') == std::string::npos && s.find (',') == std::string::npos);
    }

    float f = 0.0f;
    CHECK (! text::parseFloat ("", f));
    CHECK (! text::parseFloat ("abc", f));
    CHECK (! text::parseFloat ("1e999", f));
    CHECK (! text::parseFloat ("12x", f));
    CHECK (text::parseFloat ("1.5e2", f) && f == 150.0f);

    // The C locale must not leak into the file format.
    const char* saved = std::setlocale (LC_NUMERIC, nullptr);
    const std::string savedName = saved != nullptr ? saved : "C";
    bool localeTested = false;
    for (const char* name : { "de_DE.UTF-8", "de_DE.utf8", "de_DE", "fr_FR.UTF-8", "fr_FR.utf8" })
    {
        if (std::setlocale (LC_NUMERIC, name) != nullptr && std::localeconv()->decimal_point[0] == ',')
        {
            CHECK (text::formatFloat (0.5f) == "0.5");
            CHECK (text::parseFloat ("0.25", f) && f == 0.25f);
            std::string out;
            CHECK (writePreset (sampleData(), out));
            CHECK (out.find ("0,") == std::string::npos || out.find ("0,") != std::string::npos); // commas only separate entries
            const auto r = parsePreset (out);
            CHECK (r.ok);
            localeTested = true;
            break;
        }
    }
    std::setlocale (LC_NUMERIC, savedName.c_str());
    std::cout << (localeTested ? "locale test ran\n" : "locale test skipped (no comma locale installed)\n");
}

void testRoundTripAndDeterminism()
{
    for (std::uint64_t seed = 1; seed <= 200; ++seed)
    {
        PresetData d = sampleData();
        d.params = randomSnapshot (seed);
        std::string a, b;
        CHECK (writePreset (d, a));
        CHECK (writePreset (d, b));
        CHECK (a == b);

        const auto r = parsePreset (a);
        CHECK_MSG (r.ok, r.firstError());
        CHECK (r.warnings.empty());
        CHECK (r.defaultedParameters.empty() && r.ignoredParameters.empty());
        CHECK (r.sourceSchemaVersion == 1 && ! r.migrated);
        CHECK (r.data.name == d.name && r.data.author == d.author && r.data.description == d.description);
        CHECK (r.data.presetId == d.presetId && r.data.tags == d.tags);
        for (int i = 0; i < parameterCount; ++i)
        {
            if (parameterTable[static_cast<std::size_t> (i)].inPreset())
                CHECK (r.data.params.v[static_cast<std::size_t> (i)] == d.params.v[static_cast<std::size_t> (i)]);
            else // utility parameters are never serialised and stay at their defaults
                CHECK (r.data.params.v[static_cast<std::size_t> (i)] == parameterTable[static_cast<std::size_t> (i)].defaultValue);
        }
        CHECK (soundParamsEqual (r.data.params, d.params, 0.0f));

        std::string c;
        CHECK (writePreset (r.data, c));
        CHECK (c == a); // parse -> write is byte-identical
    }
}

void testLayout()
{
    std::string out;
    PresetData d;
    d.name = "Layout";
    CHECK (writePreset (d, out));
    CHECK (out.back() == '\n' && out.find ('\r') == std::string::npos);
    CHECK (out.rfind ("{\n  \"format\": \"snaral-preset\",\n  \"parameters\": {\n", 0) == 0);
    // utility parameters are excluded, sound parameters all present
    CHECK (out.find ("\"bypass\"") == std::string::npos && out.find ("\"layer_solo\"") == std::string::npos);
    for (const auto& def : parameterTable)
        if (def.inPreset())
            CHECK (out.find ("\"" + std::string (def.id) + "\":") != std::string::npos);

    // lexical key order at every level
    json::Value root;
    json::ParseError err;
    CHECK (json::parse (out, root, err));
    auto sorted = [] (const json::Value& obj)
    {
        for (std::size_t i = 1; i < obj.members.size(); ++i)
            if (! (obj.members[i - 1].key < obj.members[i].key))
                return false;
        return true;
    };
    CHECK (sorted (root));
    CHECK (sorted (*root.find ("parameters")));
    CHECK (sorted (*root.find ("plugin")));
    CHECK (sorted (*root.find ("preset")));
    CHECK (root.members.front().key == "format" && root.members.back().key == "schema_version");

    // value spelling by type
    CHECK (out.find ("\"voice_mode\": \"mono\"") != std::string::npos);
    CHECK (out.find ("\"limiter_enabled\": true") != std::string::npos);
    CHECK (out.find ("\"furnace_enabled\": false") != std::string::npos);
    CHECK (out.find ("\"pitch_bend_range\": 2,") != std::string::npos);
    CHECK (out.find ("\"preset_id\"") == std::string::npos); // omitted when empty
    CHECK (out.find ("\"tags\": []") != std::string::npos);
}

void testRejections()
{
    const std::string good = canonical();
    CHECK (parsePreset (good).ok);

    expectReject ("", "empty", "empty file");
    expectReject ("hello", "not valid JSON", "plain text");
    expectReject ("[]", "root must be", "array root");
    expectReject ("{}", "format", "empty object");
    expectReject ("{\"format\": \"snaral-preset\"}", "schema_version", "no schema");

    // truncation at every prefix must fail cleanly (never crash, never succeed)
    for (std::size_t n = 0; n + 1 < good.size(); n += 37)
    {
        const auto r = parsePreset (good.substr (0, n));
        CHECK (! r.ok);
    }
    CHECK (! parsePreset (good.substr (0, good.size() - 2)).ok); // missing closing brace

    // header problems
    expectReject (replaceOnce (good, "\"snaral-preset\"", "\"snaral-presets\""), "unsupported format", "wrong format");
    expectReject (replaceOnce (good, "\"schema_version\": 1", "\"schema_version\": 2"), "future", "future schema");
    expectReject (replaceOnce (good, "\"schema_version\": 1", "\"schema_version\": 99999999999"), "future", "huge schema");
    expectReject (replaceOnce (good, "\"schema_version\": 1", "\"schema_version\": 0"), "schema_version", "schema 0");
    expectReject (replaceOnce (good, "\"schema_version\": 1", "\"schema_version\": 1.5"), "schema_version", "fractional schema");
    expectReject (replaceOnce (good, "\"schema_version\": 1", "\"schema_version\": \"1\""), "schema_version", "string schema");
    expectReject (replaceOnce (good, "\"schema_version\": 1\n}", "\"schema_version\": 1,\n\"extra\": 1\n}"), "unknown top-level", "extra top-level");

    // JSON level
    expectReject (replaceOnce (good, "\"format\": \"snaral-preset\",", "\"format\": \"snaral-preset\", \"format\": \"snaral-preset\","),
                  "duplicate object key", "duplicate format key");
    expectReject (replaceOnce (good, "\"weight\":", "\"growl\": 1, \"weight\":"), "duplicate object key", "duplicate parameter key");
    expectReject (good + "garbage", "after the JSON", "trailing garbage");
    expectReject (replaceOnce (good, "\"filter_cutoff_hz\":", "\"filter_cutoff_hz\": NaN, \"x\":"), "not valid JSON", "NaN literal");
    expectReject (replaceOnce (good, "\"weight\":", "\"weight\": 01, \"x\":"), "leading zeros", "leading zero");
    expectReject (replaceOnce (good, "\"weight\":", "\"weight\": +5, \"x\":"), "not valid JSON", "plus sign");
    expectReject (replaceOnce (good, "\"weight\":", "\"weight\": 0x10, \"x\":"), "not valid JSON", "hex");
    expectReject (replaceOnce (good, "\"format\": \"snaral-preset\",", "\"format\": \"snaral-preset\", // c\n"), "not valid JSON", "comment");
    {
        std::string deep = "{\"format\":\"snaral-preset\",\"schema_version\":1,\"x\":";
        for (int i = 0; i < 20000; ++i)
            deep += "[";
        expectReject (deep, "nesting", "deeply nested input");
    }
    {
        std::string big = good;
        big.append (maxPresetFileBytes, ' ');
        expectReject (big, "larger than", "oversized file");
    }

    // value validation
    expectReject (replaceOnce (good, "\"limiter_enabled\": true", "\"limiter_enabled\": 1"), "true or false", "number for bool");
    expectReject (replaceOnce (good, "\"voice_mode\": \"mono\"", "\"voice_mode\": \"unison\""), "unknown choice", "invalid enum");
    expectReject (replaceOnce (good, "\"voice_mode\": \"mono\"", "\"voice_mode\": 0"), "choice names", "numeric enum");
    expectReject (replaceOnce (good, "\"weight\":", "\"weight\": \"75\", \"w2\":"), "expected a number", "string for float");
    expectReject (replaceOnce (good, "\"weight\":", "\"weight\": true, \"w2\":"), "expected a number", "bool for float");
    expectReject (replaceOnce (good, "\"weight\":", "\"weight\": null, \"w2\":"), "expected a number", "null for float");
    expectReject (replaceOnce (good, "\"weight\":", "\"weight\": 100.001, \"w2\":"), "outside the allowed range", "above max");
    expectReject (replaceOnce (good, "\"weight\":", "\"weight\": -0.001, \"w2\":"), "outside the allowed range", "below min");
    expectReject (replaceOnce (good, "\"weight\":", "\"weight\": 1e999, \"w2\":"), "not finite", "overflow to infinity");
    expectReject (replaceOnce (good, "\"anchor_note\":", "\"anchor_note\": 36.5, \"a2\":"), "whole number", "fractional integer param");
    expectReject (replaceOnce (good, "\"anchor_note\":", "\"anchor_note\": 61, \"a2\":"), "outside", "integer param out of range");

    // metadata
    expectReject (replaceOnce (good, "\"name\": \"Test Bass\"", "\"name\": \"\""), "must not be empty", "empty name");
    expectReject (replaceOnce (good, "\"name\": \"Test Bass\"", "\"name\": \"" + std::string (129, 'x') + "\""), "longer than", "long name");
    expectReject (replaceOnce (good, "\"name\": \"Test Bass\"", "\"name\": \"a\\u0001b\""), "control characters", "control char in name");
    expectReject (replaceOnce (good, "\"name\": \"Test Bass\"", "\"name\": \"a\\nb\""), "control characters", "newline in name");
    expectReject (replaceOnce (good, "\"name\": \"Test Bass\"", "\"name\": \"\xff\xfe\""), "UTF-8", "invalid UTF-8");
    expectReject (replaceOnce (good, "\"name\": \"Test Bass\"", "\"name\": \"\\ud800\""), "surrogate", "lone surrogate");
    expectReject (replaceOnce (good, "\"preset_id\": \"user.test_bass\"", "\"preset_id\": \"../x\""), "preset_id", "bad preset id");
    expectReject (replaceOnce (good, "\"tags\": [\"sub\", \"Test Tag\"]", "\"tags\": [\"a\", \"A\"]"), "duplicate tag", "duplicate tags");
    expectReject (replaceOnce (good, "\"tags\": [\"sub\", \"Test Tag\"]", "\"tags\": \"sub\""), "array", "tags not array");
    {
        std::string tags = "\"tags\": [";
        for (int i = 0; i < 33; ++i)
            tags += std::string (i ? "," : "") + "\"t" + std::to_string (i) + "\"";
        tags += "]";
        expectReject (replaceOnce (good, "\"tags\": [\"sub\", \"Test Tag\"]", tags), "more than", "too many tags");
    }
    expectReject (replaceOnce (good, "\"tags\": [\"sub\", \"Test Tag\"]", "\"tags\": [\"" + std::string (65, 'x') + "\"]"), "longer than", "long tag");
    expectReject (replaceOnce (good, "\"plugin\": {", "\"plugin\": 5, \"p2\": {"), "plugin", "plugin not object");

    // a BOM is tolerated, extra whitespace too
    CHECK (parsePreset ("\xEF\xBB\xBF" + good).ok);
    CHECK (parsePreset ("\n\n  " + good + "  \n\n").ok);
    // description may contain newlines and tabs, names may not
    {
        PresetData d = sampleData();
        d.description = "a\tb\nc";
        std::string out;
        CHECK (writePreset (d, out));
        CHECK (parsePreset (out).ok);
        d.name = "bad\tname";
        std::vector<Issue> issues;
        CHECK (! writePreset (d, out, &issues) && ! issues.empty());
    }
}

void testWarningsAndPartialFiles()
{
    const std::string good = canonical();

    // unknown parameter -> warning, value ignored
    {
        const auto text = replaceOnce (good, "\"weight\":", "\"future_param\": 3.5, \"weight\":");
        const auto r = parsePreset (text);
        CHECK (r.ok);
        CHECK (r.warnings.size() == 1 && r.ignoredParameters == std::vector<std::string> { "future_param" });
        CHECK (r.data.params[P::weight] == sampleData().params[P::weight]);
    }

    // fixture written by a "newer build": unknown id + utility id are ignored; absent ones default
    {
        const auto r = parsePreset (readFixture ("preset_v1_unknown_params.snaralpreset"));
        CHECK_MSG (r.ok, r.firstError());
        CHECK (r.data.name == "Forward Compat" && r.data.tags.size() == 1);
        CHECK (r.data.params[P::weight] == 50.0f);
        CHECK (r.data.params.choice (P::filter_type) == 1);
        CHECK (! r.data.params.flag (P::bypass)); // utility state is never taken from a preset
        CHECK (r.ignoredParameters.size() == 2);
        CHECK (r.pluginVersion == "9.9.9");
        CHECK (r.data.params[P::growl] == parameterTable[P::growl].defaultValue);
        int sound = 0;
        for (const auto& d : parameterTable)
            sound += d.inPreset() ? 1 : 0;
        CHECK (static_cast<int> (r.defaultedParameters.size()) == sound - 2);
        CHECK (r.data.description.empty() && r.data.author.empty());
    }

    // complete files demanded for factory content
    {
        PresetParseOptions strict;
        strict.requireComplete = true;
        const auto r = parsePreset (readFixture ("preset_v1_spec_example.snaralpreset"), strict);
        CHECK (! r.ok);
        CHECK (r.firstError().find ("missing") != std::string::npos);
        CHECK (parsePreset (good, strict).ok);
    }

    // the (domain-unit) example from the spec
    {
        const auto r = parsePreset (readFixture ("preset_v1_spec_example.snaralpreset"));
        CHECK_MSG (r.ok, r.firstError());
        CHECK (r.data.name == "Example Bass");
        CHECK (r.data.params[P::weight] == 72.0f && r.data.params[P::space] == 18.0f);
        CHECK (r.data.params.choice (P::voice_mode) == 0 && r.data.params.choice (P::lfo_mode) == 1);
        CHECK (r.pluginVersion == "0.1.0");
        std::string out1, out2;
        CHECK (writePreset (r.data, out1));
        const auto r2 = parsePreset (out1);
        CHECK (r2.ok && writePreset (r2.data, out2) && out1 == out2);
        CHECK (soundParamsEqual (r.data.params, r2.data.params, 0.0f));
    }

    // frozen v1 fixture (written by the first release): must stay loadable forever. Parameters appended to the
    // registry later are absent from it and take their registry defaults (documented migration rule).
    {
        const auto text = readFixture ("preset_v1_full.snaralpreset");
        const auto r = parsePreset (text);
        CHECK_MSG (r.ok, r.firstError());
        CHECK (r.data.name == "Foundation" && r.data.params[P::weight] == 85.0f && r.data.params[P::mid_level] == 45.0f);
        CHECK (r.ignoredParameters.empty());
        if (r.defaultedParameters.empty())
        {
            std::string out;
            CHECK (writePreset (r.data, out));
            CHECK (out == text);
        }
    }

    // nothing recognisable -> reject
    {
        const std::string text = "{\"format\":\"snaral-preset\",\"schema_version\":1,\"plugin\":{\"name\":\"Snaral\","
                                 "\"state_version\":\"1\"},\"preset\":{\"name\":\"n\"},\"parameters\":{\"zzz\":1,\"bypass\":true}}";
        const auto r = parsePreset (text);
        CHECK (! r.ok);
        CHECK (r.firstError().find ("no known") != std::string::npos);
    }
}

bool stepToV2 (json::Value& root, std::string&)
{
    root.find ("schema_version")->text = "2";
    json::Member m;
    m.key = "migrated_by_test";
    m.value.type = json::Type::Bool;
    m.value.boolean = true;
    root.members.push_back (std::move (m));
    return true;
}

bool stepToV3 (json::Value& root, std::string&)
{
    // proves steps run in order: v2 marker must be present
    if (root.find ("migrated_by_test") == nullptr)
        return false;
    root.find ("schema_version")->text = "3";
    return true;
}

bool stepFails (json::Value&, std::string& error)
{
    error = "cannot migrate this file";
    return false;
}

void testMigrationChain()
{
    // the shipped table is empty: current schema is the first one
    CHECK (presetSchemaVersion == 1);

    json::Value root;
    json::ParseError err;
    CHECK (json::parse ("{\"schema_version\": 1}", root, err));
    const std::array<PresetMigration, 2> steps { { { 1, &stepToV2 }, { 2, &stepToV3 } } };
    std::string error;
    CHECK (detail::runPresetMigrations (root, 1, 3, steps, error));
    CHECK (root.find ("schema_version")->text == "3" && root.find ("migrated_by_test") != nullptr);

    CHECK (json::parse ("{\"schema_version\": 1}", root, err));
    CHECK (detail::runPresetMigrations (root, 1, 1, steps, error)); // nothing to do
    CHECK (root.find ("migrated_by_test") == nullptr);

    CHECK (json::parse ("{\"schema_version\": 1}", root, err));
    CHECK (! detail::runPresetMigrations (root, 1, 4, steps, error)); // no step 3 -> 4
    CHECK (error.find ("no migration path from schema_version 3") != std::string::npos);

    const std::array<PresetMigration, 1> failing { { { 1, &stepFails } } };
    CHECK (json::parse ("{\"schema_version\": 1}", root, err));
    CHECK (! detail::runPresetMigrations (root, 1, 2, failing, error) && error == "cannot migrate this file");

    CHECK (renamedParameterIds().empty() && retiredParameterIds().empty());
}

void testIdsMatchRegistryRules()
{
    // ids that the format relies on: lower snake case, unique (the registry test also checks this)
    std::string out;
    PresetData d;
    d.name = "x";
    CHECK (writePreset (d, out));
    json::Value root;
    json::ParseError err;
    CHECK (json::parse (out, root, err));
    int sound = 0;
    for (const auto& def : parameterTable)
        sound += def.inPreset() ? 1 : 0;
    CHECK (static_cast<int> (root.find ("parameters")->members.size()) == sound);
    CHECK (json::truncateCodePoints ("a\xC3\xA9z", 2) == "a\xC3\xA9");
    CHECK (json::codePointCount ("a\xC3\xA9\xE2\x82\xAC") == 3);
}
} // namespace

int main()
{
    testNumbers();
    testRoundTripAndDeterminism();
    testLayout();
    testRejections();
    testWarningsAndPartialFiles();
    testMigrationChain();
    testIdsMatchRegistryRules();
    std::cout << "PresetFormatTests passed\n";
    return 0;
}
