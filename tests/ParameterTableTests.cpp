#ifdef NDEBUG
#undef NDEBUG
#endif
#include "Parameters/ParameterTable.h"
#include "Parameters/ParameterRegistry.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <set>
#include <string>

int main()
{
    using namespace snaral;
    std::set<std::string> ids;
    for (int i = 0; i < parameterCount; ++i)
    {
        const auto& d = parameterTable[static_cast<std::size_t> (i)];
        assert (! d.id.empty() && ! d.name.empty() && ! d.group.empty());
        for (const char c : d.id)
            assert ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_');
        assert (ids.insert (std::string (d.id)).second);          // unique
        assert (d.minimum < d.maximum);
        assert (d.defaultValue >= d.minimum && d.defaultValue <= d.maximum);
        if (d.type == ParamType::Choice)
            assert (d.numChoices >= 2 && d.maximum == static_cast<float> (d.numChoices - 1));
        if (d.scale == ParamScale::Log)
            assert (d.minimum > 0.0f);
        for (const float n : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
        {
            const float dom = d.fromNormalized (n);
            assert (dom >= d.minimum - 1e-4f && dom <= d.maximum + 1e-4f);
            const float back = d.toNormalized (dom);
            if (! d.isInteger())
                assert (std::abs (back - n) < 1e-5f);
        }
        assert (std::abs (d.fromNormalized (d.toNormalized (d.defaultValue)) - d.defaultValue)
                < 1e-3f * (std::abs (d.defaultValue) + 1.0f));
        assert (findParameterIndex (d.id) == i);
    }
    assert (findParameterIndex ("nope") == -1);

    // macros in the new table agree with the legacy macro registry
    for (std::size_t m = 0; m < macroParameterSpecs.size(); ++m)
    {
        const auto& legacy = macroParameterSpecs[m];
        const auto& d = parameterTable[m];
        assert (d.id == legacy.id && d.minimum == legacy.minimum && d.maximum == legacy.maximum
                && d.defaultValue == legacy.defaultValue);
    }

    ParamSnapshot s;
    assert (s[P::weight] == 75.0f && s.flag (P::limiter_enabled) && s.choice (P::lfo_sync_division) == 6);
    assert (s[P::master_tune_hz] == 440.0f && s[P::limiter_ceiling_db] == -1.0f);
    assert (parameterTable[P::bypass].inPreset() == false);
    assert (parameterTable[P::traj_x2].automatable() == false);

    std::cout << "ParameterTableTests passed (" << parameterCount << " parameters)\n";
    return 0;
}
