// Headless UI snapshot renderer. Paints the UI kit widgets (no display needed: components are rendered into a
// juce::Image with the software renderer) and writes PNGs. Usage: snaral_juce_uisnapshottests [output-dir]
// The default output directory is ./ui-snapshots. See tools/ui_snapshot/README.md.

#include "UiKitUnity.h"

#include "Parameters/JuceParameterLayout.h"

#include <iostream>

using namespace snaral;
using namespace snaral::ui;

namespace
{
juce::File outDir;
int written = 0;

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

void save (juce::Component& c, const juce::String& name)
{
    // paint the whole hierarchy; the component does not need a peer
    auto img = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
    auto file = outDir.getChildFile (name + ".png");
    file.deleteFile();
    juce::FileOutputStream os (file);
    juce::PNGImageFormat png;
    if (os.openedOk() && png.writeImageToStream (img, os))
        ++written;
    else
        std::cerr << "could not write " << file.getFullPathName() << std::endl;
}

struct Sheet : juce::Component
{
    void paint (juce::Graphics& g) override { g.fillAll (SnaralLookAndFeel::tokensFor (*this).background); }
};

struct FakeScope : ScopeSource
{
    ScopeRingBuffer ring;
    FakeScope()
    {
        ring.prepare (16384, 2);
        std::vector<float> l (8192), r (8192);
        for (std::size_t i = 0; i < l.size(); ++i)
        {
            const float ph = static_cast<float> (i) / 48000.0f;
            l[i] = 0.55f * std::sin (juce::MathConstants<float>::twoPi * 55.0f * ph)
                   + 0.25f * std::sin (juce::MathConstants<float>::twoPi * 110.0f * ph + 0.4f);
            r[i] = 0.55f * std::sin (juce::MathConstants<float>::twoPi * 55.0f * ph)
                   + 0.2f * std::sin (juce::MathConstants<float>::twoPi * 330.0f * ph);
        }
        const float* ch[2] = { l.data(), r.data() };
        ring.push (ch, 2, 8192);
    }
    int pullLatest (ScopeTap, int ch, float* d, int n) noexcept override { return ring.pullLatest (ch, d, n); }
    bool hasTap (ScopeTap) const noexcept override { return true; }
    double getSampleRate() const noexcept override { return 48000.0; }
};

// A stand-in for the real editor so the kit is judged in composition: header, macros, scope + meter + CPU, tabs, status.
struct MockEditor : juce::Component
{
    MockEditor (juce::AudioProcessorValueTreeState& apvts, OptionsModel& opts, FakeScope& scopeSource, MeterSource& meters, CpuSource& cpuSrc)
        : options (opts)
    {
        const char* ids[] = { "weight", "growl", "bite", "motion", "space" };
        const char* labels[] = { "Weight", "Growl", "Bite", "Motion", "Space" };
        for (int i = 0; i < 5; ++i)
        {
            knobs[static_cast<std::size_t> (i)] = std::make_unique<MacroKnob> (labels[i], MacroKnob::Size::Normal);
            knobs[static_cast<std::size_t> (i)]->bind (apvts, ids[i]);
            addAndMakeVisible (*knobs[static_cast<std::size_t> (i)]);
        }
        knobs[1]->setLearnState (MacroKnob::LearnState::Learned, "CC 21");
        knobs[2]->setLearnState (MacroKnob::LearnState::Armed);
        scope.setSource (&scopeSource);
        scope.refreshNow();
        meter.setSource (&meters);
        meter.refreshNow();
        cpu.setSource (&cpuSrc);
        cpu.refreshNow (1.0);
        for (int i = 0; i < 100; ++i)
            cpu.refreshNow (0.1);
        addAndMakeVisible (scope);
        addAndMakeVisible (meter);
        addAndMakeVisible (cpu);
        addAndMakeVisible (tabs);
        tabs.buildDefaultTabs (apvts);
        addAndMakeVisible (status);
        status.setVersionText ("0.2.0-beta.1");
        status.post (Severity::Success, "Preset loaded: Reese Foundation");
        status.setChip ("midi", Severity::Info, "MIDI 1 ch");
        status.setChip ("cpu", Severity::Warning, "Buffer 64");
        addAndMakeVisible (presetBox);
        presetBox.addItem ("Reese Foundation", 1);
        presetBox.setSelectedId (1, juce::dontSendNotification);
        presetBox.setTitle ("Preset");
        addAndMakeVisible (optionsButton);
        addAndMakeVisible (helpButton);
        setSize (metrics::defaultWidth, metrics::defaultHeight);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        header = r.removeFromTop (metrics::headerHeight);
        auto h = header.reduced (12, 2);
        helpButton.setBounds (h.removeFromRight (60).withSizeKeepingCentre (60, metrics::buttonHeight));
        h.removeFromRight (6);
        optionsButton.setBounds (h.removeFromRight (84).withSizeKeepingCentre (84, metrics::buttonHeight));
        h.removeFromLeft (110);
        presetBox.setBounds (h.removeFromLeft (220).withSizeKeepingCentre (220, metrics::buttonHeight));
        status.setBounds (r.removeFromBottom (StatusBar::preferredHeight));
        r.reduce (12, 10);

        const bool compact = getWidth() < 800;
        const int topH = compact ? 128 : 150;
        auto top = r.removeFromTop (topH);
        const int kw = knobs[0]->getPreferredSize().x;
        auto kArea = top.removeFromLeft ((kw + 4) * 5);
        for (auto& k : knobs)
        {
            auto cell = kArea.removeFromLeft (kw + 4);
            k->setBounds (cell.withSizeKeepingCentre (k->getPreferredSize().x, k->getPreferredSize().y));
        }
        top.removeFromLeft (10);
        auto right = top.removeFromRight (110);
        cpu.setBounds (right.removeFromTop (34));
        right.removeFromTop (4);
        meter.setBounds (right.withWidth (compact ? 44 : 56));
        top.removeFromRight (8);
        scope.setBounds (top);
        r.removeFromTop (8);
        tabs.setBounds (r);
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = SnaralLookAndFeel::tokensFor (*this);
        g.fillAll (t.background);
        g.setColour (t.panel);
        g.fillRect (header);
        g.setColour (t.border);
        g.drawHorizontalLine (header.getBottom() - 1, 0.0f, static_cast<float> (getWidth()));
        g.setColour (t.textPrimary);
        g.setFont (fonts::title());
        g.drawText ("SNARAL", 16, 0, 100, header.getHeight(), juce::Justification::centredLeft, false);
        SnaralLookAndFeel::drawSignatureNotch (g, t);
    }

    OptionsModel& options;
    juce::Rectangle<int> header;
    std::array<std::unique_ptr<MacroKnob>, 5> knobs;
    ScopeView scope;
    LevelMeter meter;
    CpuGauge cpu;
    TabbedAdvanced tabs;
    StatusBar status;
    juce::ComboBox presetBox;
    juce::TextButton optionsButton { "Options" }, helpButton { "Help" };
};

struct Env
{
    DummyProcessor processor;
    juce::AudioProcessorValueTreeState apvts { processor, nullptr, "STATE", createParameterLayout() };
    OptionsModel options;
    ThemeManager themes { options };
    FakeScope scope;
    AtomicMeterSource meter;
    AtomicCpuSource cpu;

    void feedMeter()
    {
        std::vector<float> blk (256);
        for (std::size_t i = 0; i < blk.size(); ++i)
            blk[i] = 0.35f * std::sin (static_cast<float> (i) * 0.2f);
        meter.update (blk.data(), blk.data(), 256);
        cpu.reportBlock (0.0011, 0.0029);
    }
    Env() { feedMeter(); }
};

void sizeName (juce::String& n, int w, int h) { n << "_" << w << "x" << h; }

void renderComposite (Env& env, const juce::String& themeId, int w, int h)
{
    env.options.setThemeId (themeId);
    env.feedMeter();
    MockEditor ed (env.apvts, env.options, env.scope, env.meter, env.cpu);
    env.themes.applyTo (ed);
    ed.setSize (w, h);
    for (int i = 0; i < numAdvancedTabs; ++i)
        ed.tabs.setCurrentTab (static_cast<AdvancedTab> (i));
    ed.tabs.setCurrentTab (AdvancedTab::Voice);
    juce::String n = "editor_" + themeId;
    sizeName (n, w, h);
    save (ed, n);
    env.themes.detach (ed);
}

void renderAdvancedTabs (Env& env, int w, int h)
{
    env.options.setThemeId ("cdl");
    env.feedMeter();
    MockEditor ed (env.apvts, env.options, env.scope, env.meter, env.cpu);
    env.themes.applyTo (ed);
    ed.setSize (w, h);
    for (int i = 0; i < numAdvancedTabs; ++i)
    {
        const auto tab = static_cast<AdvancedTab> (i);
        ed.tabs.setCurrentTab (tab);
        juce::String n = "advanced_" + juce::String (i) + "_" + juce::String (advancedTabName (tab)).retainCharacters ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ").toLowerCase();
        sizeName (n, w, h);
        save (ed.tabs, n + "_top");
        ed.tabs.scrollToBottom (tab);
        save (ed.tabs, n + "_bottom");
        ed.tabs.setScrollPosition (tab, 0);
    }
    env.themes.detach (ed);
}

void renderWidgetSheet (Env& env, const juce::String& themeId)
{
    env.options.setThemeId (themeId);
    Sheet sheet;
    env.themes.applyTo (sheet);
    sheet.setSize (720, 480);

    auto mk = [&] (const char* label, MacroKnob::Size s, const char* id) {
        auto k = std::make_unique<MacroKnob> (label, s);
        k->bind (env.apvts, id);
        sheet.addAndMakeVisible (*k);
        return k;
    };
    auto small = mk ("Cutoff", MacroKnob::Size::Small, "weight");
    auto normal = mk ("Growl", MacroKnob::Size::Normal, "growl");
    auto large = mk ("Weight", MacroKnob::Size::Large, "weight");
    auto armed = mk ("Armed", MacroKnob::Size::Normal, "bite");
    armed->setLearnState (MacroKnob::LearnState::Armed);
    auto learned = mk ("Learned", MacroKnob::Size::Normal, "motion");
    learned->setLearnState (MacroKnob::LearnState::Learned, "CC 74");
    auto disabled = mk ("Disabled", MacroKnob::Size::Normal, "space");
    disabled->setEnabled (false);
    auto long1 = mk ("A Very Long Label Name", MacroKnob::Size::Normal, "space");

    std::vector<MacroKnob*> row { small.get(), normal.get(), large.get(), armed.get(), learned.get(), disabled.get(), long1.get() };
    int x = 14;
    for (auto* k : row)
    {
        const auto ps = k->getPreferredSize();
        k->setBounds (x, 14, ps.x, ps.y);
        x += ps.x + 12;
    }

    const char* sliderIds[] = { "lfo_rate_hz", "traj_length_ms", "traj_depth" };
    int y = 150;
    std::vector<std::unique_ptr<ParamSlider>> sliders;
    for (auto* id : sliderIds)
    {
        sliders.push_back (std::make_unique<ParamSlider> (env.apvts, id));
        sheet.addAndMakeVisible (*sliders.back());
        sliders.back()->setBounds (14, y, 330, ParamSlider::preferredHeight);
        y += 38;
    }
    ParamCombo combo (env.apvts, "traj_dest");
    ParamToggle toggle (env.apvts, "traj_loop");
    ParamToggle toggleOn (env.apvts, "traj_enabled");
    toggleOn.setToggleState (true, juce::dontSendNotification);
    sheet.addAndMakeVisible (combo);
    sheet.addAndMakeVisible (toggle);
    sheet.addAndMakeVisible (toggleOn);
    combo.setBounds (14, y + 6, juce::jmax (180, combo.getPreferredWidth()), ParamCombo::preferredHeight);
    toggle.setBounds (240, y + 12, toggle.getPreferredWidth(), metrics::buttonHeight);
    toggleOn.setBounds (14, y + 70, toggleOn.getPreferredWidth(), metrics::buttonHeight);

    juce::Component body1;
    body1.setSize (300, 60);
    SectionPanel open ("Expanded section", &body1);
    SectionPanel closed ("Collapsed section");
    closed.setExpanded (false);
    sheet.addAndMakeVisible (open);
    sheet.addAndMakeVisible (closed);
    open.setBounds (370, 150, 330, open.getPreferredHeight (330));
    closed.setBounds (370, 150 + open.getHeight() + 10, 330, closed.getPreferredHeight (330));

    StatusBar bar;
    bar.setBounds (0, 454, 720, StatusBar::preferredHeight);
    sheet.addAndMakeVisible (bar);
    bar.post (Severity::Warning, "Audio buffer is small; expect crackles on slow machines.");
    bar.setChip ("a", Severity::Error, "No MIDI");
    bar.setChip ("b", Severity::Success, "Synced");
    bar.setVersionText ("0.2.0-beta.1");

    save (sheet, "widgets_" + themeId + "_720x480");

    ToastHost toasts;
    toasts.setBounds (0, 0, 720, 300);
    sheet.addAndMakeVisible (toasts);
    toasts.show (Severity::Info, "Preset copied to clipboard.");
    toasts.show (Severity::Success, "Saved.");
    toasts.show (Severity::Warning, "Value out of range, kept the previous value.");
    toasts.show (Severity::Error, "Could not read file.");
    Sheet empty;
    empty.setSize (720, 300);
    env.themes.applyTo (empty);
    empty.addAndMakeVisible (toasts);
    toasts.setBounds (0, 0, 720, 300);
    save (empty, "toasts_" + themeId + "_720x300");

    env.themes.detach (empty);
    env.themes.detach (sheet);
}

void renderDisplays (Env& env)
{
    env.options.setThemeId ("cdl");
    Sheet sheet;
    env.themes.applyTo (sheet);
    sheet.setSize (720, 300);
    ScopeView sv;
    sv.setSource (&env.scope);
    sv.refreshNow();
    sheet.addAndMakeVisible (sv);
    sv.setBounds (10, 10, 420, 170);
    LevelMeter vm;
    vm.setSource (&env.meter);
    vm.refreshNow();
    sheet.addAndMakeVisible (vm);
    vm.setBounds (450, 10, 56, 170);
    LevelMeter hm (LevelMeter::Orientation::Horizontal);
    AtomicMeterSource meter2;
    {
        std::vector<float> blk (256);
        for (std::size_t i = 0; i < blk.size(); ++i)
            blk[i] = 0.35f * std::sin (static_cast<float> (i) * 0.2f);
        meter2.update (blk.data(), blk.data(), 256);
    }
    hm.setSource (&meter2);
    hm.refreshNow();
    sheet.addAndMakeVisible (hm);
    hm.setBounds (520, 10, 180, 40);
    CpuGauge cg;
    cg.setSource (&env.cpu);
    for (int i = 0; i < 100; ++i)
        cg.refreshNow (0.1);
    sheet.addAndMakeVisible (cg);
    cg.setBounds (520, 70, 150, 34);
    CpuGauge cgOver;
    AtomicCpuSource over;
    over.reportBlock (0.0035, 0.0029);
    cgOver.setSource (&over);
    for (int i = 0; i < 100; ++i)
        cgOver.refreshNow (0.1);
    sheet.addAndMakeVisible (cgOver);
    cgOver.setBounds (520, 114, 190, 34);
    CpuGauge cgNone;
    AtomicCpuSource none;
    cgNone.setSource (&none);
    cgNone.refreshNow();
    sheet.addAndMakeVisible (cgNone);
    cgNone.setBounds (520, 158, 190, 34);
    ScopeView empty;
    sheet.addAndMakeVisible (empty);
    empty.setBounds (10, 190, 420, 100);
    LevelMeter noData;
    sheet.addAndMakeVisible (noData);
    noData.setBounds (450, 190, 56, 100);
    save (sheet, "displays_cdl_720x300");
    env.themes.detach (sheet);
}

void renderTrajectory (Env& env, const juce::String& themeId, int w, int h)
{
    env.options.setThemeId (themeId);
    Sheet sheet;
    env.themes.applyTo (sheet);
    sheet.setSize (w, h);
    auto binding = std::make_unique<ApvtsTrajectoryBinding> (env.apvts);
    binding->tempoProvider = [] { return 174.0; };
    TrajectoryEditor ed (std::move (binding));
    sheet.addAndMakeVisible (ed);
    ed.setBounds (10, 10, w - 20, h - 20);
    ed.selectPoint (2);
    juce::String n = "trajectory_" + themeId;
    sizeName (n, w, h);
    save (sheet, n);
    env.themes.detach (sheet);
}

void renderPanels (Env& env)
{
    env.options.setThemeId ("cdl");
    for (const bool standalone : { false, true })
    {
        for (const auto& size : { juce::Point<int> (720, 480), juce::Point<int> (900, 560) })
        {
            Sheet sheet;
            env.themes.applyTo (sheet);
            sheet.setSize (size.x, size.y);
            OptionsPanel::Config cfg;
            cfg.standalone = standalone;
            cfg.versionText = "0.2.0-beta.1";
            cfg.formatText = standalone ? "Standalone" : "VST3";
            cfg.onRerunTour = [] {};
            cfg.onOpenHelp = [] {};
            cfg.onClose = [] {};
            cfg.onReferenceTone = [] (bool) {};
            cfg.createDeviceSelector = [] {
                struct Fake : juce::Component
                {
                    void paint (juce::Graphics& g) override
                    {
                        g.fillAll (juce::Colours::darkgrey.darker());
                        g.setColour (juce::Colours::white);
                        g.drawText ("(audio device selector placeholder)", getLocalBounds(), juce::Justification::centred);
                    }
                };
                auto c = std::make_unique<Fake>();
                c->setSize (400, 220);
                return std::unique_ptr<juce::Component> (std::move (c));
            };
            OptionsPanel panel (env.options, cfg);
            sheet.addAndMakeVisible (panel);
            panel.setBounds (sheet.getLocalBounds());
            juce::String n = juce::String ("options_") + (standalone ? "standalone" : "plugin");
            sizeName (n, size.x, size.y);
            save (sheet, n);
            if (standalone && size.x == 720)
            {
                panel.getViewport().setViewPosition (0, 10000);
                save (sheet, n + "_scrolled");
            }
            env.themes.detach (sheet);
        }
    }

    for (const auto& size : { juce::Point<int> (720, 480), juce::Point<int> (900, 560) })
    {
        Sheet sheet;
        env.themes.applyTo (sheet);
        sheet.setSize (size.x, size.y);
        HelpPanel help;
        help.callbacks.rerunTour = [] {};
        help.callbacks.closePanel = [] {};
        sheet.addAndMakeVisible (help);
        help.setBounds (sheet.getLocalBounds());
        help.showTopic ("macro-weight");
        juce::String n = "help";
        sizeName (n, size.x, size.y);
        save (sheet, n);
        help.setSearchText ("filter");
        save (sheet, n + "_search");
        help.setSearchText ("zzzzqqq");
        save (sheet, n + "_nomatch");
        env.themes.detach (sheet);
    }
}

void renderOnboarding (Env& env)
{
    env.options.setThemeId ("cdl");
    for (const auto& size : { juce::Point<int> (720, 480), juce::Point<int> (900, 560) })
    {
        env.feedMeter();
    MockEditor ed (env.apvts, env.options, env.scope, env.meter, env.cpu);
        env.themes.applyTo (ed);
        ed.setSize (size.x, size.y);
        OnboardingOverlay tour;
        ed.addChildComponent (tour);
        tour.setBounds (ed.getLocalBounds());
        tour.registerTarget ("macro.weight", ed.knobs[0].get());
        tour.registerTarget ("macro.growl", ed.knobs[1].get());
        tour.registerTarget ("macro.bite", ed.knobs[2].get());
        tour.registerTarget ("macro.motion", ed.knobs[3].get());
        tour.registerTarget ("macro.space", ed.knobs[4].get());
        tour.registerTarget ("preset.selector", &ed.presetBox);
        tour.registerTarget ("scope", &ed.scope);
        tour.registerTarget ("advanced.tabs", &ed.tabs);
        tour.registerTarget ("options.button", &ed.optionsButton);
        tour.start();
        for (int i = 0; i < tour.getNumSteps(); ++i)
        {
            tour.goToStep (i);
            juce::String n = "tour_step" + juce::String (i).paddedLeft ('0', 2);
            sizeName (n, size.x, size.y);
            save (ed, n);
        }
        env.themes.detach (ed);
    }
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;

    outDir = argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile (argv[1])
                      : juce::File::getCurrentWorkingDirectory().getChildFile ("ui-snapshots");
    outDir.createDirectory();

    Env env;
    for (const auto& id : ThemeCatalog::ids())
    {
        renderComposite (env, id, 900, 560);
        renderComposite (env, id, 720, 480);
        renderWidgetSheet (env, id);
    }
    renderAdvancedTabs (env, 720, 480);
    renderAdvancedTabs (env, 900, 560);
    renderDisplays (env);
    renderTrajectory (env, "cdl", 720, 300);
    renderTrajectory (env, "highcontrast", 900, 360);
    renderPanels (env);
    renderOnboarding (env);

    std::cout << "wrote " << written << " snapshots to " << outDir.getFullPathName() << std::endl;
    return written > 0 ? 0 : 1;
}
