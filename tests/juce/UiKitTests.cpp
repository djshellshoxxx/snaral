#include "UiKitUnity.h"

#include "Parameters/JuceParameterLayout.h"

#include <iostream>
#include <optional>
#include <set>

using namespace snaral;
using namespace snaral::ui;

namespace
{
int failures = 0;
int checks = 0;

void report (bool ok, const char* expr, const char* file, int line, const juce::String& extra = {})
{
    ++checks;
    if (! ok)
    {
        ++failures;
        std::cerr << "FAIL " << file << ":" << line << "  " << expr << (extra.isEmpty() ? "" : "  [" + extra + "]") << std::endl;
    }
}
#define CHECK(x) report (static_cast<bool> (x), #x, __FILE__, __LINE__)
#define CHECK_MSG(x, m) report (static_cast<bool> (x), #x, __FILE__, __LINE__, m)

struct DummyProcessor : juce::AudioProcessor
{
    DummyProcessor() : juce::AudioProcessor (BusesProperties().withOutput ("Out", juce::AudioChannelSet::stereo(), true)) {}
    const juce::String getName() const override { return "dummy"; }
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    double getTailLengthSeconds() const override { return 0; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}
};

struct Fixture
{
    DummyProcessor processor;
    juce::AudioProcessorValueTreeState apvts { processor, nullptr, "STATE", createParameterLayout() };
    float get (const char* id) { return apvts.getParameter (id)->convertFrom0to1 (apvts.getParameter (id)->getValue()); }
    void set (const char* id, float domain)
    {
        auto* p = apvts.getParameter (id);
        p->setValueNotifyingHost (p->convertTo0to1 (domain));
    }
};

// ------------------------------------------------------------------------------------------------------------------
void testThemes()
{
    CHECK (ThemeCatalog::all().size() >= 5);
    const auto& cdl = ThemeCatalog::cdlDefault();
    CHECK (cdl.id == "cdl");
    CHECK (cdl.background == juce::Colour::fromString ("FF0E1116"));
    CHECK (cdl.panel == juce::Colour::fromString ("FF171B22"));
    CHECK (cdl.border == juce::Colour::fromString ("FF2A303A"));
    CHECK (cdl.accent == juce::Colour::fromString ("FFE8532A"));
    CHECK (cdl.secondary == juce::Colour::fromString ("FF4FB6C4"));
    CHECK (cdl.textPrimary == juce::Colour::fromString ("FFE6E8EC"));
    CHECK (cdl.textMuted == juce::Colour::fromString ("FF8A929E"));
    CHECK (cdl.success == juce::Colour::fromString ("FF7BC96F"));
    CHECK (cdl.warning == juce::Colour::fromString ("FFF2C14E"));
    CHECK (ThemeCatalog::byId ("no-such-theme").id == "cdl");

    // known contrast values
    CHECK (std::abs (contrastRatio (juce::Colours::black, juce::Colours::white) - 21.0) < 0.01);
    CHECK (std::abs (contrastRatio (juce::Colours::white, juce::Colours::white) - 1.0) < 0.001);

    for (const auto& theme : ThemeCatalog::all())
    {
        for (const auto& req : contrastRequirements())
        {
            const auto ratio = contrastRatio (req.foreground (theme), req.background (theme));
            CHECK_MSG (ratio >= req.minimumRatio,
                       theme.id + " / " + req.description + " ratio " + juce::String (ratio, 2) + " < " + juce::String (req.minimumRatio));
        }
        CHECK (theme.focusRing != theme.background);
    }
    CHECK (contrastRequirements().size() >= 20);
}

void testOptions()
{
    OptionsModel m;
    CHECK (m.getThemeId() == "cdl");
    CHECK (m.areTooltipsEnabled());
    CHECK (m.areAnimationsEnabled());
    CHECK (m.getUiScale() == 1.0f);
    CHECK (m.shouldOfferTour());

    m.setUiScale (5.0f);
    CHECK (m.getUiScale() == metrics::maxUiScale);
    m.setUiScale (0.1f);
    CHECK (m.getUiScale() == metrics::minUiScale);
    m.setUiScale (1.23f);
    CHECK (std::abs (m.getUiScale() - 1.25f) < 1.0e-4f);

    m.setThemeId ("midnight");
    m.setTooltipsEnabled (false);
    m.setAnimationsEnabled (false);
    m.setFirstRunComplete (true);
    const auto tree = m.toValueTree();

    OptionsModel m2;
    CHECK (m2.setState (tree));
    CHECK (m2.getThemeId() == "midnight");
    CHECK (! m2.areTooltipsEnabled());
    CHECK (! m2.areAnimationsEnabled());
    CHECK (! m2.shouldOfferTour());

    // hostile state
    juce::ValueTree bad ("SnaralUi");
    bad.setProperty ("theme", "does-not-exist", nullptr);
    bad.setProperty ("uiScale", 99.0, nullptr);
    bad.setProperty ("tooltips", "banana", nullptr);
    OptionsModel m3;
    CHECK (m3.setState (bad));
    CHECK (m3.getUiScale() <= metrics::maxUiScale);
    CHECK (ThemeCatalog::byId (m3.getThemeId()).id.isNotEmpty());
    CHECK (! m3.setState (juce::ValueTree ("Other")));

    // listener
    struct L : OptionsModel::Listener
    {
        void optionChanged (const juce::Identifier&) override { ++n; }
        int n = 0;
    } l;
    m3.addListener (&l);
    m3.setThemeId ("tealdrift");
    CHECK (l.n >= 1);
    m3.removeListener (&l);

    // settings file with an injected path
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("snaral-ui-test-" + juce::String (juce::Random::getSystemRandom().nextInt64()));
    auto file = dir.getChildFile ("sub/ui-settings.xml");
    CHECK (SettingsFile::save (m, file).wasOk());
    CHECK (file.existsAsFile());
    OptionsModel loaded;
    CHECK (SettingsFile::load (loaded, file).wasOk());
    CHECK (loaded.getThemeId() == "midnight");
    CHECK (! loaded.areAnimationsEnabled());

    OptionsModel untouched;
    CHECK (SettingsFile::load (untouched, dir.getChildFile ("missing.xml")).wasOk());
    CHECK (untouched.getThemeId() == "cdl");

    file.replaceWithText ("<<<< not xml");
    OptionsModel afterBad;
    afterBad.setThemeId ("daylight");
    CHECK (SettingsFile::load (afterBad, file).failed());
    CHECK (afterBad.getThemeId() == "daylight");
    dir.deleteRecursively();

    CHECK (SettingsFile::defaultFile().getFullPathName().containsIgnoreCase ("Snaral"));
}

void testFormatting()
{
    for (const auto& def : parameterTable)
    {
        const float values[] = { def.minimum, def.defaultValue, def.maximum, def.fromNormalized (0.37f) };
        for (float v : values)
        {
            const auto text = formatParamValue (def, v, true);
            CHECK_MSG (text.isNotEmpty(), juce::String (std::string (def.id)));
            float back = 0.0f;
            const bool ok = parseParamValue (def, text, back, nullptr);
            CHECK_MSG (ok, juce::String (std::string (def.id)) + " '" + text + "'");
            if (ok && def.type == ParamType::Float && def.maximum > def.minimum)
            {
                const float tol = (def.maximum - def.minimum) * 0.02f;   // displayed precision is limited
                CHECK_MSG (std::abs (back - v) <= tol, juce::String (std::string (def.id)) + " '" + text + "' -> " + juce::String (back));
            }
            else if (ok && def.type != ParamType::Float)
                CHECK_MSG (back == v, juce::String (std::string (def.id)) + " '" + text + "'");
        }
    }

    const auto* weight = findParamDef ("weight");
    CHECK (weight != nullptr);
    float out = 0;
    juce::String err;
    CHECK (parseParamValue (*weight, "50", out, &err) && out == 50.0f);
    CHECK (parseParamValue (*weight, "50%", out, &err) && out == 50.0f);
    CHECK (! parseParamValue (*weight, "150", out, &err));
    CHECK (err.isNotEmpty());
    CHECK (! parseParamValue (*weight, "abc", out, &err));
    CHECK (! parseParamValue (*weight, "", out, &err));
    CHECK (! parseParamValue (*weight, "-5", out, &err));
    CHECK (findParamDef ("nonexistent") == nullptr);
    {
        const auto* mode = findParamDef ("drive_type");
        CHECK (parseParamValue (*mode, "1", out, &err) && out == 1.0f);   // plain index from "Copy value"
        CHECK (! parseParamValue (*mode, "7", out, &err));
        CHECK (plainDomainValueText (*findParamDef ("filter_cutoff_hz"), 1234.5f) == "1234.5");
        CHECK (plainDomainValueText (*mode, 1.0f) == "1");
    }

    // BUG-006: integer parameters must reject fractional input instead of rounding it silently
    const auto* bend = findParamDef ("pitch_bend_range");
    CHECK (bend != nullptr);
    CHECK (parseParamValue (*bend, "5", out, &err) && out == 5.0f);
    CHECK (! parseParamValue (*bend, "2.5", out, &err));
    CHECK (err.isNotEmpty());
    const auto* transpose = findParamDef ("transpose_semitones");
    CHECK (parseParamValue (*transpose, "-7 st", out, &err) && out == -7.0f);
    CHECK (! parseParamValue (*transpose, "0.4", out, &err));

    CHECK (midiNoteName (60) == "C4" || midiNoteName (60) == "C3");
    CHECK (prettyChoiceName ("source_position") != "source_position");
}

void testContextActions()
{
    ContextHooks hooks;
    ContextMenuSpec spec;
    spec.hooks = &hooks;
    auto has = [&] (ContextAction a) { auto v = availableContextActions (spec); return std::find (v.begin(), v.end(), a) != v.end(); };
    CHECK (has (ContextAction::Reset));
    CHECK (has (ContextAction::CopyValue));
    CHECK (has (ContextAction::PasteValue));
    CHECK (! has (ContextAction::MidiLearn));
    CHECK (! has (ContextAction::ClearMidiMapping));
    CHECK (! has (ContextAction::RandomizeThis));
    hooks.onMidiLearn = [] {};
    hooks.onClearMidiMapping = [] {};
    hooks.onRandomizeThis = [] {};
    CHECK (has (ContextAction::MidiLearn));
    CHECK (has (ContextAction::ClearMidiMapping));
    CHECK (has (ContextAction::RandomizeThis));
    hooks.isLearned = [] { return false; };
    CHECK (! has (ContextAction::ClearMidiMapping));
    hooks.isLearned = [] { return true; };
    CHECK (has (ContextAction::ClearMidiMapping));
    CHECK (buildContextMenu (spec).getNumItems() >= 5);

    juce::String stored;
    Clipboard::setProvider ([&] { return stored; }, [&] (const juce::String& s) { stored = s; });
    Clipboard::write ("abc");
    CHECK (Clipboard::read() == "abc");
    Clipboard::resetProvider();
}

void testMacroKnob()
{
    Fixture f;
    MacroKnob knob ("Weight");
    knob.bind (f.apvts, "weight");
    knob.setBounds (0, 0, knob.getPreferredSize().x, knob.getPreferredSize().y);
    CHECK (knob.getPreferredSize().x >= metrics::minHitTarget && knob.getPreferredSize().y >= metrics::minHitTarget);
    CHECK (knob.getWantsKeyboardFocus());
    CHECK (knob.getTitle().containsIgnoreCase ("weight"));
    CHECK (knob.getReadoutText().contains ("75"));

    f.set ("weight", 30.0f);
    CHECK (std::abs (knob.getValue() - 30.0) < 0.5);
    knob.performContextAction (ContextAction::Reset);
    CHECK (std::abs (f.get ("weight") - 75.0f) < 0.5f);

    CHECK (knob.commitValueEntry ("40"));
    CHECK (std::abs (f.get ("weight") - 40.0f) < 0.5f);
    CHECK (! knob.commitValueEntry ("400"));
    CHECK (std::abs (f.get ("weight") - 40.0f) < 0.5f);
    CHECK (! knob.commitValueEntry ("zzz"));

    int learn = 0, clear = 0;
    knob.hooks.onMidiLearn = [&] { ++learn; };
    knob.hooks.onClearMidiMapping = [&] { ++clear; };
    knob.performContextAction (ContextAction::MidiLearn);
    knob.performContextAction (ContextAction::ClearMidiMapping);
    CHECK (learn == 1 && clear == 1);
    knob.setLearnState (MacroKnob::LearnState::Armed);
    CHECK (knob.getLearnState() == MacroKnob::LearnState::Armed);

    // copy / paste
    juce::String stored;
    Clipboard::setProvider ([&] { return stored; }, [&] (const juce::String& s) { stored = s; });
    f.set ("weight", 62.0f);
    knob.performContextAction (ContextAction::CopyValue);
    f.set ("weight", 10.0f);
    knob.performContextAction (ContextAction::PasteValue);
    CHECK (std::abs (f.get ("weight") - 62.0f) < 1.0f);
    // BUG-007: copy is the plain domain number, not the rounded display text
    f.set ("weight", 62.375f);
    knob.performContextAction (ContextAction::CopyValue);
    CHECK (stored == "62.375");
    f.set ("weight", 10.0f);
    knob.performContextAction (ContextAction::PasteValue);
    CHECK (std::abs (f.get ("weight") - 62.375f) < 1.0e-4f);
    stored = "garbage";
    knob.performContextAction (ContextAction::PasteValue);
    CHECK (std::abs (f.get ("weight") - 62.0f) < 1.0f);
    Clipboard::resetProvider();

    // keyboard
    const double before = knob.getValue();
    knob.keyPressed (juce::KeyPress (juce::KeyPress::upKey));
    CHECK (knob.getValue() > before);
    knob.keyPressed (juce::KeyPress (juce::KeyPress::homeKey));
    CHECK (knob.getValue() <= knob.getMinimum() + 1.0e-6);
    knob.keyPressed (juce::KeyPress (juce::KeyPress::endKey));
    CHECK (knob.getValue() >= knob.getMaximum() - 1.0e-6);
}

void testParamControls()
{
    Fixture f;
    ParamSlider slider (f.apvts, "bite");
    ParamCombo combo (f.apvts, "traj_dest");
    ParamToggle toggle (f.apvts, "traj_loop");
    slider.setSize (300, ParamSlider::preferredHeight);
    CHECK (slider.getHeight() >= metrics::minHitTarget);
    CHECK (ParamCombo::preferredHeight >= metrics::buttonHeight);
    CHECK (slider.getSlider().getWantsKeyboardFocus());
    CHECK (combo.getComboBox().getNumItems() == 3);
    combo.getComboBox().setSelectedItemIndex (2, juce::sendNotificationSync);
    CHECK (std::abs (f.get ("traj_dest") - 2.0f) < 0.01f);
    toggle.setToggleState (true, juce::sendNotificationSync);
    CHECK (f.get ("traj_loop") > 0.5f);
    slider.performContextAction (ContextAction::Reset);
    CHECK (std::abs (f.get ("bite") - 20.0f) < 0.5f);
}

void testAdvanced()
{
    Fixture f;
    std::set<juce::String> covered;
    for (int t = 0; t < numAdvancedTabs; ++t)
    {
        AdvancedTabContent content (static_cast<AdvancedTab> (t), f.apvts);
        for (auto& id : content.getParameterIds())
        {
            CHECK_MSG (covered.insert (id).second, "duplicate control " + id);
            CHECK (content.findControl (id) != nullptr);
        }
        CHECK (content.getNumSections() > 0);
        CHECK (content.getPreferredHeight (700) > 0);
    }
    for (const auto& def : parameterTable)
    {
        const juce::String id (std::string (def.id).c_str());
        const bool expectedElsewhere = isEditedByDedicatedWidget (id) || isMainViewParameter (id);
        CHECK_MSG (expectedElsewhere || covered.count (id) == 1, "parameter not reachable: " + id);
    }

    TabbedAdvanced tabs;
    tabs.buildDefaultTabs (f.apvts);
    tabs.setSize (700, 380);
    for (int t = 0; t < numAdvancedTabs; ++t)
    {
        tabs.setCurrentTab (static_cast<AdvancedTab> (t));
        CHECK (tabs.getCurrentTab() == static_cast<AdvancedTab> (t));
        CHECK (tabs.getTabButton (static_cast<AdvancedTab> (t)).getHeight() >= metrics::minHitTarget);
    }
    CHECK (juce::String (advancedTabName (AdvancedTab::FilterMotion)) == "Filter & Motion");
}

void testTrajectory()
{
    std::array<double, 5> x { 0.0, 0.2, 0.5, 0.8, 1.0 };
    CHECK (TrajectoryGeometry::clampX (x, 1, -1.0) >= 0.0 + TrajectoryGeometry::minGap - 1.0e-9);
    CHECK (TrajectoryGeometry::clampX (x, 1, 0.9) <= x[2] - TrajectoryGeometry::minGap + 1.0e-9);
    CHECK (TrajectoryGeometry::clampX (x, 3, 0.1) >= x[2] + TrajectoryGeometry::minGap - 1.0e-9);
    CHECK (TrajectoryGeometry::clampX (x, 3, 2.0) <= 1.0 - TrajectoryGeometry::minGap + 1.0e-9);
    CHECK (TrajectoryGeometry::clampY (-3.0) == 0.0 && TrajectoryGeometry::clampY (3.0) == 1.0);
    CHECK (std::abs (TrajectoryGeometry::snap (0.52, 0.05) - 0.50) < 1.0e-9);
    CHECK (TrajectoryGeometry::isValid (x, { 0.2, 0.9, 0.5, 0.3, 0.2 }));
    CHECK (! TrajectoryGeometry::isValid ({ 0.0, 0.5, 0.4, 0.8, 1.0 }, { 0.2, 0.9, 0.5, 0.3, 0.2 }));

    // random stress: points never cross
    juce::Random rng (1234);
    for (int i = 0; i < 2000; ++i)
    {
        const int idx = 1 + rng.nextInt (3);
        x[static_cast<std::size_t> (idx)] = TrajectoryGeometry::clampX (x, idx, rng.nextDouble() * 3.0 - 1.0);
        CHECK (TrajectoryGeometry::isValid (x, { 0, 0, 0, 0, 0 }));
    }

    Fixture f;
    struct Counter : juce::AudioProcessorParameter::Listener
    {
        void parameterValueChanged (int, float) override { ++changes; }
        void parameterGestureChanged (int, bool starting) override { starting ? ++begins : ++ends; }
        int changes = 0, begins = 0, ends = 0;
    } counter;
    f.apvts.getParameter ("traj_y2")->addListener (&counter);
    f.apvts.getParameter ("traj_x2")->addListener (&counter);

    auto binding = std::make_unique<ApvtsTrajectoryBinding> (f.apvts);
    auto* raw = binding.get();
    TrajectoryEditor ed (std::move (binding));
    ed.setSize (600, 300);
    CHECK (std::abs (ed.getX()[1] - 0.15) < 1.0e-3);
    CHECK (ed.getX()[0] == 0.0 && ed.getX()[4] == 1.0);

    CHECK (ed.movePointTo (1, 0.3, 0.6));
    CHECK (std::abs (f.get ("traj_x2") - 0.3f) < 1.0e-3f);
    CHECK (std::abs (f.get ("traj_y2") - 0.6f) < 1.0e-3f);
    CHECK (counter.begins == counter.ends);
    CHECK (counter.begins >= 1);

    ed.movePointTo (1, 0.95, 0.5);                       // cannot cross point 3 (x3 = 0.40)
    CHECK (f.get ("traj_x2") < f.get ("traj_x3"));

    ed.selectPoint (2);
    ed.keyPressed (juce::KeyPress (juce::KeyPress::upKey));
    CHECK (f.get ("traj_y3") > 0.5f);
    ed.keyPressed (juce::KeyPress (juce::KeyPress::deleteKey));
    ed.resetAll();
    CHECK (std::abs (f.get ("traj_x2") - 0.15f) < 1.0e-3f);
    CHECK (std::abs (f.get ("traj_y3") - 0.5f) < 1.0e-3f);
    CHECK (ed.getReadoutText().isNotEmpty());
    (void) raw;

    f.apvts.getParameter ("traj_y2")->removeListener (&counter);
    f.apvts.getParameter ("traj_x2")->removeListener (&counter);

    // default curve is finite and bounded
    std::array<float, 5> cx { 0, 0.15f, 0.4f, 0.7f, 1 }, cy { 0.2f, 0.9f, 0.5f, 0.3f, 0.2f };
    for (int i = 0; i <= 20; ++i)
    {
        const float v = defaultTrajectoryCurve (cx, cy, 0.5f, static_cast<float> (i) / 20.0f);
        CHECK (std::isfinite (v) && v >= -0.001f && v <= 1.001f);
    }
    // BUG-008: the preview curve is the DSP curve (passes through every control point) and the UI gap equals the DSP gap
    for (int k = 0; k < 5; ++k)
        CHECK (std::abs (defaultTrajectoryCurve (cx, cy, 0.5f, cx[static_cast<std::size_t> (k)]) - cy[static_cast<std::size_t> (k)]) < 1.0e-5f);
    CHECK (static_cast<float> (TrajectoryGeometry::minGap) == snaral::trajMinGap);
}

void testMeters()
{
    MeterBallistics b;
    b.update (0.0, 1.0f, 0.5f);
    CHECK (std::abs (b.holdDb - 0.0f) < 0.01f);
    for (int i = 0; i < 14; ++i)                              // 1.4 s: still held
        b.update (0.1, 0.0f, 0.0f);
    CHECK (b.holdDb > -0.5f);
    for (int i = 0; i < 10; ++i)                              // +1.0 s: decaying at ~20 dB/s after the hold
        b.update (0.1, 0.0f, 0.0f);
    CHECK (b.holdDb < -3.0f && b.holdDb > -25.0f);
    CHECK (b.peakDb < -30.0f);

    AtomicMeterSource src;
    CHECK (! src.readAndReset().valid);
    const float block[4] = { 0.5f, -0.5f, 0.5f, -0.5f };
    src.update (block, block, 4);
    auto r = src.readAndReset();
    CHECK (r.valid && std::abs (r.peak[0] - 0.5f) < 1.0e-6f && std::abs (r.rms[0] - 0.5f) < 1.0e-6f);
    CHECK (src.readAndReset().peak[0] == 0.0f);

    LevelMeter meter;
    meter.setSize (24, 160);
    CHECK (! meter.hasValidReading());
    CHECK (meter.getReadoutText().contains ("--") || meter.getReadoutText().containsIgnoreCase ("no"));
    meter.setSource (&src);
    const float loud[4] = { 1.0f, -1.0f, 1.0f, -1.0f };
    src.update (loud, loud, 4);
    meter.refreshNow();
    CHECK (meter.hasValidReading());
    CHECK (meter.isClipping());
    meter.clearClip();
    CHECK (! meter.isClipping());
    meter.setSource (nullptr);
    meter.refreshNow();
    CHECK (! meter.hasValidReading());
    CHECK (! meter.isClipping());

    AtomicCpuSource cpu;
    CpuGauge gauge;
    gauge.setSize (120, 30);
    gauge.setSource (&cpu);
    gauge.refreshNow();
    CHECK (! gauge.hasValidReading());                         // no data: never invents a number
    CHECK (gauge.getReadoutText().containsIgnoreCase ("unavailable"));
    cpu.reportBlock (0.001, 0.004);
    for (int i = 0; i < 100; ++i)
        gauge.refreshNow (0.1);
    CHECK (gauge.hasValidReading());
    CHECK (std::abs (gauge.getSmoothedLoad() - 0.25f) < 0.02f);
    CHECK (! gauge.isOverrunShown());
    cpu.reportBlock (0.006, 0.004);
    gauge.refreshNow (0.1);
    CHECK (gauge.isOverrunShown());
    CHECK (gauge.getTitle() == "DSP CPU" || gauge.getTitle().containsIgnoreCase ("DSP CPU"));
    CHECK (gauge.getReadoutText().isNotEmpty());
}

struct FakeScope : ScopeSource
{
    ScopeRingBuffer ring;
    int pulls = 0;
    FakeScope() { ring.prepare (8192, 2); }
    int pullLatest (ScopeTap, int ch, float* dest, int n) noexcept override { ++pulls; return ring.pullLatest (ch, dest, n); }
    double getSampleRate() const noexcept override { return 48000.0; }
};

void testScope()
{
    FakeScope src;
    std::vector<float> sine (2048);
    for (std::size_t i = 0; i < sine.size(); ++i)
        sine[i] = std::sin (static_cast<float> (i) * 0.05f);
    const float* ch[2] = { sine.data(), sine.data() };
    src.ring.push (ch, 2, 2048);
    std::vector<float> out (512, 0.0f);
    CHECK (src.ring.pullLatest (0, out.data(), 512) == 512);
    CHECK (std::abs (out[511] - sine[2047]) < 1.0e-6f);

    ScopeView scope;
    scope.setSize (300, 140);
    CHECK (scope.getRefreshRateHz() == 30 || scope.getRefreshRateHz() == 0);
    scope.setSource (&src);
    scope.refreshNow();
    CHECK (scope.hasData());
    CHECK (src.pulls >= 1);
    CHECK (! scope.isRefreshing());                            // not showing (no peer): the timer must not run

    scope.setDisplayEnabled (false);
    CHECK (! scope.isRefreshing());
}

void testHelp()
{
    CHECK (HelpContent::undocumentedParameterIds().empty());
    CHECK (HelpContent::topics().size() > 20);
    std::set<juce::String> ids;
    for (const auto& t : HelpContent::topics())
    {
        CHECK_MSG (ids.insert (t.id).second, "duplicate topic " + t.id);
        CHECK (t.title.isNotEmpty() && t.body.isNotEmpty());
    }
    CHECK (HelpContent::findTopic ("midi-learn") != nullptr);
    CHECK (HelpContent::topicIdForParameter ("weight").isNotEmpty());
    CHECK (HelpContent::topicIdForParameter ("filter_cutoff").isNotEmpty() || findParameterIndex ("filter_cutoff") < 0);
    CHECK (! HelpContent::search ("").empty());
    auto hits = HelpContent::search ("growl");
    CHECK (! hits.empty());
    CHECK (HelpContent::search ("qqzzxxnothing").empty());
    CHECK (! HelpContent::search ("MIDI learn").empty());
    CHECK (! HelpContent::search ("keyboard").empty());

    HelpPanel panel;
    panel.setSize (680, 440);
    std::vector<juce::String> opened;
    panel.callbacks.openUrl = [&] (const juce::String& u) { opened.push_back (u); };
    panel.setSearchText ("growl");
    CHECK (panel.getNumVisibleTopics() >= 1);
    panel.setSearchText ("qqzzxxnothing");
    CHECK (panel.getNumVisibleTopics() == 0);
    panel.setSearchText ({});
    CHECK (panel.showTopicForParameter ("weight"));
    CHECK (panel.getDisplayedText().containsIgnoreCase ("weight"));
}

void testOverlayAndPanels()
{
    juce::Component editor;
    editor.setSize (900, 560);
    OnboardingOverlay overlay;
    juce::Component weight, space;
    editor.addAndMakeVisible (weight);
    editor.addAndMakeVisible (space);
    editor.addChildComponent (overlay);
    overlay.setBounds (editor.getLocalBounds());
    weight.setBounds (40, 100, 60, 80);
    space.setBounds (700, 100, 60, 80);
    overlay.registerTarget ("macro.weight", &weight);
    overlay.registerTarget ("macro.space", &space);

    int shown = 0;
    std::optional<OnboardingOverlay::Outcome> outcome;
    overlay.onStepShown = [&] (int, const OnboardingStep&) { ++shown; };
    overlay.onFinished = [&] (OnboardingOverlay::Outcome o) { outcome = o; };
    CHECK (overlay.getNumSteps() >= 8);
    overlay.start();
    CHECK (overlay.isRunning() && shown == 1);
    CHECK (overlay.getHighlightBounds().isEmpty());
    overlay.next();
    CHECK (overlay.getStepIndex() == 1);
    CHECK (! overlay.getHighlightBounds().isEmpty());
    CHECK (! overlay.getCardBounds().intersects (overlay.getHighlightBounds()));
    CHECK (overlay.getLocalBounds().contains (overlay.getCardBounds()));
    overlay.back();
    CHECK (overlay.getStepIndex() == 0);
    overlay.back();
    CHECK (overlay.getStepIndex() == 0);
    overlay.skip();
    CHECK (! overlay.isRunning() && outcome == OnboardingOverlay::Outcome::Skipped);

    outcome.reset();
    overlay.start();                                          // re-runnable
    for (int i = 0; i < overlay.getNumSteps(); ++i)
        overlay.next();
    CHECK (! overlay.isRunning() && outcome == OnboardingOverlay::Outcome::Completed);
    overlay.start();
    overlay.keyPressed (juce::KeyPress (juce::KeyPress::escapeKey));
    CHECK (! overlay.isRunning());
    CHECK (HelpContent::topics().size() > 0);

    // options panel
    OptionsModel options;
    OptionsPanel::Config plug;
    plug.versionText = "0.0.0-test";
    OptionsPanel p1 (options, plug);
    p1.setSize (720, 400);
    CHECK (p1.getReferenceToneButton() == nullptr);          // plug-in: no standalone-only controls

    OptionsPanel::Config sa;
    sa.standalone = true;
    bool tone = false;
    sa.onReferenceTone = [&] (bool on) { tone = on; };
    sa.createDeviceSelector = [] { return std::make_unique<juce::Component>(); };
    OptionsPanel p2 (options, sa);
    p2.setSize (720, 400);
    CHECK (p2.getReferenceToneButton() != nullptr);
    p2.setReferenceToneRunning (true);
    CHECK (tone && p2.isReferenceToneRunning());
    p2.setReferenceToneRunning (false);
    CHECK (! tone);
    p2.getAnimationsToggle().setToggleState (false, juce::sendNotificationSync);
    CHECK (! options.areAnimationsEnabled());
    p2.getThemeCombo().setSelectedItemIndex (1, juce::sendNotificationSync);
    CHECK (options.getThemeId() == ThemeCatalog::all()[1].id);
}

void testThemeManagerAndStatus()
{
    OptionsModel options;
    ThemeManager manager (options);
    juce::Component root;
    root.setSize (300, 200);
    manager.applyTo (root);
    CHECK (&SnaralLookAndFeel::tokensFor (root) != nullptr);
    options.setThemeId ("highcontrast");
    CHECK (SnaralLookAndFeel::tokensFor (root).id == "highcontrast");
    options.setAnimationsEnabled (false);
    CHECK (! SnaralLookAndFeel::animationsFor (root));
    manager.detach (root);

    StatusBar bar;
    bar.setSize (600, StatusBar::preferredHeight);
    bar.post (Severity::Warning, "Something");
    CHECK (bar.getMessage() == "Something" && bar.getMessageSeverity() == Severity::Warning);
    bar.setChip ("midi", Severity::Info, "MIDI");
    CHECK (bar.hasChip ("midi"));
    bar.clearChip ("midi");
    CHECK (! bar.hasChip ("midi"));

    ToastHost toasts;
    toasts.setSize (400, 300);
    for (int i = 0; i < 6; ++i)
        toasts.show (Severity::Info, "t" + juce::String (i));
    CHECK (toasts.getNumToasts() <= 6);
    toasts.dismissAll();
    CHECK (toasts.getNumToasts() == 0);
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI init;

    testThemes();
    testOptions();
    testFormatting();
    testContextActions();
    testMacroKnob();
    testParamControls();
    testAdvanced();
    testTrajectory();
    testMeters();
    testScope();
    testHelp();
    testOverlayAndPanels();
    testThemeManagerAndStatus();

    std::cout << checks << " checks, " << failures << " failures" << std::endl;
    return failures == 0 ? 0 : 1;
}
