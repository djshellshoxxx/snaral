#include "Core/CoreOptions.h"

#include <cmath>

#include "State/Pure/NumberText.h"

namespace snaral
{
bool CoreOptions::isValidThemeId (std::string_view id) noexcept
{
    if (id.empty() || id.size() > 32)
        return false;
    for (char c : id)
        if (! ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-'))
            return false;
    return true;
}

bool CoreOptions::isValidScale (float s) noexcept { return std::isfinite (s) && s >= minScale && s <= maxScale; }

std::vector<std::string> CoreOptions::validate() const
{
    std::vector<std::string> p;
    if (! isValidThemeId (themeId)) p.push_back ("theme id is invalid");
    if (! isValidScale (scale)) p.push_back ("scale must be 0.75 to 2.0");
    return p;
}

CoreOptions CoreOptions::sanitised() const
{
    CoreOptions o = *this;
    const CoreOptions d;
    if (! isValidThemeId (o.themeId)) o.themeId = d.themeId;
    if (! isValidScale (o.scale)) o.scale = d.scale;
    return o;
}

std::string toText (const CoreOptions& in)
{
    const auto o = in.sanitised();
    return "snaral-options 1\ntheme=" + o.themeId + "\ntooltips=" + (o.tooltips ? "1" : "0") + "\nanimations=" + (o.animations ? "1" : "0")
           + "\nscale=" + text::formatFloat (o.scale) + "\nonboarding_seen=" + (o.onboardingSeen ? "1" : "0") + "\n";
}

OptionsParseResult optionsFromText (std::string_view t)
{
    OptionsParseResult r;
    if (t.size() > 16 * 1024)
    {
        r.problems.push_back ("options text too large; defaults used");
        return r;
    }
    auto boolOf = [&r] (std::string_view key, std::string_view v, bool& out) {
        if (v == "0" || v == "1") out = v == "1";
        else r.problems.push_back (std::string (key) + " must be 0 or 1");
    };
    int lineNo = 0;
    std::size_t pos = 0;
    while (pos < t.size())
    {
        auto nl = t.find ('\n', pos);
        auto line = t.substr (pos, nl == std::string_view::npos ? std::string_view::npos : nl - pos);
        pos = nl == std::string_view::npos ? t.size() : nl + 1;
        ++lineNo;
        while (! line.empty() && (line.back() == '\r' || line.back() == ' ')) line.remove_suffix (1);
        if (line.empty() || line[0] == '#') continue;
        if (lineNo == 1 || ! r.headerOk)
        {
            if (line == "snaral-options 1") { r.headerOk = true; continue; }
            r.problems.push_back ("missing or unsupported header");
            return r; // defaults
        }
        const auto eq = line.find ('=');
        if (eq == std::string_view::npos) { r.problems.push_back ("line " + std::to_string (lineNo) + ": expected key=value"); continue; }
        const auto key = line.substr (0, eq);
        const auto val = line.substr (eq + 1);
        if (key == "theme")
        {
            if (CoreOptions::isValidThemeId (val)) r.options.themeId = std::string (val);
            else r.problems.push_back ("theme id is invalid");
        }
        else if (key == "tooltips") boolOf (key, val, r.options.tooltips);
        else if (key == "animations") boolOf (key, val, r.options.animations);
        else if (key == "onboarding_seen") boolOf (key, val, r.options.onboardingSeen);
        else if (key == "scale")
        {
            float s = 1.0f;
            if (text::parseFloat (val, s) && CoreOptions::isValidScale (s)) r.options.scale = s;
            else r.problems.push_back ("scale must be 0.75 to 2.0");
        }
        // unknown keys: ignored
    }
    if (! r.headerOk)
        r.problems.push_back ("empty options text; defaults used");
    return r;
}
} // namespace snaral
