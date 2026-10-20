#include "../Source/Parameters/MacroMappings.h"
#include "../Source/Parameters/ParameterRegistry.h"

#include <array>
#include <cassert>
#include <cmath>
#include <iostream>

int main()
{
    constexpr std::array<std::string_view, 5> expectedIds {
        "weight", "growl", "bite", "motion", "space"
    };
    constexpr std::array<float, 5> expectedDefaults { 75.0f, 65.0f, 20.0f, 0.0f, 0.0f };

    assert (snaral::macroParameterSpecs.size() == expectedIds.size());
    for (std::size_t i = 0; i < expectedIds.size(); ++i)
    {
        const auto& spec = snaral::macroParameterSpecs[i];
        assert (spec.id == expectedIds[i]);
        assert (spec.minimum < spec.maximum);
        assert (spec.defaultValue >= spec.minimum && spec.defaultValue <= spec.maximum);
        assert (std::abs (spec.defaultNormalized() - expectedDefaults[i] / 100.0f) < 1.0e-6f);
        assert (snaral::findMacroParameter (spec.id) == &spec);

        for (const auto normalized : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
        {
            const auto domain = spec.normalizedToDomain (normalized);
            assert (std::abs (spec.domainToNormalized (domain) - normalized) < 1.0e-6f);
        }
    }
    assert (snaral::findMacroParameter ("not_a_parameter") == nullptr);

    for (const auto x : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
    {
        const auto expectedSquare = x * x;
        assert (std::abs (snaral::weightSubMultiplier (x) - expectedSquare) < 1.0e-6f);
        assert (std::abs (snaral::growlMidMultiplier (x) - expectedSquare) < 1.0e-6f);
        assert (std::abs (snaral::growlSourceBBias (x) - 0.20f * x) < 1.0e-6f);
        assert (std::abs (snaral::biteFilterPeakOffsetSemitones (x) - 18.0f * x) < 1.0e-6f);
        assert (std::abs (snaral::biteDriveOffsetDb (x) - 12.0f * x) < 1.0e-6f);
        assert (std::abs (snaral::motionDepthMultiplier (x) - x) < 1.0e-6f);
        assert (std::abs (snaral::spaceWidthPercent (x) - 120.0f * x) < 1.0e-6f);
    }

    assert (snaral::weightSubMultiplier (-2.0f) == 0.0f);
    assert (snaral::spaceWidthPercent (2.0f) == 120.0f);
    assert (snaral::biteDriveOffsetDb (NAN) == 0.0f);
    assert (snaral::growlSourceBlend (0.9f, 1.0f) == 1.0f);
    std::cout << "ParameterContractTests passed\n";
}
