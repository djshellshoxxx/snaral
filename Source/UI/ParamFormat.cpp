#include "ParamFormat.h"

#include <cmath>

namespace snaral::ui
{
const ParamDef* findParamDef (const juce::String& id)
{
    const auto idx = findParameterIndex (std::string_view (id.toRawUTF8()));
    return idx < 0 ? nullptr : &parameterTable[static_cast<std::size_t> (idx)];
}

juce::String prettyChoiceName (std::string_view raw)
{
    juce::String in = toJuce (raw);
    if (in.isEmpty() || in.containsChar ('/') || in.containsAnyOf ("0123456789"))
        return in;                                   // "1/4", "x2", "1/8t": keep verbatim
    if (in == "ms")
        return in;

    juce::StringArray words;
    words.addTokens (in, "_", "");
    for (auto& w : words)
        w = w.substring (0, 1).toUpperCase() + w.substring (1);
    return words.joinIntoString (" ");
}

juce::String midiNoteName (int note)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const int n = juce::jlimit (0, 127, note);
    return juce::String (names[n % 12]) + juce::String ((n / 12) - 1);
}

namespace
{
juce::String fixed (double v, int decimals)
{
    // juce::String (double, 0) means "as many digits as needed", so whole numbers are formatted from an integer.
    auto s = decimals <= 0 ? juce::String (static_cast<juce::int64> (std::llround (v))) : juce::String (v, decimals);
    if (s.startsWith ("-") && s.containsOnly ("-0."))   // never show "-0.0"
        s = s.substring (1);
    return s;
}

int decimalsFor (const ParamDef& d, double v)
{
    const double span = static_cast<double> (d.maximum - d.minimum);
    if (d.isInteger())
        return 0;
    const double a = std::abs (v);
    if (span <= 2.0 || a < 1.0) return 2;
    if (span <= 40.0)  return 1;
    if (a < 10.0)      return 2;
    if (a < 100.0)     return 1;
    return 0;
}
} // namespace

juce::String formatParamValue (const ParamDef& d, float value, bool withUnit)
{
    value = d.clampToDomain (value);
    switch (d.type)
    {
        case ParamType::Bool:
            return value >= 0.5f ? "On" : "Off";
        case ParamType::Choice:
        {
            const int i = juce::jlimit (0, juce::jmax (0, d.numChoices - 1), static_cast<int> (value + 0.5f));
            return prettyChoiceName (d.choices[static_cast<std::size_t> (i)]);
        }
        case ParamType::Float:
            break;
    }

    const auto unit = toJuce (d.unit);
    const double v = static_cast<double> (value);
    const bool signedDisplay = d.minimum < 0.0f;
    auto withSign = [&] (const juce::String& s) { return (signedDisplay && v > 0.0 && ! s.startsWith ("-")) ? "+" + s : s; };

    if (unit == "Hz")
    {
        if (v >= 1000.0)
            return fixed (v / 1000.0, 2) + (withUnit ? " kHz" : "k");
        return fixed (v, decimalsFor (d, v)) + (withUnit ? " Hz" : "");
    }
    if (unit == "ms")
    {
        if (v >= 1000.0)
            return fixed (v / 1000.0, 2) + (withUnit ? " s" : "s");
        return fixed (v, v < 10.0 ? 1 : 0) + (withUnit ? " ms" : "");
    }
    if (unit == "%")
        return fixed (v, d.isInteger() || (d.maximum - d.minimum) >= 50.0f ? 0 : 1) + (withUnit ? "%" : "");
    if (unit == "dB")
        return withSign (fixed (v, 1)) + (withUnit ? " dB" : "");
    if (unit == "st" || unit == "ct")
        return withSign (fixed (v, 0)) + (withUnit ? " " + unit : "");
    if (unit == "deg")
        return fixed (v, 0) + (withUnit ? juce::String (juce::CharPointer_UTF8 ("\xc2\xb0")) : "");
    if (unit == "note")
        return midiNoteName (static_cast<int> (std::lround (v)));
    return withSign (fixed (v, decimalsFor (d, v))) + (withUnit && unit.isNotEmpty() ? " " + unit : "");
}

juce::String formatPlainNumber (double v, const juce::String& unit)
{
    const int dec = std::abs (v) >= 100.0 ? 0 : (std::abs (v) >= 10.0 ? 1 : 2);
    return fixed (v, dec) + (unit.isEmpty() ? "" : (unit == "%" ? unit : " " + unit));
}

juce::String describeParamRange (const ParamDef& d)
{
    switch (d.type)
    {
        case ParamType::Bool:
            return "On or Off";
        case ParamType::Choice:
        {
            juce::StringArray names;
            for (int i = 0; i < d.numChoices; ++i)
                names.add (prettyChoiceName (d.choices[static_cast<std::size_t> (i)]));
            return names.joinIntoString (", ");
        }
        case ParamType::Float:
            break;
    }
    const auto unit = toJuce (d.unit);
    if (unit == "note")
        return midiNoteName (static_cast<int> (d.minimum)) + " to " + midiNoteName (static_cast<int> (d.maximum));
    return formatParamValue (d, d.minimum) + " to " + formatParamValue (d, d.maximum);
}

namespace
{
bool parseNote (const juce::String& s, int& note)
{
    auto t = s.trim().toLowerCase();
    if (t.length() < 2)
        return false;
    static const int semis[] = { 9, 11, 0, 2, 4, 5, 7 };   // a b c d e f g
    const auto letter = t[0];
    if (letter < 'a' || letter > 'g')
        return false;
    int semi = semis[letter - 'a'];
    int i = 1;
    if (t[i] == '#')      { ++semi; ++i; }
    else if (t[i] == 'b' && t.length() > 2) { --semi; ++i; }
    const auto oct = t.substring (i);
    if (oct.isEmpty() || ! oct.containsOnly ("-0123456789") || oct == "-")
        return false;
    note = (oct.getIntValue() + 1) * 12 + semi;
    return true;
}
} // namespace

bool parseParamValue (const ParamDef& d, const juce::String& textIn, float& out, juce::String* error)
{
    auto fail = [&] (const juce::String& msg)
    {
        if (error != nullptr)
            *error = msg;
        return false;
    };

    const auto text = textIn.trim();
    if (text.isEmpty())
        return fail ("Enter a value");

    if (d.type == ParamType::Bool)
    {
        const auto l = text.toLowerCase();
        if (l == "on" || l == "true" || l == "1" || l == "yes")  { out = 1.0f; return true; }
        if (l == "off" || l == "false" || l == "0" || l == "no") { out = 0.0f; return true; }
        return fail ("Use On or Off");
    }

    if (d.type == ParamType::Choice)
    {
        for (int i = 0; i < d.numChoices; ++i)
        {
            const auto raw = toJuce (d.choices[static_cast<std::size_t> (i)]);
            if (text.equalsIgnoreCase (raw) || text.equalsIgnoreCase (prettyChoiceName (d.choices[static_cast<std::size_t> (i)])))
            {
                out = static_cast<float> (i);
                return true;
            }
        }
        return fail ("Choose one of: " + describeParamRange (d));
    }

    const auto unit = toJuce (d.unit);
    if (unit == "note")
    {
        int note = 0;
        if (parseNote (text, note))
        {
            if (static_cast<float> (note) < d.minimum || static_cast<float> (note) > d.maximum)
                return fail ("Out of range (" + describeParamRange (d) + ")");
            out = static_cast<float> (note);
            return true;
        }
    }

    // numeric prefix
    int i = 0;
    const int n = text.length();
    if (i < n && (text[i] == '-' || text[i] == '+'))
        ++i;
    const int digitsStart = i;
    bool dot = false;
    while (i < n && (juce::CharacterFunctions::isDigit (text[i]) || (text[i] == '.' && ! dot)))
    {
        dot = dot || text[i] == '.';
        ++i;
    }
    if (i == digitsStart || (i == digitsStart + 1 && dot))
        return fail ("Not a number");

    double v = text.substring (0, i).getDoubleValue();
    const auto suffix = text.substring (i).trim().toLowerCase();

    if (suffix.isNotEmpty())
    {
        const auto ul = unit.toLowerCase();
        bool ok = false;
        if (suffix == ul)                                   ok = true;
        else if (ul == "hz" && (suffix == "k" || suffix == "khz")) { v *= 1000.0; ok = true; }
        else if (ul == "ms" && suffix == "s")               { v *= 1000.0; ok = true; }
        else if (ul == "deg" && (suffix == "deg" || suffix == juce::String (juce::CharPointer_UTF8 ("\xc2\xb0")))) ok = true;
        else if (ul == "st" && (suffix == "semitones" || suffix == "semitone")) ok = true;
        else if (ul == "ct" && (suffix == "cents" || suffix == "cent")) ok = true;
        if (! ok)
            return fail (unit.isEmpty() ? "No unit expected" : "Unit should be " + unit);
    }

    if (! std::isfinite (v) || v < static_cast<double> (d.minimum) - 1e-6 || v > static_cast<double> (d.maximum) + 1e-6)
        return fail ("Out of range (" + describeParamRange (d) + ")");

    out = d.clampToDomain (static_cast<float> (v));
    return true;
}
} // namespace snaral::ui
