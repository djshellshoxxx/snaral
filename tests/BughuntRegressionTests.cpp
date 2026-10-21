// Regression tests for defects found by the bug-hunt pass (docs/release/BUGHUNT.md). Each test names its bug id.
#include "TestSupport.h"

#include <cmath>
#include <vector>

#include "Dsp/Fx/FormantFurnace.h"
#include "Dsp/SvfFilter.h"

using namespace snaral;

namespace
{
// BUG-002: SvfFilter::prepare() on a sample-rate change kept stale coefficients when the cutoff value was unchanged.
void testSvfReprepareRefreshesCoefficients()
{
    std::vector<float> in (2048);
    for (std::size_t i = 0; i < in.size(); ++i)
        in[i] = static_cast<float> (std::sin (0.07 * static_cast<double> (i)) + 0.3 * std::sin (1.9 * static_cast<double> (i)));

    SvfFilter fresh;
    fresh.prepare (96000.0);
    fresh.setCutoff (1200.0f);

    SvfFilter reused;
    reused.prepare (44100.0);
    reused.setCutoff (1200.0f);
    for (float x : in) (void) reused.process (x); // coefficients computed for 44.1 kHz
    reused.prepare (96000.0);                     // host changed the sample rate; same default cutoff
    reused.setCutoff (1200.0f);

    for (float x : in)
        CHECK (fresh.process (x) == reused.process (x));
}

// BUG-003: macro-mapping.md "Motion ... also multiplies furnace_env_amount and furnace_lfo_amount".
void testFurnaceAmountsScaledByMotion()
{
    ParamSnapshot s;
    s[P::furnace_env_amount] = 80.0f;
    s[P::furnace_lfo_amount] = 60.0f;

    s[P::motion] = 0.0f;
    auto p = FormantFurnaceParams::from (s);
    CHECK (p.envAmount == 0.0f && p.lfoAmount == 0.0f);

    s[P::motion] = 100.0f;
    p = FormantFurnaceParams::from (s);
    CHECK (std::fabs (p.envAmount - 0.8f) < 1e-6f && std::fabs (p.lfoAmount - 0.6f) < 1e-6f);

    s[P::motion] = 50.0f;
    p = FormantFurnaceParams::from (s);
    CHECK (std::fabs (p.envAmount - 0.4f) < 1e-6f && std::fabs (p.lfoAmount - 0.3f) < 1e-6f);

    s[P::motion] = std::nanf ("");
    p = FormantFurnaceParams::from (s);
    CHECK (p.envAmount == 0.0f && p.lfoAmount == 0.0f);
}
} // namespace

int main()
{
    testSvfReprepareRefreshesCoefficients();
    testFurnaceAmountsScaledByMotion();
    std::puts ("BughuntRegressionTests passed");
    return 0;
}
