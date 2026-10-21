#include "ProjectState.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <set>

#include "Pure/NumberText.h"

namespace snaral::project
{
namespace
{
using juce::Identifier;
using juce::ValueTree;

const Identifier idProperty { "id" };
const Identifier valueProperty { "value" };
const Identifier nameProperty { "name" };
const Identifier presetIdProperty { "presetId" };
const Identifier modifiedProperty { "modified" };
const Identifier activeProperty { "active" };
const Identifier indexProperty { "index" };
const Identifier emptyProperty { "empty" };
const Identifier engagedProperty { "engaged" };
const Identifier matchTrimProperty { "matchTrim" };

constexpr std::size_t maxIssuesPerKind = 50;
constexpr int maxRootChildren = 4096;

juce::String toJuce (std::string_view s)
{
    return juce::String::fromUTF8 (s.data(), static_cast<int> (s.size()));
}

std::string toStd (const juce::String& s)
{
    return std::string (s.toRawUTF8());
}

struct Collector
{
    std::vector<Issue>& errors;
    std::vector<Issue>& warnings;

    void error (std::string path, std::string message)
    {
        if (errors.size() < maxIssuesPerKind)
            errors.push_back ({ std::move (path), std::move (message) });
    }

    void warn (std::string path, std::string message)
    {
        if (warnings.size() < maxIssuesPerKind)
            warnings.push_back ({ std::move (path), std::move (message) });
    }
};

bool varToDouble (const juce::var& v, double& out)
{
    if (v.isInt() || v.isInt64() || v.isDouble())
    {
        out = static_cast<double> (v);
        return std::isfinite (out);
    }
    if (v.isBool())
    {
        out = static_cast<bool> (v) ? 1.0 : 0.0;
        return true;
    }
    if (v.isString())
        return text::parseDouble (toStd (v.toString()), out);
    return false;
}

bool varToInt (const juce::var& v, long long& out)
{
    if (v.isInt() || v.isInt64())
    {
        out = static_cast<long long> (static_cast<juce::int64> (v));
        return true;
    }
    if (v.isBool())
    {
        out = static_cast<bool> (v) ? 1 : 0;
        return true;
    }
    if (v.isDouble())
    {
        const double d = static_cast<double> (v);
        if (! std::isfinite (d) || d != std::floor (d) || std::abs (d) > 1e15)
            return false;
        out = static_cast<long long> (d);
        return true;
    }
    if (v.isString())
        return text::parseInt (toStd (v.toString()), out);
    return false;
}

bool varToBool (const juce::var& v, bool& out)
{
    long long i = 0;
    if (! varToInt (v, i) || (i != 0 && i != 1))
        return false;
    out = i == 1;
    return true;
}

bool readParamValue (const ParamDef& d, const juce::var& v, const std::string& path, Collector& c, float& out)
{
    float f = 0.0f;
    if (v.isString())
    {
        if (! text::parseFloat (toStd (v.toString()), f))
        {
            c.error (path, "missing or not a finite number");
            return false;
        }
    }
    else
    {
        double x = 0.0;
        if (! varToDouble (v, x))
        {
            c.error (path, "missing or not a finite number");
            return false;
        }
        if (std::abs (x) > static_cast<double> (FLT_MAX))
        {
            c.error (path, "value is outside the allowed range");
            return false;
        }
        f = static_cast<float> (x);
    }
    if (f < d.minimum || f > d.maximum)
    {
        c.error (path, "value " + text::formatFloat (f) + " is outside the allowed range ["
                           + text::formatFloat (d.minimum) + ", " + text::formatFloat (d.maximum) + "]");
        return false;
    }
    if (d.isInteger())
    {
        if (std::abs (f - std::round (f)) > 1e-4f)
        {
            c.error (path, "expected a whole number, got " + text::formatFloat (f));
            return false;
        }
        f = std::round (f);
    }
    out = f;
    return true;
}

/// Decodes sound-parameter properties of a VALUES node. Unknown ids warn; missing ids default (counted).
void decodeValues (const ValueTree& values, const std::string& path, Collector& c, bool reportMissing,
                   ParamSnapshot& out)
{
    std::array<bool, static_cast<std::size_t> (parameterCount)> seen {};
    for (int i = 0; i < values.getNumProperties(); ++i)
    {
        const auto key = toStd (values.getPropertyName (i).toString());
        const auto p = path + "." + key;
        const int index = findParameterIndex (key);
        if (index < 0)
        {
            c.warn (p, "unknown parameter ignored");
            continue;
        }
        const auto& d = parameterTable[static_cast<std::size_t> (index)];
        if (! d.inPreset())
        {
            c.warn (p, "utility parameter is not stored here; ignored");
            continue;
        }
        float f = d.defaultValue;
        if (readParamValue (d, values.getProperty (values.getPropertyName (i)), p, c, f))
            out.v[static_cast<std::size_t> (index)] = f;
        seen[static_cast<std::size_t> (index)] = true;
    }
    int missing = 0;
    for (int i = 0; i < parameterCount; ++i)
        if (parameterTable[static_cast<std::size_t> (i)].inPreset() && ! seen[static_cast<std::size_t> (i)])
            ++missing;
    if (missing > 0 && reportMissing)
        c.warn (path, std::to_string (missing) + " parameter(s) absent; registry defaults used");
}

// ---- opaque subtree (UI, MIDILEARN) -------------------------------------------------------------------------
bool validateOpaqueNode (const ValueTree& node, int depth, int& nodeCount, const std::string& path, Collector& c)
{
    if (depth > maxOpaqueDepth)
    {
        c.error (path, "nested too deeply");
        return false;
    }
    if (++nodeCount > maxOpaqueNodes)
    {
        c.error (path, "too many nodes");
        return false;
    }
    if (! node.getType().isValid())
    {
        c.error (path, "node without a type");
        return false;
    }
    for (int i = 0; i < node.getNumProperties(); ++i)
    {
        const auto name = node.getPropertyName (i);
        const auto& v = node.getProperty (name);
        const auto p = path + "@" + toStd (name.toString());
        if (v.isString())
        {
            if (v.toString().length() > 4096)
            {
                c.error (p, "text too long");
                return false;
            }
        }
        else if (v.isDouble())
        {
            if (! std::isfinite (static_cast<double> (v)))
            {
                c.error (p, "non-finite number");
                return false;
            }
        }
        else if (! (v.isInt() || v.isInt64() || v.isBool()))
        {
            c.error (p, "unsupported value type (only text, numbers and booleans are allowed)");
            return false;
        }
    }
    for (const auto child : node)
        if (! validateOpaqueNode (child, depth + 1, nodeCount, path + "/" + toStd (child.getType().toString()), c))
            return false;
    return true;
}

bool validateOpaque (const ValueTree& tree, const Identifier& expected, Collector& c, ValueTree& out)
{
    const auto path = toStd (expected.toString());
    int count = 0;
    if (! validateOpaqueNode (tree, 0, count, path, c))
        return false;
    if (const auto xml = tree.createXml())
        if (xml->toString (juce::XmlElement::TextFormat().singleLine()).length() > static_cast<int> (maxOpaqueChildBytes))
        {
            c.error (path, "subtree is too large");
            return false;
        }
    out = tree.createCopy();
    return true;
}

// ---- name validation ---------------------------------------------------------------------------------------
bool noControlChars (const std::string& s)
{
    for (const char ch : s)
        if (static_cast<unsigned char> (ch) < 0x20 || ch == 0x7F)
            return false;
    return true;
}

// ---- v1 -> v2 -----------------------------------------------------------------------------------------------
/// Validates the legacy 5-macro state with exactly the rules of schema v1 (docs/specs/parameter-state.md) and, if
/// valid, returns the same values wrapped in a v2 tree. All other parameters will take registry defaults.
bool migrateV1 (const ValueTree& v1, ValueTree& v2, Collector& c)
{
    static constexpr std::array<std::string_view, 5> macroIds { "weight", "growl", "bite", "motion", "space" };
    if (v1.getNumChildren() != static_cast<int> (macroIds.size()))
    {
        c.error ("", "schema 1 must contain exactly the five macro parameters");
        return false;
    }
    std::array<bool, 5> seen {};
    v2 = ValueTree (rootType);
    v2.setProperty (schemaVersionProperty, 2, nullptr);
    for (const auto child : v1)
    {
        if (! child.hasType (paramType))
        {
            c.error ("", "schema 1 allows only PARAM children");
            return false;
        }
        const auto id = toStd (child.getProperty (idProperty).toString());
        std::size_t slot = macroIds.size();
        for (std::size_t i = 0; i < macroIds.size(); ++i)
            if (macroIds[i] == id)
                slot = i;
        if (slot == macroIds.size())
        {
            c.error ("PARAM " + id, "unknown parameter in schema 1");
            return false;
        }
        if (seen[slot])
        {
            c.error ("PARAM " + id, "duplicate parameter");
            return false;
        }
        const auto& d = parameterTable[static_cast<std::size_t> (findParameterIndex (id))];
        float f = 0.0f;
        if (! readParamValue (d, child.getProperty (valueProperty), "PARAM " + id, c, f))
            return false;
        seen[slot] = true;
        ValueTree p (paramType);
        p.setProperty (idProperty, toJuce (id), nullptr);
        p.setProperty (valueProperty, toJuce (text::formatFloat (f)), nullptr);
        v2.appendChild (p, nullptr);
    }
    return true;
}

// ---- cheap XML nesting pre-scan (guards the recursive XML parser against hostile blobs) -------------------
bool xmlNestingWithinLimit (const char* text, std::size_t size, int limit)
{
    int depth = 0;
    for (std::size_t i = 0; i < size; ++i)
    {
        if (text[i] != '<')
            continue;
        if (i + 1 >= size)
            break;
        const char next = text[i + 1];
        if (next == '?' || next == '!')
            continue;
        if (next == '/')
        {
            if (depth > 0)
                --depth;
            continue;
        }
        ++depth;
        if (depth > limit)
            return false;
        // find end of tag, honouring quotes; a trailing '/' closes immediately
        char quote = 0;
        std::size_t j = i + 1;
        for (; j < size; ++j)
        {
            const char ch = text[j];
            if (quote != 0)
            {
                if (ch == quote)
                    quote = 0;
            }
            else if (ch == '"' || ch == '\'')
            {
                quote = ch;
            }
            else if (ch == '>')
            {
                break;
            }
        }
        if (j < size && j > 0 && text[j - 1] == '/')
            --depth;
        i = j;
    }
    return true;
}
} // namespace

ProjectModel defaultModel()
{
    return ProjectModel {};
}

DecodeResult validateAndMigrate (const ValueTree& candidateIn)
{
    DecodeResult r;
    Collector c { r.errors, r.warnings };

    if (! candidateIn.isValid() || ! candidateIn.hasType (rootType))
    {
        c.error ("", "not a Snaral state (root must be SnaralState)");
        return r;
    }
    long long version = 0;
    if (! candidateIn.hasProperty (schemaVersionProperty) || ! varToInt (candidateIn.getProperty (schemaVersionProperty), version))
    {
        c.error ("schemaVersion", "missing or not an integer");
        return r;
    }
    r.sourceSchemaVersion = static_cast<int> (std::clamp<long long> (version, -1, 1000000));
    if (version < 1)
    {
        c.error ("schemaVersion", "must be 1 or higher");
        return r;
    }
    if (version > currentSchemaVersion)
    {
        c.error ("schemaVersion", "unsupported future schemaVersion " + std::to_string (version)
                                      + " (this build reads up to " + std::to_string (currentSchemaVersion) + ")");
        return r;
    }

    ValueTree candidate = candidateIn;
    if (version == 1)
    {
        ValueTree migrated;
        if (! migrateV1 (candidateIn, migrated, c))
            return r;
        candidate = migrated;
        r.migrated = true;
    }

    if (candidate.getNumChildren() > maxRootChildren)
    {
        c.error ("", "too many child nodes");
        return r;
    }

    ProjectModel m;
    std::array<bool, static_cast<std::size_t> (parameterCount)> seen {};
    bool havePreset = false, haveVariations = false, haveAb = false, haveMidi = false, haveUi = false;

    for (const auto child : candidate)
    {
        const auto type = child.getType();
        const auto typeName = toStd (type.toString());
        if (type == paramType)
        {
            const auto id = toStd (child.getProperty (idProperty).toString());
            const auto path = "PARAM " + id;
            const int index = findParameterIndex (id);
            if (index < 0)
            {
                c.warn (path, "unknown parameter ignored");
                continue;
            }
            const auto idx = static_cast<std::size_t> (index);
            if (seen[idx])
            {
                c.error (path, "duplicate parameter");
                continue;
            }
            seen[idx] = true;
            float f = parameterTable[idx].defaultValue;
            if (readParamValue (parameterTable[idx], child.getProperty (valueProperty), path, c, f))
                m.params.v[idx] = f;
        }
        else if (type == presetType)
        {
            if (havePreset)
            {
                c.error (typeName, "duplicate node");
                continue;
            }
            havePreset = true;
            const auto name = toStd (child.getProperty (nameProperty).toString());
            if (! json::isValidUtf8 (name) || ! noControlChars (name) || json::codePointCount (name) > maxPresetNameChars)
                c.error ("PRESET.name", "invalid preset name");
            else
                m.presetName = name;
            const auto pid = toStd (child.getProperty (presetIdProperty).toString());
            if (! json::isValidUtf8 (pid) || ! noControlChars (pid) || pid.size() > 256)
                c.error ("PRESET.presetId", "invalid preset id");
            else
                m.presetId = pid;
            if (child.hasProperty (modifiedProperty) && ! varToBool (child.getProperty (modifiedProperty), m.presetModified))
                c.error ("PRESET.modified", "must be 0 or 1");
        }
        else if (type == variationsType)
        {
            if (haveVariations)
            {
                c.error (typeName, "duplicate node");
                continue;
            }
            haveVariations = true;
            long long active = 0;
            if (child.hasProperty (activeProperty) && (! varToInt (child.getProperty (activeProperty), active) || active < 0
                                                       || active >= variationSlotCount))
                c.error ("VARIATIONS.active", "must be an integer 0-3");
            else
                m.variations.setActiveIndex (static_cast<int> (active));

            std::array<bool, variationSlotCount> slotSeen {};
            for (const auto slot : child)
            {
                if (! slot.hasType (slotType))
                {
                    c.warn ("VARIATIONS", "unknown child " + toStd (slot.getType().toString()) + " ignored");
                    continue;
                }
                long long index = -1;
                if (! varToInt (slot.getProperty (indexProperty), index) || index < 0 || index >= variationSlotCount)
                {
                    c.error ("SLOT.index", "must be an integer 0-3");
                    continue;
                }
                const auto si = static_cast<std::size_t> (index);
                const auto spath = "SLOT " + std::to_string (index);
                if (slotSeen[si])
                {
                    c.error (spath, "duplicate slot");
                    continue;
                }
                slotSeen[si] = true;

                const auto name = toStd (slot.getProperty (nameProperty).toString());
                std::string cleanName;
                if (! VariationBank::normaliseName (name, cleanName))
                    c.error (spath + ".name", "invalid slot name");
                else
                    m.variations.rename (static_cast<int> (index), cleanName);

                bool isEmpty = true;
                if (! slot.hasProperty (emptyProperty) || ! varToBool (slot.getProperty (emptyProperty), isEmpty))
                {
                    c.error (spath + ".empty", "missing or not 0/1");
                    continue;
                }
                const auto values = slot.getChildWithName (valuesType);
                if (isEmpty)
                {
                    if (values.isValid())
                        c.error (spath, "empty slot must not hold values");
                    continue;
                }
                if (! values.isValid())
                {
                    c.error (spath, "slot is marked used but has no VALUES");
                    continue;
                }
                ParamSnapshot snap;
                decodeValues (values, spath, c, ! r.migrated, snap);
                m.variations.store (static_cast<int> (index), snap);
            }
        }
        else if (type == abType)
        {
            if (haveAb)
            {
                c.error (typeName, "duplicate node");
                continue;
            }
            haveAb = true;
            bool engaged = false, matchTrim = true;
            long long active = 0;
            if (! varToBool (child.getProperty (engagedProperty), engaged))
                c.error ("ABCOMPARE.engaged", "missing or not 0/1");
            if (child.hasProperty (matchTrimProperty) && ! varToBool (child.getProperty (matchTrimProperty), matchTrim))
                c.error ("ABCOMPARE.matchTrim", "must be 0 or 1");
            if (! varToInt (child.getProperty (activeProperty), active) || (active != 0 && active != 1))
                c.error ("ABCOMPARE.active", "must be 0 or 1");
            std::array<ParamSnapshot, 2> sides;
            std::array<bool, 2> sideSeen {};
            for (const auto side : child)
            {
                long long index = -1;
                if (! side.hasType (abSideType) || ! varToInt (side.getProperty (indexProperty), index) || (index != 0 && index != 1))
                {
                    c.error ("ABCOMPARE", "children must be SIDE nodes with index 0 or 1");
                    continue;
                }
                if (sideSeen[static_cast<std::size_t> (index)])
                {
                    c.error ("ABCOMPARE", "duplicate SIDE");
                    continue;
                }
                sideSeen[static_cast<std::size_t> (index)] = true;
                const auto values = side.getChildWithName (valuesType);
                if (! values.isValid())
                {
                    c.error ("ABCOMPARE.SIDE", "missing VALUES");
                    continue;
                }
                decodeValues (values, "ABCOMPARE.SIDE " + std::to_string (index), c, ! r.migrated,
                              sides[static_cast<std::size_t> (index)]);
            }
            if (! sideSeen[0] || ! sideSeen[1])
                c.error ("ABCOMPARE", "both SIDE 0 and SIDE 1 are required");
            else if (r.errors.empty())
                m.ab.restore (engaged, active == 0 ? AbCompare::Side::A : AbCompare::Side::B, sides[0], sides[1], matchTrim);
        }
        else if (type == midiLearnType)
        {
            if (haveMidi)
            {
                c.error (typeName, "duplicate node");
                continue;
            }
            haveMidi = true;
            validateOpaque (child, midiLearnType, c, m.midiLearn);
        }
        else if (type == uiType)
        {
            if (haveUi)
            {
                c.error (typeName, "duplicate node");
                continue;
            }
            haveUi = true;
            validateOpaque (child, uiType, c, m.ui);
        }
        else
        {
            c.warn (typeName, "unknown node ignored");
        }
    }

    int missing = 0;
    for (std::size_t i = 0; i < seen.size(); ++i)
        if (! seen[i])
            ++missing;
    if (missing > 0 && ! r.migrated)
        c.warn ("", std::to_string (missing) + " parameter(s) absent; registry defaults used");
    if (r.migrated)
        c.warn ("", "migrated from schema 1; " + std::to_string (missing) + " parameters took registry defaults");

    if (r.errors.empty())
    {
        r.ok = true;
        r.model = std::move (m);
    }
    return r;
}

DecodeResult decode (const void* data, int sizeInBytes)
{
    DecodeResult r;
    if (data == nullptr || sizeInBytes <= 0)
    {
        r.errors.push_back ({ "", "empty state" });
        return r;
    }
    if (sizeInBytes > maxStateBytes)
    {
        r.errors.push_back ({ "", "state is larger than " + std::to_string (maxStateBytes / 1024) + " KiB" });
        return r;
    }
    const auto* bytes = static_cast<const char*> (data);
    // JUCE binary XML: 4-byte magic, 4-byte length, XML text. Guard the recursive parser first.
    if (sizeInBytes > 8 && ! xmlNestingWithinLimit (bytes + 8, static_cast<std::size_t> (sizeInBytes - 8), 64))
    {
        r.errors.push_back ({ "", "state is nested too deeply" });
        return r;
    }
    // JUCE clamps the declared text length to the available bytes, so a truncated blob could parse as a shorter,
    // "valid" document. Require the whole declared payload to be present.
    if (sizeInBytes <= 8 || juce::ByteOrder::littleEndianInt (bytes) != 0x21324356u)
    {
        r.errors.push_back ({ "", "state is truncated or not a valid Snaral state blob" });
        return r;
    }
    const auto declared = static_cast<juce::int64> (static_cast<juce::uint32> (juce::ByteOrder::littleEndianInt (bytes + 4)));
    if (declared <= 0 || declared > static_cast<juce::int64> (sizeInBytes) - 8)
    {
        r.errors.push_back ({ "", "state is truncated or not a valid Snaral state blob" });
        return r;
    }
    const auto xml = juce::AudioProcessor::getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr)
    {
        r.errors.push_back ({ "", "state is truncated or not a valid Snaral state blob" });
        return r;
    }
    const auto tree = ValueTree::fromXml (*xml);
    if (! tree.isValid())
    {
        r.errors.push_back ({ "", "state XML could not be read" });
        return r;
    }
    return validateAndMigrate (tree);
}

ValueTree snapshotToValuesTree (const ParamSnapshot& snapshot)
{
    ValueTree values (valuesType);
    for (int i = 0; i < parameterCount; ++i)
    {
        const auto& d = parameterTable[static_cast<std::size_t> (i)];
        if (! d.inPreset())
            continue;
        values.setProperty (Identifier (toJuce (d.id)),
                            toJuce (text::formatFloat (d.clampToDomain (snapshot.v[static_cast<std::size_t> (i)]))),
                            nullptr);
    }
    return values;
}

ValueTree toValueTree (const ProjectModel& model)
{
    ValueTree root (rootType);
    root.setProperty (schemaVersionProperty, currentSchemaVersion, nullptr);
    root.setProperty (pluginVersionProperty, toJuce (presetWriterStateVersion), nullptr);

    for (int i = 0; i < parameterCount; ++i)
    {
        const auto& d = parameterTable[static_cast<std::size_t> (i)];
        ValueTree p (paramType);
        p.setProperty (idProperty, toJuce (d.id), nullptr);
        p.setProperty (valueProperty, toJuce (text::formatFloat (d.clampToDomain (model.params.v[static_cast<std::size_t> (i)]))),
                       nullptr);
        root.appendChild (p, nullptr);
    }

    ValueTree preset (presetType);
    preset.setProperty (nameProperty, toJuce (model.presetName), nullptr);
    preset.setProperty (presetIdProperty, toJuce (model.presetId), nullptr);
    preset.setProperty (modifiedProperty, model.presetModified ? 1 : 0, nullptr);
    root.appendChild (preset, nullptr);

    ValueTree variations (variationsType);
    variations.setProperty (activeProperty, model.variations.activeIndex(), nullptr);
    for (int i = 0; i < variationSlotCount; ++i)
    {
        const auto& s = model.variations.slot (i);
        ValueTree slot (slotType);
        slot.setProperty (indexProperty, i, nullptr);
        slot.setProperty (nameProperty, toJuce (s.name), nullptr);
        slot.setProperty (emptyProperty, s.empty ? 1 : 0, nullptr);
        if (! s.empty)
            slot.appendChild (snapshotToValuesTree (s.snapshot), nullptr);
        variations.appendChild (slot, nullptr);
    }
    root.appendChild (variations, nullptr);

    if (model.ab.valid())
    {
        ValueTree ab (abType);
        ab.setProperty (engagedProperty, model.ab.engaged() ? 1 : 0, nullptr);
        ab.setProperty (activeProperty, model.ab.activeSide() == AbCompare::Side::A ? 0 : 1, nullptr);
        ab.setProperty (matchTrimProperty, model.ab.matchOutputTrim() ? 1 : 0, nullptr);
        for (int side = 0; side < 2; ++side)
        {
            ValueTree s (abSideType);
            s.setProperty (indexProperty, side, nullptr);
            s.appendChild (snapshotToValuesTree (model.ab.snapshot (side == 0 ? AbCompare::Side::A : AbCompare::Side::B)),
                           nullptr);
            ab.appendChild (s, nullptr);
        }
        root.appendChild (ab, nullptr);
    }

    root.appendChild (model.midiLearn.isValid() && model.midiLearn.hasType (midiLearnType) ? model.midiLearn.createCopy()
                                                                                          : ValueTree (midiLearnType),
                      nullptr);
    root.appendChild (model.ui.isValid() && model.ui.hasType (uiType) ? model.ui.createCopy() : ValueTree (uiType), nullptr);
    return root;
}

void encode (const ProjectModel& model, juce::MemoryBlock& destination)
{
    const auto tree = toValueTree (model);
    if (const auto xml = tree.createXml())
        juce::AudioProcessor::copyXmlToBinary (*xml, destination);
    else
        destination.reset();
}

ParamSnapshot snapshotFromApvts (juce::AudioProcessorValueTreeState& apvts)
{
    ParamSnapshot s;
    for (int i = 0; i < parameterCount; ++i)
    {
        const auto& d = parameterTable[static_cast<std::size_t> (i)];
        if (const auto* raw = apvts.getRawParameterValue (toJuce (d.id)))
            s.v[static_cast<std::size_t> (i)] = d.clampToDomain (raw->load());
    }
    return s;
}

void applySnapshot (juce::AudioProcessorValueTreeState& apvts, const ParamSnapshot& snapshot, bool soundOnly)
{
    for (int i = 0; i < parameterCount; ++i)
    {
        const auto& d = parameterTable[static_cast<std::size_t> (i)];
        if (soundOnly && ! d.inPreset())
            continue;
        if (auto* param = apvts.getParameter (toJuce (d.id)))
        {
            const float normalised = param->convertTo0to1 (d.clampToDomain (snapshot.v[static_cast<std::size_t> (i)]));
            if (std::abs (param->getValue() - normalised) > 1.0e-9f)
                param->setValueNotifyingHost (normalised);
        }
    }
}
} // namespace snaral::project
