#pragma once

#include "UiCommon.h"
#include "Parameters/ParameterTable.h"

namespace snaral::ui
{
/// One page of the local help. Plain text; the HelpPanel renders title + body.
struct HelpTopic
{
    juce::String id;          ///< stable id ("macro-weight", "tab-filter-motion", "midi-learn", ...)
    juce::String category;    ///< "Getting started", "Macros", "Advanced", "Reference", "Support"
    juce::String title;
    juce::String body;
    juce::String keywords;    ///< extra search terms (parameter ids and names are added automatically)
};

/// Static help content (versioned with the application, no network needed). The macro and Advanced pages are generated
/// from snaral::parameterTable (names, units, ranges, defaults) plus a one-line purpose per parameter, so the help can
/// never disagree with the controls about a range or unit. undocumentedParameterIds() lets a test fail when a new
/// parameter is added without a purpose line.
namespace HelpContent
{
inline constexpr const char* cdlWebsite = "https://djshellshoxxx.github.io/circuitdriftlabs/";

const std::vector<HelpTopic>& topics();
const HelpTopic* findTopic (const juce::String& id);

/// Topic ids that describe a parameter (its Advanced tab page, or the macro page) for context help.
juce::String topicIdForParameter (const juce::String& parameterId);

/// Case-insensitive AND search over title, keywords and body. Returns indices into topics(), best matches first.
std::vector<int> search (const juce::String& query);

/// One-line purpose, or an empty string when the parameter has no documentation.
juce::String parameterPurpose (const juce::String& parameterId);
std::vector<juce::String> undocumentedParameterIds();

/// Single formatted reference line: "Cutoff (Hz): 20 Hz to 20 kHz, default 1.20 kHz. Purpose..."
juce::String describeParameter (const ParamDef&);
} // namespace HelpContent
} // namespace snaral::ui
