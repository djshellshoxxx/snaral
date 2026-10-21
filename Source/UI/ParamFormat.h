#pragma once

#include "UiCommon.h"
#include "Parameters/ParameterTable.h"

namespace snaral::ui
{
/// Table lookup by parameter id; nullptr when the id is unknown.
const ParamDef* findParamDef (const juce::String& id);

/// "saw_stack" -> "Saw Stack", "1/4" -> "1/4", "ms" -> "ms", "x2" -> "x2".
juce::String prettyChoiceName (std::string_view raw);

/// MIDI note number to scientific pitch name (C4 = 60), e.g. 36 -> "C2".
juce::String midiNoteName (int midiNote);

/// Human readable value in domain units: "1.20 kHz", "600 ms", "-3.0 dB", "75%", "Saw Stack", "On".
juce::String formatParamValue (const ParamDef&, float domainValue, bool withUnit = true);

/// Parses text a user typed or pasted. Accepts a trailing unit ("1.2k", "1200 Hz", "0.6 s", "-3 dB", "50%"), choice names
/// and bool words. Values outside the parameter range are REJECTED (not clamped), so a bad paste leaves the last valid
/// value untouched. On failure `error` (when given) receives a short message including the valid range.
bool parseParamValue (const ParamDef&, const juce::String& text, float& domainOut, juce::String* error = nullptr);

/// "20 Hz to 20 kHz", "Mono, Poly" ... for help text and error messages.
juce::String describeParamRange (const ParamDef&);

/// Domain value -> text used for default knob readouts when no ParamDef exists (generic knobs).
juce::String formatPlainNumber (double value, const juce::String& unit);
} // namespace snaral::ui
