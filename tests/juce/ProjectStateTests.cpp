// JUCE-dependent tests for the host project-state codec (schema v2) and its APVTS compatibility.
// Plain main(), assert-style, no GUI.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>

#include <juce_audio_processors/juce_audio_processors.h>

#include "Parameters/JuceParameterLayout.h"
#include "State/ProjectState.h"
#include "../TestSupport.h"

using namespace snaral;
using namespace snaral::testing;
namespace pj = snaral::project;

namespace
{
class DummyProcessor : public juce::AudioProcessor
{
public:
    DummyProcessor()
        : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
        , apvts (*this, nullptr, "SnaralState", createParameterLayout())
    {
    }

    const juce::String getName() const override { return "Dummy"; }
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

    juce::AudioProcessorValueTreeState apvts;
};

juce::String s (const std::string& v) { return juce::String::fromUTF8 (v.c_str()); }

juce::MemoryBlock toBlob (const juce::ValueTree& tree)
{
    juce::MemoryBlock block;
    const auto xml = tree.createXml();
    CHECK (xml != nullptr);
    juce::AudioProcessor::copyXmlToBinary (*xml, block);
    return block;
}

pj::DecodeResult decodeTree (const juce::ValueTree& tree)
{
    const auto block = toBlob (tree);
    return pj::decode (block.getData(), static_cast<int> (block.getSize()));
}

pj::ProjectModel randomModel (std::uint64_t seed)
{
    Rng rng (seed);
    pj::ProjectModel m;
    m.params = randomSnapshot (seed);
    m.presetName = "Preset \xC3\xA9\xE2\x82\xAC #" + std::to_string (seed);
    m.presetId = "user:Preset " + std::to_string (seed) + ".snaralpreset";
    m.presetModified = rng.below (2) != 0;
    for (int i = 0; i < variationSlotCount; ++i)
    {
        if (rng.below (4) != 0)
            m.variations.store (i, randomSnapshot (seed * 10 + static_cast<std::uint64_t> (i)));
        if (rng.below (2) != 0)
            m.variations.rename (i, "Slot <" + std::to_string (i) + "> & \"q\"");
    }
    m.variations.setActiveIndex (rng.below (variationSlotCount));
    if (rng.below (3) != 0)
    {
        m.ab.restore (rng.below (2) != 0, rng.below (2) != 0 ? AbCompare::Side::A : AbCompare::Side::B,
                      randomSnapshot (seed + 1), randomSnapshot (seed + 2), rng.below (2) != 0);
    }
    juce::ValueTree ml (pj::midiLearnType);
    for (int i = 0; i < 3; ++i)
    {
        juce::ValueTree map ("MAP");
        map.setProperty ("cc", 20 + i, nullptr);
        map.setProperty ("param", "weight", nullptr);
        map.setProperty ("scale", 0.5 + i, nullptr);
        ml.appendChild (map, nullptr);
    }
    m.midiLearn = ml;
    juce::ValueTree ui (pj::uiType);
    ui.setProperty ("theme", "dark", nullptr);
    ui.setProperty ("scale", 1.25, nullptr);
    ui.setProperty ("tourDone", true, nullptr);
    juce::ValueTree panel ("PANEL");
    panel.setProperty ("open", 3, nullptr);
    ui.appendChild (panel, nullptr);
    m.ui = ui;
    return m;
}

void expectModelsEqual (const pj::ProjectModel& a, const pj::ProjectModel& b)
{
    for (std::size_t i = 0; i < a.params.v.size(); ++i)
        CHECK_MSG (a.params.v[i] == b.params.v[i], std::string (parameterTable[i].id));
    CHECK (a.presetName == b.presetName && a.presetId == b.presetId && a.presetModified == b.presetModified);
    CHECK (a.variations == b.variations);
    CHECK (a.ab == b.ab);
    CHECK (a.midiLearn.isEquivalentTo (b.midiLearn));
    CHECK (a.ui.isEquivalentTo (b.ui));
}

void expectReject (const pj::DecodeResult& r, const std::string& needle, const char* what)
{
    if (r.ok)
    {
        std::fprintf (stderr, "expected rejection (%s) but decode succeeded\n", what);
        std::exit (1);
    }
    bool found = needle.empty();
    for (const auto& e : r.errors)
        found = found || describe (e).find (needle) != std::string::npos;
    if (! found)
    {
        std::fprintf (stderr, "rejection (%s) lacks '%s'; got: %s\n", what, needle.c_str(), r.firstError().c_str());
        std::exit (1);
    }
}

juce::ValueTree goodTree()
{
    return pj::toValueTree (randomModel (77));
}

juce::ValueTree findParam (juce::ValueTree tree, const char* id)
{
    for (const auto child : tree)
        if (child.hasType (pj::paramType) && child.getProperty ("id").toString() == id)
            return child;
    CHECK_MSG (false, std::string ("PARAM not found: ") + id);
    return {};
}

juce::ValueTree makeV1 (float weight = 75.0f, float growl = 65.0f, float bite = 20.0f, float motion = 0.0f, float space = 0.0f)
{
    juce::ValueTree v1 ("SnaralState");
    v1.setProperty ("schemaVersion", 1, nullptr);
    const std::pair<const char*, float> macros[] = { { "weight", weight }, { "growl", growl }, { "bite", bite },
                                                      { "motion", motion }, { "space", space } };
    for (const auto& [id, value] : macros)
    {
        juce::ValueTree p ("PARAM");
        p.setProperty ("id", id, nullptr);
        p.setProperty ("value", value, nullptr); // APVTS stores a float var
        v1.appendChild (p, nullptr);
    }
    return v1;
}

void testRoundTrip()
{
    {
        const auto model = pj::defaultModel();
        juce::MemoryBlock blob;
        pj::encode (model, blob);
        const auto r = pj::decode (blob.getData(), static_cast<int> (blob.getSize()));
        CHECK_MSG (r.ok, r.firstError());
        CHECK (r.warnings.empty() && ! r.migrated && r.sourceSchemaVersion == 2);
        expectModelsEqual (model, r.model);
        CHECK (snapshotsIdentical (r.model.params, ParamSnapshot {}));
        std::cout << "default state blob: " << blob.getSize() << " bytes\n";
    }

    std::size_t biggest = 0;
    for (std::uint64_t seed = 1; seed <= 60; ++seed)
    {
        const auto model = randomModel (seed);
        juce::MemoryBlock blob;
        pj::encode (model, blob);
        biggest = std::max (biggest, blob.getSize());
        CHECK (blob.getSize() < static_cast<std::size_t> (pj::maxStateBytes));
        const auto r = pj::decode (blob.getData(), static_cast<int> (blob.getSize()));
        CHECK_MSG (r.ok, r.firstError());
        CHECK_MSG (r.warnings.empty(), r.warnings.empty() ? "" : describe (r.warnings.front()));
        expectModelsEqual (model, r.model);

        // idempotent: re-encoding the decoded model yields the same bytes
        juce::MemoryBlock again;
        pj::encode (r.model, again);
        CHECK (again == blob);
    }
    std::cout << "largest full state blob: " << biggest << " bytes (cap " << pj::maxStateBytes << ")\n";
}

void testStrictRejection()
{
    CHECK (decodeTree (goodTree()).ok);
    expectReject (pj::decode (nullptr, 10), "empty", "null data");
    expectReject (pj::decode ("abc", 0), "empty", "zero size");
    expectReject (pj::decode ("not a juce blob at all, just text", 33), "", "garbage");

    // truncated blobs
    const auto blob = toBlob (goodTree());
    for (std::size_t n = 0; n + 1 < blob.getSize(); n += 97)
        CHECK (! pj::decode (blob.getData(), static_cast<int> (n)).ok);
    CHECK (! pj::decode (blob.getData(), static_cast<int> (blob.getSize()) - 1).ok);

    // size cap
    {
        juce::MemoryBlock big (static_cast<std::size_t> (pj::maxStateBytes) + 1, true);
        expectReject (pj::decode (big.getData(), static_cast<int> (big.getSize())), "larger", "oversize");
    }

    // nesting bomb in the XML text is refused before the recursive parser runs
    {
        juce::String text;
        const int depth = 100000;
        for (int i = 0; i < depth; ++i)
            text << "<a>";
        juce::MemoryOutputStream out;
        out.writeInt (0x21324356);
        out.writeInt (text.getNumBytesAsUTF8());
        out.write (text.toRawUTF8(), text.getNumBytesAsUTF8() + 1);
        expectReject (pj::decode (out.getData(), static_cast<int> (out.getDataSize())), "nested", "xml nesting bomb");
    }

    // root + version
    expectReject (pj::validateAndMigrate ({}), "root", "invalid tree");
    expectReject (pj::validateAndMigrate (juce::ValueTree ("Other")), "root", "wrong root");
    {
        auto t = goodTree();
        t.removeProperty ("schemaVersion", nullptr);
        expectReject (pj::validateAndMigrate (t), "schemaVersion", "missing version");
        t.setProperty ("schemaVersion", 3, nullptr);
        expectReject (pj::validateAndMigrate (t), "future", "future version");
        t.setProperty ("schemaVersion", 99999, nullptr);
        expectReject (pj::validateAndMigrate (t), "future", "far future version");
        t.setProperty ("schemaVersion", 0, nullptr);
        expectReject (pj::validateAndMigrate (t), "schemaVersion", "version 0");
        t.setProperty ("schemaVersion", "two", nullptr);
        expectReject (pj::validateAndMigrate (t), "schemaVersion", "text version");
        t.setProperty ("schemaVersion", 2.5, nullptr);
        expectReject (pj::validateAndMigrate (t), "schemaVersion", "fractional version");
    }

    // parameter values
    auto mutateParam = [] (const char* id, const juce::var& value, const std::string& needle, const char* what)
    {
        auto t = goodTree();
        findParam (t, id).setProperty ("value", value, nullptr);
        expectReject (pj::validateAndMigrate (t), needle, what);
    };
    mutateParam ("weight", "100.5", "allowed range", "above max");
    mutateParam ("weight", -0.1, "allowed range", "below min");
    mutateParam ("weight", "nan", "finite", "nan");
    mutateParam ("weight", "inf", "finite", "inf");
    mutateParam ("weight", "", "finite", "empty text");
    mutateParam ("weight", "12abc", "finite", "trailing junk");
    mutateParam ("weight", juce::var(), "finite", "void value");
    mutateParam ("weight", "1e999", "finite", "overflow");
    mutateParam ("voice_mode", 2, "allowed range", "choice out of range");
    mutateParam ("voice_mode", 0.5, "whole number", "fractional choice");
    mutateParam ("limiter_enabled", 2, "allowed range", "bool out of range");
    mutateParam ("anchor_note", 36.5, "whole number", "fractional integer");
    mutateParam ("anchor_note", 70, "allowed range", "integer out of range");
    mutateParam ("filter_cutoff_hz", 5, "allowed range", "cutoff below min");
    {
        auto t = goodTree();
        auto dup = t.getChild (0).createCopy();
        t.appendChild (dup, nullptr);
        expectReject (pj::validateAndMigrate (t), "duplicate", "duplicate PARAM");
    }
    {
        auto t = goodTree();
        t.getChild (0).setProperty ("id", "", nullptr); // unknown id is a warning, not an error
        const auto r = pj::validateAndMigrate (t);
        CHECK (r.ok && ! r.warnings.empty());
    }

    // singleton nodes
    {
        auto t = goodTree();
        t.appendChild (t.getChildWithName (pj::uiType).createCopy(), nullptr);
        expectReject (pj::validateAndMigrate (t), "duplicate", "two UI nodes");
        t = goodTree();
        t.appendChild (t.getChildWithName (pj::variationsType).createCopy(), nullptr);
        expectReject (pj::validateAndMigrate (t), "duplicate", "two VARIATIONS");
        t = goodTree();
        t.appendChild (t.getChildWithName (pj::presetType).createCopy(), nullptr);
        expectReject (pj::validateAndMigrate (t), "duplicate", "two PRESET");
    }

    // preset node
    {
        auto t = goodTree();
        t.getChildWithName (pj::presetType).setProperty ("name", "bad\nname", nullptr);
        expectReject (pj::validateAndMigrate (t), "preset name", "control char in name");
        t.getChildWithName (pj::presetType).setProperty ("name", s (std::string (129, 'x')), nullptr);
        expectReject (pj::validateAndMigrate (t), "preset name", "long name");
        t = goodTree();
        t.getChildWithName (pj::presetType).setProperty ("modified", 7, nullptr);
        expectReject (pj::validateAndMigrate (t), "modified", "bad flag");
    }

    // variations
    {
        auto slotOf = [] (juce::ValueTree t, int i) { return t.getChildWithName (pj::variationsType).getChild (i); };
        auto t = goodTree();
        t.getChildWithName (pj::variationsType).setProperty ("active", 4, nullptr);
        expectReject (pj::validateAndMigrate (t), "VARIATIONS.active", "active 4");
        t = goodTree();
        slotOf (t, 1).setProperty ("index", 4, nullptr);
        expectReject (pj::validateAndMigrate (t), "SLOT.index", "slot index 4");
        t = goodTree();
        slotOf (t, 1).setProperty ("index", 0, nullptr);
        expectReject (pj::validateAndMigrate (t), "duplicate slot", "duplicate slot");
        t = goodTree();
        slotOf (t, 0).setProperty ("name", "bad\tname", nullptr);
        expectReject (pj::validateAndMigrate (t), "slot name", "control char in slot name");
        t = goodTree();
        slotOf (t, 0).setProperty ("name", s (std::string (40, 'x')), nullptr);
        expectReject (pj::validateAndMigrate (t), "slot name", "long slot name");

        // find a used slot and an empty slot in the sample state
        auto model = randomModel (77);
        for (int i = 0; i < variationSlotCount; ++i)
            model.variations.store (i, randomSnapshot (900 + static_cast<std::uint64_t> (i)));
        model.variations.clear (2);
        t = pj::toValueTree (model);
        CHECK (pj::validateAndMigrate (t).ok);
        auto used = slotOf (t, 0);
        auto values = used.getChildWithName (pj::valuesType);
        values.setProperty ("weight", 1000, nullptr);
        expectReject (pj::validateAndMigrate (t), "SLOT 0.weight", "slot value out of range");
        values.setProperty ("weight", 10, nullptr);
        values.setProperty ("voice_mode", "poly?", nullptr);
        expectReject (pj::validateAndMigrate (t), "SLOT 0.voice_mode", "slot bad choice");
        t = pj::toValueTree (model);
        slotOf (t, 0).removeChild (slotOf (t, 0).getChildWithName (pj::valuesType), nullptr);
        expectReject (pj::validateAndMigrate (t), "no VALUES", "used slot without values");
        t = pj::toValueTree (model);
        slotOf (t, 2).appendChild (pj::snapshotToValuesTree (ParamSnapshot {}), nullptr);
        expectReject (pj::validateAndMigrate (t), "empty slot must not hold values", "empty slot with values");
        t = pj::toValueTree (model);
        slotOf (t, 3).setProperty ("empty", "maybe", nullptr);
        expectReject (pj::validateAndMigrate (t), "empty", "bad empty flag");
    }

    // A/B
    {
        auto model = randomModel (5);
        model.ab.restore (true, AbCompare::Side::B, randomSnapshot (1), randomSnapshot (2), true);
        auto t = pj::toValueTree (model);
        CHECK (pj::validateAndMigrate (t).ok);
        auto ab = t.getChildWithName (pj::abType);
        ab.removeChild (ab.getChild (1), nullptr);
        expectReject (pj::validateAndMigrate (t), "both SIDE", "A/B with one side");
        t = pj::toValueTree (model);
        t.getChildWithName (pj::abType).setProperty ("active", 5, nullptr);
        expectReject (pj::validateAndMigrate (t), "ABCOMPARE.active", "A/B bad side");
        t = pj::toValueTree (model);
        t.getChildWithName (pj::abType).getChild (0).getChildWithName (pj::valuesType).setProperty ("bite", 500, nullptr);
        expectReject (pj::validateAndMigrate (t), "bite", "A/B value out of range");
    }

    // opaque subtrees: depth, node count, value types, size
    {
        auto t = goodTree();
        auto ui = t.getChildWithName (pj::uiType);
        ui.setProperty ("blob", juce::var (juce::MemoryBlock ("abc", 3)), nullptr);
        expectReject (pj::validateAndMigrate (t), "unsupported value type", "binary property");
        ui.removeProperty ("blob", nullptr);
        ui.setProperty ("nested", juce::var (juce::Array<juce::var> { 1, 2 }), nullptr);
        expectReject (pj::validateAndMigrate (t), "unsupported value type", "array property");
        ui.removeProperty ("nested", nullptr);
        ui.setProperty ("nan", std::nan (""), nullptr);
        expectReject (pj::validateAndMigrate (t), "non-finite", "nan property");
        ui.removeProperty ("nan", nullptr);
        ui.setProperty ("long", s (std::string (5000, 'x')), nullptr);
        expectReject (pj::validateAndMigrate (t), "too long", "huge text property");
        ui.removeProperty ("long", nullptr);

        juce::ValueTree deep ("N");
        auto cursor = deep;
        for (int i = 0; i < 20; ++i)
        {
            juce::ValueTree child ("N");
            cursor.appendChild (child, nullptr);
            cursor = child;
        }
        ui.appendChild (deep, nullptr);
        expectReject (pj::validateAndMigrate (t), "deeply", "deep ui tree");
        ui.removeChild (deep, nullptr);

        for (int i = 0; i < pj::maxOpaqueNodes + 5; ++i)
            ui.appendChild (juce::ValueTree ("N"), nullptr);
        expectReject (pj::validateAndMigrate (t), "too many", "many nodes");
    }

    // unknown nodes and parameters are ignored with warnings; absent parameters take defaults
    {
        auto t = goodTree();
        t.appendChild (juce::ValueTree ("FUTURE_THING"), nullptr);
        juce::ValueTree extra ("PARAM");
        extra.setProperty ("id", "param_from_the_future", nullptr);
        extra.setProperty ("value", "3", nullptr);
        t.appendChild (extra, nullptr);
        findParam (t, "growl").getParent().removeChild (findParam (t, "growl"), nullptr);
        const auto r = pj::validateAndMigrate (t);
        CHECK_MSG (r.ok, r.firstError());
        CHECK (r.warnings.size() >= 3);
        CHECK (r.model.params[P::growl] == parameterTable[P::growl].defaultValue);
    }
}

void testMigrationFromV1()
{
    // valid legacy state, binary round trip exactly as the old processor wrote it
    {
        const auto v1 = makeV1 (80.0f, 40.0f, 10.0f, 55.5f, 12.25f);
        const auto r = decodeTree (v1);
        CHECK_MSG (r.ok, r.firstError());
        CHECK (r.migrated && r.sourceSchemaVersion == 1);
        CHECK (r.model.params[P::weight] == 80.0f && r.model.params[P::growl] == 40.0f && r.model.params[P::bite] == 10.0f);
        CHECK (r.model.params[P::motion] == 55.5f && r.model.params[P::space] == 12.25f);
        for (int i = 5; i < parameterCount; ++i) // every non-macro parameter takes the registry default
            CHECK_MSG (r.model.params.v[static_cast<std::size_t> (i)] == parameterTable[static_cast<std::size_t> (i)].defaultValue,
                       std::string (parameterTable[static_cast<std::size_t> (i)].id));
        for (int i = 0; i < variationSlotCount; ++i)
            CHECK (r.model.variations.isEmpty (i));
        CHECK (r.model.presetName.empty() && ! r.model.presetModified && ! r.model.ab.valid());

        // saving again writes schema 2
        juce::MemoryBlock blob;
        pj::encode (r.model, blob);
        const auto r2 = pj::decode (blob.getData(), static_cast<int> (blob.getSize()));
        CHECK (r2.ok && ! r2.migrated && r2.sourceSchemaVersion == 2);
        expectModelsEqual (r.model, r2.model);
    }

    // schemaVersion stored as text (what XML round trips produce) and as default-value state
    {
        auto v1 = makeV1();
        v1.setProperty ("schemaVersion", "1", nullptr);
        const auto r = pj::validateAndMigrate (v1);
        CHECK (r.ok && r.migrated);
        CHECK (r.model.params[P::weight] == 75.0f && r.model.params[P::growl] == 65.0f);
    }

    // legacy validation rules still apply
    auto expectV1Reject = [] (juce::ValueTree t, const std::string& needle, const char* what)
    {
        const auto r = pj::validateAndMigrate (t);
        expectReject (r, needle, what);
    };
    {
        auto t = makeV1();
        t.removeChild (4, nullptr);
        expectV1Reject (t, "exactly the five", "missing macro");
        t = makeV1();
        t.appendChild (t.getChild (0).createCopy(), nullptr);
        expectV1Reject (t, "exactly the five", "six children");
        t = makeV1();
        t.getChild (1).setProperty ("id", "weight", nullptr);
        expectV1Reject (t, "duplicate", "duplicate macro");
        t = makeV1();
        t.getChild (1).setProperty ("id", "filter_cutoff_hz", nullptr);
        expectV1Reject (t, "unknown parameter in schema 1", "non-macro id in v1");
        t = makeV1();
        t.getChild (0).setProperty ("value", 100.01f, nullptr);
        expectV1Reject (t, "allowed range", "macro out of range");
        t = makeV1();
        t.getChild (0).setProperty ("value", "nan", nullptr);
        expectV1Reject (t, "finite", "macro nan");
        t = makeV1();
        t.getChild (0).removeProperty ("value", nullptr);
        expectV1Reject (t, "finite", "macro without value");
        t = makeV1();
        t.getChild (0).setProperty ("value", "12x", nullptr);
        expectV1Reject (t, "finite", "macro garbage text");
        t = makeV1();
        t.removeChild (0, nullptr);
        t.appendChild (juce::ValueTree ("OTHER"), nullptr);
        expectV1Reject (t, "only PARAM", "foreign child");
    }
}

void testApvtsCompatibility()
{
    DummyProcessor proc;
    auto& apvts = proc.apvts;

    // fresh processor: registry defaults
    const auto fresh = pj::snapshotFromApvts (apvts);
    for (int i = 0; i < parameterCount; ++i)
        CHECK_MSG (std::abs (fresh.v[static_cast<std::size_t> (i)] - parameterTable[static_cast<std::size_t> (i)].defaultValue) <= 1e-3f
                       * (std::abs (parameterTable[static_cast<std::size_t> (i)].defaultValue) + 1.0f),
                   std::string (parameterTable[static_cast<std::size_t> (i)].id));

    // a whole v2 tree published through replaceState lands in the parameters (the processor's atomic publish)
    for (std::uint64_t seed = 1; seed <= 20; ++seed)
    {
        const auto model = randomModel (seed);
        apvts.replaceState (pj::toValueTree (model));
        const auto live = pj::snapshotFromApvts (apvts);
        for (int i = 0; i < parameterCount; ++i)
        {
            const auto& d = parameterTable[static_cast<std::size_t> (i)];
            const float expected = model.params.v[static_cast<std::size_t> (i)];
            const float got = live.v[static_cast<std::size_t> (i)];
            CHECK_MSG (std::abs (d.toNormalized (got) - d.toNormalized (expected)) <= 1.0e-6f
                           || (d.isInteger() && got == expected),
                       std::string (d.id) + " expected " + std::to_string (expected) + " got " + std::to_string (got));
        }
        // the live APVTS tree (with the extra children) is itself a decodable candidate once it carries the version
        auto copy = apvts.copyState();
        CHECK (copy.getChildWithName (pj::variationsType).isValid());
        copy.setProperty (pj::schemaVersionProperty, 2, nullptr);
        const auto r = pj::validateAndMigrate (copy);
        CHECK_MSG (r.ok, r.firstError());
    }

    // snapshots can be applied directly; utility parameters are left alone unless asked for
    {
        ParamSnapshot target = randomSnapshot (321);
        target[P::bypass] = 1.0f;
        const auto before = pj::snapshotFromApvts (apvts);
        pj::applySnapshot (apvts, target, true);
        const auto after = pj::snapshotFromApvts (apvts);
        CHECK (after[P::bypass] == before[P::bypass]);
        CHECK (after[P::weight] == target[P::weight] || std::abs (after[P::weight] - target[P::weight]) < 1e-3f);
        CHECK (std::abs (after[P::filter_cutoff_hz] - target[P::filter_cutoff_hz]) < 1e-3f * target[P::filter_cutoff_hz]);
        CHECK (after.choice (P::oversampling_mode) == target.choice (P::oversampling_mode));
        pj::applySnapshot (apvts, target, false);
        CHECK (pj::snapshotFromApvts (apvts)[P::bypass] == 1.0f);
    }

    // a failed decode never touches the processor: validation is pure and happens first
    {
        const auto before = pj::snapshotFromApvts (apvts);
        auto bad = goodTree();
        findParam (bad, "weight").setProperty ("value", "5000", nullptr);
        const auto r = pj::validateAndMigrate (bad);
        CHECK (! r.ok);
        CHECK (snapshotsIdentical (pj::snapshotFromApvts (apvts), before));
    }
}
} // namespace

int main()
{
    testRoundTrip();
    testStrictRejection();
    testMigrationFromV1();
    testApvtsCompatibility();
    std::cout << "ProjectStateTests passed\n";
    return 0;
}
