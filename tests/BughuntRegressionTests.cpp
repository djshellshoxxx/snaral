// Regression tests for defects found by the bug-hunt pass (docs/release/BUGHUNT.md). Each test names its bug id.
#include "TestSupport.h"

#include <cmath>
#include <vector>

#include "Dsp/Fx/FormantFurnace.h"
#include "Dsp/Oscillators.h"
#include "Dsp/SvfFilter.h"
#include "State/Pure/PresetPack.h"

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
// BUG-004: pack ids / file names that Windows cannot create (device names, trailing dot) were accepted.
void testPackNamesRejectWindowsDeviceNames()
{
    CHECK (validPackId ("my.pack-1"));
    CHECK (! validPackId ("con"));
    CHECK (! validPackId ("nul"));
    CHECK (! validPackId ("com1.extra"));
    CHECK (! validPackId ("lpt9"));
    CHECK (! validPackId ("trailing."));
    CHECK (validPackId ("console"));
    CHECK (validPackFileName ("Fat Bass.snaralpreset"));
    CHECK (! validPackFileName ("CON.snaralpreset"));
    CHECK (! validPackFileName ("aux.snaralpreset"));
    CHECK (! validPackFileName ("COM3.snaralpreset"));
    CHECK (validPackFileName ("COM10.snaralpreset"));
}
// BUG-005: clampf let NaN through; a NaN pulse width reached a float->int cast and indexed outside the correlation
// table (found by DspRobustnessTests under ASan).
void testClampfAndOscillatorNaN()
{
    CHECK (clampf (std::nanf (""), 0.2f, 0.8f) == 0.2f);
    CHECK (clampf (0.5f, 0.2f, 0.8f) == 0.5f && clampf (-1.0f, 0.2f, 0.8f) == 0.2f && clampf (9.0f, 0.2f, 0.8f) == 0.8f);
    ParamSnapshot s;
    s[P::mid_pulse_width] = std::nanf ("");
    s[P::mid_table_position] = std::nanf ("");
    s[P::mid_blend] = std::nanf ("");
    s[P::mid_source_a] = 1.0f;
    s[P::mid_source_b] = 3.0f;
    MidOscillator o;
    o.prepare (48000.0);
    o.setParams (MidOscParams::from (s));
    for (int i = 0; i < 4096; ++i)
        CHECK (std::isfinite (o.process (110.0f, std::nanf (""))));
}
} // namespace

int main()
{
    testSvfReprepareRefreshesCoefficients();
    testFurnaceAmountsScaledByMotion();
    testPackNamesRejectWindowsDeviceNames();
    testClampfAndOscillatorNaN();
    std::puts ("BughuntRegressionTests passed");
    return 0;
}
