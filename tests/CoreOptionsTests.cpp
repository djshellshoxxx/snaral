#include "TestSupport.h"

#include <iostream>

#include "Core/CoreOptions.h"

using namespace snaral;
using namespace snaral::testing;

namespace
{
void testDefaultsAndRoundTrip()
{
    const CoreOptions d;
    CHECK (d.themeId == "cdl" && d.tooltips && d.animations && d.scale == 1.0f && ! d.onboardingSeen && d.validate().empty());
    CoreOptions o;
    o.themeId = "high-contrast"; o.tooltips = false; o.animations = false; o.scale = 1.25f; o.onboardingSeen = true;
    const auto t = toText (o);
    const auto r = optionsFromText (t);
    CHECK (r.headerOk && r.problems.empty() && r.options == o);
    CHECK (toText (r.options) == t);
    CHECK (toText (CoreOptions {}) == "snaral-options 1\ntheme=cdl\ntooltips=1\nanimations=1\nscale=1\nonboarding_seen=0\n");
}

void testValidation()
{
    CoreOptions o;
    o.themeId = "Bad Theme!"; CHECK (! o.validate().empty() && o.sanitised().themeId == "cdl");
    o = {}; o.scale = 5.0f; CHECK (! o.validate().empty() && o.sanitised().scale == 1.0f);
    o = {}; o.scale = std::nanf (""); CHECK (! o.validate().empty() && o.sanitised().scale == 1.0f);
    o = {}; o.themeId = std::string (33, 'a'); CHECK (! o.validate().empty());
    o = {}; o.themeId = ""; CHECK (! o.validate().empty());
    CHECK (CoreOptions::isValidScale (0.75f) && CoreOptions::isValidScale (2.0f) && ! CoreOptions::isValidScale (0.74f) && ! CoreOptions::isValidScale (2.01f));
    CHECK (toText (o).find ("theme=cdl") != std::string::npos);   // invalid values are never written

    // per-field fallback; other values still load
    const auto r = optionsFromText ("snaral-options 1\ntheme=Nope!\ntooltips=0\nscale=9\nanimations=maybe\nonboarding_seen=1\nfuture_key=7\n");
    CHECK (r.headerOk && r.problems.size() == 3);
    CHECK (r.options.themeId == "cdl" && ! r.options.tooltips && r.options.scale == 1.0f && r.options.animations && r.options.onboardingSeen);
    // malformed input never throws and yields defaults
    for (const char* bad : { "", "garbage", "snaral-options 2\ntheme=x\n", "theme=x\n" })
    {
        const auto b = optionsFromText (bad);
        CHECK (b.options == CoreOptions {} && ! b.problems.empty());
    }
    CHECK (optionsFromText (std::string (20000, 'x')).options == CoreOptions {});
    Rng rng (3);
    for (int i = 0; i < 2000; ++i)
    {
        std::string g = "snaral-options 1\n";
        for (int k = 0; k < 60; ++k) g.push_back (static_cast<char> (rng.below (128)));
        const auto b = optionsFromText (g);
        CHECK (b.options.validate().empty());
    }
}
} // namespace

int main()
{
    testDefaultsAndRoundTrip();
    testValidation();
    std::cout << "CoreOptionsTests OK\n";
    return 0;
}
