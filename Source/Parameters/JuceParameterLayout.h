#pragma once
#include <atomic>

#include <juce_audio_processors/juce_audio_processors.h>
#include "ParameterTable.h"

namespace snaral
{
/// Builds the full APVTS layout (version hint 1) from parameterTable.
juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

/// Reads every parameter's current raw value (domain units) into a snapshot.
/// Real-time safe: only atomic loads. `raw` must hold one entry per parameter.
void readSnapshot (const std::array<std::atomic<float>*, static_cast<std::size_t> (parameterCount)>& raw,
                   ParamSnapshot& out) noexcept;
} // namespace snaral
