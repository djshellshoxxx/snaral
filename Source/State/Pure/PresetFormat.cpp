#include "PresetFormat.h"

#include <algorithm>
#include <array>
#include <set>

#include "NumberText.h"

namespace snaral
{
namespace
{
// ---- migration / rename tables (empty for the first released schema) ------------------------------------
constexpr std::array<PresetMigration, 0> presetMigrations {};
constexpr std::array<RenamedParameter, 0> renamedIds {};
constexpr std::array<std::string_view, 0> retiredIds {};

bool hasControlChars (std::string_view s, bool allowNewlineAndTab) noexcept
{
    for (const char ch : s)
    {
        const auto c = static_cast<unsigned char> (ch);
        if (c == 0x7F)
            return true;
        if (c < 0x20 && ! (allowNewlineAndTab && (c == '\n' || c == '\t')))
            return true;
    }
    return false;
}

bool isBlank (std::string_view s) noexcept
{
    for (const char c : s)
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r')
            return false;
    return true;
}

std::string lowerAscii (std::string_view s)
{
    std::string out (s);
    for (auto& c : out)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char> (c - 'A' + 'a');
    return out;
}

void checkText (std::vector<Issue>& errors, const std::string& path, std::string_view value,
                std::size_t maxChars, bool allowNewlineAndTab, bool required)
{
    if (! json::isValidUtf8 (value))
    {
        errors.push_back ({ path, "text is not valid UTF-8" });
        return;
    }
    if (hasControlChars (value, allowNewlineAndTab))
        errors.push_back ({ path, "control characters are not allowed" });
    if (json::codePointCount (value) > maxChars)
        errors.push_back ({ path, "longer than " + std::to_string (maxChars) + " characters" });
    if (required && isBlank (value))
        errors.push_back ({ path, "must not be empty" });
}

bool validPresetIdChars (std::string_view id) noexcept
{
    if (id.empty() || id.size() > maxPresetIdChars)
        return false;
    for (const char c : id)
    {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
                        || c == '.' || c == '_' || c == '-';
        if (! ok)
            return false;
    }
    return true;
}

const std::vector<int>& lexicalParameterOrder()
{
    static const std::vector<int> order = []
    {
        std::vector<int> v;
        for (int i = 0; i < parameterCount; ++i)
            if (parameterTable[static_cast<std::size_t> (i)].inPreset())
                v.push_back (i);
        std::sort (v.begin(), v.end(), [] (int a, int b)
                   { return parameterTable[static_cast<std::size_t> (a)].id < parameterTable[static_cast<std::size_t> (b)].id; });
        return v;
    }();
    return order;
}

std::string rangeText (const ParamDef& d)
{
    return "[" + text::formatFloat (d.minimum) + ", " + text::formatFloat (d.maximum) + "]";
}

std::string choiceList (const ParamDef& d)
{
    std::string s;
    for (int i = 0; i < d.numChoices; ++i)
    {
        if (i > 0)
            s += ", ";
        s += d.choices[static_cast<std::size_t> (i)];
    }
    return s;
}

/// Validates one parameter value from JSON. Returns true and sets `out` on success.
bool readParameterValue (const ParamDef& d, const json::Value& v, const std::string& path,
                         std::vector<Issue>& errors, float& out)
{
    switch (d.type)
    {
        case ParamType::Float:
        {
            if (! v.isNumber())
            {
                errors.push_back ({ path, "expected a number" });
                return false;
            }
            float f = 0.0f;
            if (! text::parseFloat (v.text, f))
            {
                errors.push_back ({ path, "number is not finite or not representable (" + v.text + ")" });
                return false;
            }
            if (f < d.minimum || f > d.maximum)
            {
                errors.push_back ({ path, "value " + v.text + " is outside the allowed range " + rangeText (d) });
                return false;
            }
            if (d.isInteger() && f != std::round (f))
            {
                errors.push_back ({ path, "expected a whole number, got " + v.text });
                return false;
            }
            out = f;
            return true;
        }
        case ParamType::Bool:
            if (! v.isBool())
            {
                errors.push_back ({ path, "expected true or false" });
                return false;
            }
            out = v.boolean ? 1.0f : 0.0f;
            return true;
        case ParamType::Choice:
        {
            if (! v.isString())
            {
                errors.push_back ({ path, "expected one of the choice names: " + choiceList (d) });
                return false;
            }
            for (int i = 0; i < d.numChoices; ++i)
            {
                if (d.choices[static_cast<std::size_t> (i)] == v.text)
                {
                    out = static_cast<float> (i);
                    return true;
                }
            }
            errors.push_back ({ path, "unknown choice \"" + v.text + "\" (valid: " + choiceList (d) + ")" });
            return false;
        }
    }
    return false;
}

void warnUnknownKeys (const json::Value& obj, const std::string& pathPrefix,
                      std::initializer_list<std::string_view> known, std::vector<Issue>& warnings)
{
    for (const auto& m : obj.members)
    {
        if (std::find (known.begin(), known.end(), std::string_view (m.key)) == known.end())
            warnings.push_back ({ pathPrefix + "." + m.key, "unknown field ignored" });
    }
}
} // namespace

std::span<const RenamedParameter> renamedParameterIds() noexcept { return renamedIds; }
std::span<const std::string_view> retiredParameterIds() noexcept { return retiredIds; }

std::string parameterValueToJson (const ParamDef& d, float value)
{
    const float v = d.clampToDomain (value);
    switch (d.type)
    {
        case ParamType::Float: return text::formatFloat (v);
        case ParamType::Bool:  return v >= 0.5f ? "true" : "false";
        case ParamType::Choice:
        {
            int idx = static_cast<int> (v);
            idx = std::clamp (idx, 0, std::max (0, d.numChoices - 1));
            return json::quote (d.choices[static_cast<std::size_t> (idx)]);
        }
    }
    return "0";
}

bool validatePresetMetadata (const PresetData& p, std::vector<Issue>& errors)
{
    const auto before = errors.size();
    checkText (errors, "preset.name", p.name, maxPresetNameChars, false, true);
    checkText (errors, "preset.author", p.author, maxPresetAuthorChars, false, false);
    checkText (errors, "preset.description", p.description, maxPresetDescriptionChars, true, false);
    if (! p.presetId.empty() && ! validPresetIdChars (p.presetId))
        errors.push_back ({ "preset.preset_id", "must be 1-" + std::to_string (maxPresetIdChars)
                                                    + " characters from [A-Za-z0-9._-]" });
    if (p.tags.size() > maxPresetTags)
        errors.push_back ({ "preset.tags", "more than " + std::to_string (maxPresetTags) + " tags" });
    std::set<std::string> seen;
    for (std::size_t i = 0; i < p.tags.size(); ++i)
    {
        const auto path = "preset.tags[" + std::to_string (i) + "]";
        checkText (errors, path, p.tags[i], maxPresetTagChars, false, true);
        if (! seen.insert (lowerAscii (p.tags[i])).second)
            errors.push_back ({ path, "duplicate tag \"" + p.tags[i] + "\"" });
    }
    return errors.size() == before;
}

bool writePreset (const PresetData& preset, std::string& out, std::vector<Issue>* errors)
{
    std::vector<Issue> localErrors;
    if (! validatePresetMetadata (preset, localErrors))
    {
        if (errors != nullptr)
            *errors = std::move (localErrors);
        return false;
    }

    std::string s;
    s.reserve (6000);
    s += "{\n";
    s += "  \"format\": " + json::quote (presetFormatId) + ",\n";
    s += "  \"parameters\": {\n";
    const auto& order = lexicalParameterOrder();
    for (std::size_t k = 0; k < order.size(); ++k)
    {
        const auto i = static_cast<std::size_t> (order[k]);
        const auto& d = parameterTable[i];
        s += "    " + json::quote (d.id) + ": " + parameterValueToJson (d, preset.params.v[i]);
        s += (k + 1 < order.size()) ? ",\n" : "\n";
    }
    s += "  },\n";
    s += "  \"plugin\": {\n";
    s += "    \"name\": " + json::quote (presetPluginName) + ",\n";
    s += "    \"state_version\": " + json::quote (presetWriterStateVersion) + "\n";
    s += "  },\n";
    s += "  \"preset\": {\n";
    s += "    \"author\": " + json::quote (preset.author) + ",\n";
    s += "    \"description\": " + json::quote (preset.description) + ",\n";
    s += "    \"name\": " + json::quote (preset.name) + ",\n";
    if (! preset.presetId.empty())
        s += "    \"preset_id\": " + json::quote (preset.presetId) + ",\n";
    s += "    \"tags\": [";
    for (std::size_t i = 0; i < preset.tags.size(); ++i)
    {
        if (i > 0)
            s += ", ";
        s += json::quote (preset.tags[i]);
    }
    s += "]\n";
    s += "  },\n";
    s += "  \"schema_version\": " + std::to_string (presetSchemaVersion) + "\n";
    s += "}\n";
    out = std::move (s);
    return true;
}

std::string writePresetOrEmpty (const PresetData& preset)
{
    std::string out;
    return writePreset (preset, out, nullptr) ? out : std::string();
}

namespace detail
{
bool runPresetMigrations (json::Value& root, int fromVersion, int toVersion,
                          std::span<const PresetMigration> steps, std::string& error)
{
    int version = fromVersion;
    while (version < toVersion)
    {
        const PresetMigration* step = nullptr;
        for (const auto& s : steps)
            if (s.fromVersion == version && s.apply != nullptr)
                step = &s;
        if (step == nullptr)
        {
            error = "no migration path from schema_version " + std::to_string (version);
            return false;
        }
        if (! step->apply (root, error))
        {
            if (error.empty())
                error = "migration from schema_version " + std::to_string (version) + " failed";
            return false;
        }
        ++version;
    }
    return true;
}
} // namespace detail

PresetParseResult parsePreset (std::string_view jsonText, const PresetParseOptions& options)
{
    PresetParseResult r;
    auto fail = [&r] (std::string path, std::string message)
    {
        r.errors.push_back ({ std::move (path), std::move (message) });
        r.ok = false;
        return r;
    };

    if (jsonText.size() > maxPresetFileBytes)
        return fail ("", "file is larger than the " + std::to_string (maxPresetFileBytes / 1024) + " KiB preset limit");
    if (jsonText.empty())
        return fail ("", "file is empty");

    json::Value root;
    json::ParseError perr;
    json::ParseOptions popts;
    popts.maxBytes = maxPresetFileBytes;
    popts.maxDepth = 8;
    if (! json::parse (jsonText, root, perr, popts))
        return fail ("", "not valid JSON: " + perr.describe());

    if (! root.isObject())
        return fail ("", "the document root must be a JSON object");

    // ---- header: format + schema version (checked before anything else is trusted) ----
    const auto* format = root.find ("format");
    if (format == nullptr || ! format->isString())
        return fail ("format", "missing; expected \"" + std::string (presetFormatId) + "\"");
    if (format->text != presetFormatId)
        return fail ("format", "unsupported format identifier \"" + format->text + "\"; expected \""
                                   + std::string (presetFormatId) + "\"");

    const auto* version = root.find ("schema_version");
    long long schema = 0;
    if (version == nullptr || ! version->isNumber() || ! text::parseInt (version->text, schema))
        return fail ("schema_version", "missing or not an integer");
    r.sourceSchemaVersion = static_cast<int> (std::clamp<long long> (schema, 0, 1000000));
    if (schema < 1)
        return fail ("schema_version", "must be 1 or higher");
    if (schema > presetSchemaVersion)
        return fail ("schema_version", "unsupported future schema_version " + version->text
                                           + " (this build reads up to " + std::to_string (presetSchemaVersion) + ")");

    if (schema < presetSchemaVersion)
    {
        std::string migrationError;
        if (! detail::runPresetMigrations (root, static_cast<int> (schema), presetSchemaVersion,
                                           presetMigrations, migrationError))
            return fail ("schema_version", migrationError);
        r.migrated = true;
    }

    // ---- top level (schema v1 rejects unknown top-level fields) ----
    for (const auto& m : root.members)
        if (m.key != "format" && m.key != "schema_version" && m.key != "plugin" && m.key != "preset"
            && m.key != "parameters")
            r.errors.push_back ({ m.key, "unknown top-level field (schema v1 does not allow extra fields)" });

    const auto* plugin = root.find ("plugin");
    const auto* preset = root.find ("preset");
    const auto* params = root.find ("parameters");
    if (plugin == nullptr || ! plugin->isObject())
        r.errors.push_back ({ "plugin", "missing or not an object" });
    if (preset == nullptr || ! preset->isObject())
        r.errors.push_back ({ "preset", "missing or not an object" });
    if (params == nullptr || ! params->isObject())
        r.errors.push_back ({ "parameters", "missing or not an object" });
    if (! r.errors.empty())
        return r;

    // ---- plugin ----
    {
        const auto* name = plugin->find ("name");
        if (name == nullptr || ! name->isString())
            r.errors.push_back ({ "plugin.name", "missing or not a string" });
        else if (name->text != presetPluginName)
            r.warnings.push_back ({ "plugin.name", "written by \"" + name->text + "\", not Snaral" });
        const auto* sv = plugin->find ("state_version");
        if (sv == nullptr || ! sv->isString())
            r.errors.push_back ({ "plugin.state_version", "missing or not a string" });
        else
        {
            checkText (r.errors, "plugin.state_version", sv->text, maxStateVersionChars, false, false);
            r.pluginVersion = sv->text;
        }
        warnUnknownKeys (*plugin, "plugin", { "name", "state_version" }, r.warnings);
    }

    // ---- preset metadata ----
    PresetData data;
    {
        auto readString = [&] (const char* key, bool required, std::string& dest)
        {
            const auto* v = preset->find (key);
            if (v == nullptr)
            {
                if (required)
                    r.errors.push_back ({ std::string ("preset.") + key, "missing" });
                return;
            }
            if (! v->isString())
            {
                r.errors.push_back ({ std::string ("preset.") + key, "expected a string" });
                return;
            }
            dest = v->text;
        };
        readString ("name", true, data.name);
        readString ("author", false, data.author);
        readString ("description", false, data.description);
        readString ("preset_id", false, data.presetId);

        if (const auto* tags = preset->find ("tags"))
        {
            if (! tags->isArray())
                r.errors.push_back ({ "preset.tags", "expected an array of strings" });
            else
                for (std::size_t i = 0; i < tags->items.size(); ++i)
                {
                    if (! tags->items[i].isString())
                        r.errors.push_back ({ "preset.tags[" + std::to_string (i) + "]", "expected a string" });
                    else
                        data.tags.push_back (tags->items[i].text);
                }
        }
        warnUnknownKeys (*preset, "preset", { "name", "author", "description", "tags", "preset_id" }, r.warnings);
        if (r.errors.empty())
            validatePresetMetadata (data, r.errors);
    }

    // ---- parameters ----
    std::array<bool, static_cast<std::size_t> (parameterCount)> seen {};
    int knownSeen = 0;
    for (const auto& m : params->members)
    {
        const auto path = "parameters." + m.key;
        int index = findParameterIndex (m.key);
        if (index < 0)
        {
            bool handled = false;
            for (const auto& rn : renamedParameterIds())
            {
                if (rn.from == m.key)
                {
                    index = findParameterIndex (rn.to);
                    r.warnings.push_back ({ path, "renamed parameter, mapped to \"" + std::string (rn.to) + "\"" });
                    handled = index >= 0;
                    break;
                }
            }
            if (! handled)
            {
                const bool retired = std::find (retiredParameterIds().begin(), retiredParameterIds().end(),
                                                std::string_view (m.key)) != retiredParameterIds().end();
                r.warnings.push_back ({ path, retired ? "retired parameter ignored" : "unknown parameter ignored" });
                r.ignoredParameters.push_back (m.key);
                continue;
            }
        }

        const auto idx = static_cast<std::size_t> (index);
        const auto& def = parameterTable[idx];
        if (! def.inPreset())
        {
            r.warnings.push_back ({ path, "utility parameter is not stored in presets; ignored" });
            r.ignoredParameters.push_back (m.key);
            continue;
        }
        if (seen[idx])
        {
            r.errors.push_back ({ path, "parameter given twice (also present under a renamed id)" });
            continue;
        }
        float value = def.defaultValue;
        if (readParameterValue (def, m.value, path, r.errors, value))
        {
            data.params.v[idx] = value;
            seen[idx] = true;
            ++knownSeen;
        }
        else
        {
            seen[idx] = true; // already reported; do not also report as missing
        }
    }

    int missing = 0;
    for (int i = 0; i < parameterCount; ++i)
    {
        const auto idx = static_cast<std::size_t> (i);
        const auto& def = parameterTable[idx];
        if (! def.inPreset() || seen[idx])
            continue;
        ++missing;
        r.defaultedParameters.emplace_back (def.id);
        if (options.requireComplete)
            r.errors.push_back ({ "parameters." + std::string (def.id), "missing (complete preset required)" });
    }
    if (! options.requireComplete && missing > 0)
        r.warnings.push_back ({ "parameters", std::to_string (missing)
                                                  + " parameter(s) absent; registry defaults used" });
    if (knownSeen == 0 && r.errors.empty())
        r.errors.push_back ({ "parameters", "contains no known preset parameters" });

    if (r.errors.empty())
    {
        r.ok = true;
        r.data = std::move (data);
    }
    return r;
}
} // namespace snaral
