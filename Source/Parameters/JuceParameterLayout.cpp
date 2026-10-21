#include "JuceParameterLayout.h"

namespace snaral
{
static juce::String toJuceString (std::string_view s)
{
    return juce::String::fromUTF8 (s.data(), static_cast<int> (s.size()));
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (const auto& d : parameterTable)
    {
        const juce::ParameterID pid { toJuceString (d.id), 1 };
        const auto name = toJuceString (d.name);
        const bool automatable = d.automatable();
        switch (d.type)
        {
            case ParamType::Float:
            {
                juce::NormalisableRange<float> range { d.minimum, d.maximum };
                if (d.isInteger())
                    range.interval = 1.0f;
                if (d.scale == ParamScale::Log && d.minimum > 0.0f)
                {
                    const float lo = d.minimum, hi = d.maximum;
                    range = { lo, hi,
                              [lo, hi] (float, float, float p) { return lo * std::exp (juce::jlimit (0.0f, 1.0f, p) * std::log (hi / lo)); },
                              [lo, hi] (float, float, float v) { return std::log (juce::jlimit (lo, hi, v) / lo) / std::log (hi / lo); },
                              [lo, hi] (float, float, float v) { return juce::jlimit (lo, hi, v); } };
                }
                layout.add (std::make_unique<juce::AudioParameterFloat> (
                    pid, name, range, d.defaultValue,
                    juce::AudioParameterFloatAttributes().withLabel (toJuceString (d.unit))
                        .withAutomatable (automatable)));
                break;
            }
            case ParamType::Choice:
            {
                juce::StringArray items;
                for (int i = 0; i < d.numChoices; ++i)
                    items.add (toJuceString (d.choices[static_cast<std::size_t> (i)]));
                layout.add (std::make_unique<juce::AudioParameterChoice> (
                    pid, name, items, static_cast<int> (d.defaultValue),
                    juce::AudioParameterChoiceAttributes().withAutomatable (automatable)));
                break;
            }
            case ParamType::Bool:
                layout.add (std::make_unique<juce::AudioParameterBool> (
                    pid, name, d.defaultValue >= 0.5f,
                    juce::AudioParameterBoolAttributes().withAutomatable (automatable)));
                break;
        }
    }
    return layout;
}

void readSnapshot (const std::array<std::atomic<float>*, static_cast<std::size_t> (parameterCount)>& raw,
                   ParamSnapshot& out) noexcept
{
    for (std::size_t i = 0; i < raw.size(); ++i)
    {
        const auto& d = parameterTable[i];
        const float value = raw[i] != nullptr ? raw[i]->load (std::memory_order_relaxed) : d.defaultValue;
        out.v[i] = d.clampToDomain (value);
    }
}
} // namespace snaral
