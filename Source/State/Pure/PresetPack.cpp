#include "PresetPack.h"

#include <set>

#include "Json.h"
#include "NumberText.h"
#include "PresetFormat.h"

namespace snaral
{
bool validPackId (std::string_view id) noexcept
{
    if (id.empty() || id.size() > 64)
        return false;
    for (const char c : id)
    {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
        if (! ok)
            return false;
    }
    return id.front() != '.';
}

bool validPackFileName (std::string_view name) noexcept
{
    if (name.size() <= presetFileExtension.size() || name.size() > 128)
        return false;
    if (name.substr (name.size() - presetFileExtension.size()) != presetFileExtension)
        return false;
    if (name.front() == '.')
        return false;
    for (const char ch : name)
    {
        const auto c = static_cast<unsigned char> (ch);
        if (c < 0x20 || c == 0x7F || std::string_view ("<>:\"/\\|?*").find (ch) != std::string_view::npos)
            return false;
    }
    return json::isValidUtf8 (name);
}

namespace
{
void checkLen (std::vector<Issue>& errors, const std::string& path, std::string_view v, std::size_t maxChars,
               bool required)
{
    if (! json::isValidUtf8 (v))
        errors.push_back ({ path, "text is not valid UTF-8" });
    for (const char ch : v)
        if (static_cast<unsigned char> (ch) < 0x20 || ch == 0x7F)
        {
            errors.push_back ({ path, "control characters are not allowed" });
            break;
        }
    if (json::codePointCount (v) > maxChars)
        errors.push_back ({ path, "longer than " + std::to_string (maxChars) + " characters" });
    if (required && v.empty())
        errors.push_back ({ path, "must not be empty" });
}

bool validateManifest (const PackManifest& m, std::vector<Issue>& errors)
{
    const auto before = errors.size();
    if (! validPackId (m.packId))
        errors.push_back ({ "pack.id", "must be 1-64 characters from [a-z0-9._-] and not start with '.'" });
    checkLen (errors, "pack.name", m.name, 128, true);
    checkLen (errors, "pack.version", m.version, 32, true);
    checkLen (errors, "pack.author", m.author, 128, false);
    checkLen (errors, "pack.license", m.license, 256, true);
    checkLen (errors, "pack.min_plugin_version", m.minPluginVersion, 32, false);
    if (m.files.empty())
        errors.push_back ({ "presets", "a pack must list at least one preset" });
    if (m.files.size() > maxPackPresets)
        errors.push_back ({ "presets", "more than " + std::to_string (maxPackPresets) + " presets" });
    if (m.presetCount != static_cast<int> (m.files.size()))
        errors.push_back ({ "pack.preset_count", "declares " + std::to_string (m.presetCount) + " but lists "
                                                     + std::to_string (m.files.size()) + " presets" });
    std::set<std::string> seen;
    for (std::size_t i = 0; i < m.files.size(); ++i)
    {
        const auto path = "presets[" + std::to_string (i) + "]";
        if (! validPackFileName (m.files[i]))
            errors.push_back ({ path, "invalid preset file name \"" + m.files[i] + "\"" });
        std::string lower = m.files[i];
        for (auto& c : lower)
            if (c >= 'A' && c <= 'Z')
                c = static_cast<char> (c - 'A' + 'a');
        if (! seen.insert (lower).second)
            errors.push_back ({ path, "duplicate file name \"" + m.files[i] + "\"" });
    }
    return errors.size() == before;
}
} // namespace

PackManifestResult parsePackManifest (std::string_view jsonText)
{
    PackManifestResult r;
    if (jsonText.size() > maxPackManifestBytes)
    {
        r.errors.push_back ({ "", "manifest is larger than the limit" });
        return r;
    }
    json::Value root;
    json::ParseError perr;
    json::ParseOptions po;
    po.maxBytes = maxPackManifestBytes;
    po.maxDepth = 6;
    if (! json::parse (jsonText, root, perr, po))
    {
        r.errors.push_back ({ "", "not valid JSON: " + perr.describe() });
        return r;
    }
    if (! root.isObject())
    {
        r.errors.push_back ({ "", "the manifest root must be a JSON object" });
        return r;
    }
    const auto* format = root.find ("format");
    if (format == nullptr || ! format->isString() || format->text != packFormatId)
    {
        r.errors.push_back ({ "format", "expected \"" + std::string (packFormatId) + "\"" });
        return r;
    }
    long long schema = 0;
    const auto* sv = root.find ("schema_version");
    if (sv == nullptr || ! sv->isNumber() || ! text::parseInt (sv->text, schema) || schema < 1)
    {
        r.errors.push_back ({ "schema_version", "missing or not a positive integer" });
        return r;
    }
    if (schema > packSchemaVersion)
    {
        r.errors.push_back ({ "schema_version", "unsupported future pack schema_version " + sv->text });
        return r;
    }
    for (const auto& m : root.members)
        if (m.key != "format" && m.key != "schema_version" && m.key != "pack" && m.key != "presets")
            r.errors.push_back ({ m.key, "unknown top-level field" });

    const auto* pack = root.find ("pack");
    const auto* presets = root.find ("presets");
    if (pack == nullptr || ! pack->isObject())
        r.errors.push_back ({ "pack", "missing or not an object" });
    if (presets == nullptr || ! presets->isArray())
        r.errors.push_back ({ "presets", "missing or not an array" });
    if (! r.errors.empty())
        return r;

    PackManifest m;
    auto str = [&] (const char* key, bool required, std::string& dest)
    {
        const auto* v = pack->find (key);
        if (v == nullptr)
        {
            if (required)
                r.errors.push_back ({ std::string ("pack.") + key, "missing" });
            return;
        }
        if (! v->isString())
            r.errors.push_back ({ std::string ("pack.") + key, "expected a string" });
        else
            dest = v->text;
    };
    str ("id", true, m.packId);
    str ("name", true, m.name);
    str ("version", true, m.version);
    str ("author", false, m.author);
    str ("license", true, m.license);
    str ("min_plugin_version", false, m.minPluginVersion);
    long long count = -1;
    const auto* pc = pack->find ("preset_count");
    if (pc == nullptr || ! pc->isNumber() || ! text::parseInt (pc->text, count) || count < 0 || count > 100000)
        r.errors.push_back ({ "pack.preset_count", "missing or not a non-negative integer" });
    else
        m.presetCount = static_cast<int> (count);
    for (const auto& member : pack->members)
        if (member.key != "id" && member.key != "name" && member.key != "version" && member.key != "author"
            && member.key != "license" && member.key != "min_plugin_version" && member.key != "preset_count")
            r.warnings.push_back ({ "pack." + member.key, "unknown field ignored" });

    for (std::size_t i = 0; i < presets->items.size(); ++i)
    {
        if (! presets->items[i].isString())
            r.errors.push_back ({ "presets[" + std::to_string (i) + "]", "expected a file name string" });
        else
            m.files.push_back (presets->items[i].text);
    }
    if (r.errors.empty())
        validateManifest (m, r.errors);
    if (r.errors.empty())
    {
        r.ok = true;
        r.manifest = std::move (m);
    }
    return r;
}

bool writePackManifest (const PackManifest& m, std::string& out, std::vector<Issue>* errors)
{
    std::vector<Issue> local;
    if (! validateManifest (m, local))
    {
        if (errors != nullptr)
            *errors = std::move (local);
        return false;
    }
    std::string s;
    s += "{\n";
    s += "  \"format\": " + json::quote (packFormatId) + ",\n";
    s += "  \"pack\": {\n";
    s += "    \"author\": " + json::quote (m.author) + ",\n";
    s += "    \"id\": " + json::quote (m.packId) + ",\n";
    s += "    \"license\": " + json::quote (m.license) + ",\n";
    s += "    \"min_plugin_version\": " + json::quote (m.minPluginVersion) + ",\n";
    s += "    \"name\": " + json::quote (m.name) + ",\n";
    s += "    \"preset_count\": " + std::to_string (m.presetCount) + ",\n";
    s += "    \"version\": " + json::quote (m.version) + "\n";
    s += "  },\n";
    s += "  \"presets\": [";
    for (std::size_t i = 0; i < m.files.size(); ++i)
    {
        s += i == 0 ? "\n    " : ",\n    ";
        s += json::quote (m.files[i]);
    }
    s += "\n  ],\n";
    s += "  \"schema_version\": " + std::to_string (packSchemaVersion) + "\n";
    s += "}\n";
    out = std::move (s);
    return true;
}
} // namespace snaral
