// Real-editor tests: open the editor headless, check that every parameter has a visible control, drive every control the
// way a user would and confirm the APVTS follows, then exercise the surrounding actions (undo, panic, A/B, variations,
// presets, learn, dialogs, overlays, resize, project-state round trip). No audio device is needed.
#include <JuceHeader.h>

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "TestSupport.h"

#include <cmath>
#include <cstdio>
#include <set>
#include <string>

using namespace snaral;
namespace ui = snaral::ui;

namespace
{
// Parameters that intentionally have no dedicated primary control. Each entry says why.
// (Kept empty on purpose: utility parameters live in the action row / Advanced header and macros on the Main view, and
// the trajectory points are edited on the Trajectory canvas, all of which register in the ControlRegistry.)
const std::set<std::string> intentionallyHidden = {};

juce::File freshFolder (const char* name)
{
    auto d = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile (juce::String ("snaral-editor-test-") + name);
    d.deleteRecursively();
    d.createDirectory();
    return d;
}

void pump (int ms)
{
    const auto end = juce::Time::getMillisecondCounter() + static_cast<juce::uint32> (ms);
    while (juce::Time::getMillisecondCounter() < end)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
}

void renderBlocks (SnaralAudioProcessor& p, int blocks)
{
    for (int b = 0; b < blocks; ++b)
    {
        juce::AudioBuffer<float> buf (2, 256);
        buf.clear();
        juce::MidiBuffer m;
        if (b == 0)
            m.addEvent (juce::MidiMessage::noteOn (1, 40, 0.8f), 0);
        p.processBlock (buf, m);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 256; ++i)
                CHECK (std::isfinite (buf.getSample (c, i)));
    }
}

[[maybe_unused]] float rawOf (SnaralAudioProcessor& p, const juce::String& id)
{
    return p.getParameters().getParameter (id)->getValue();
}

struct Rig
{
    explicit Rig (const char* name)
    {
        proc = std::make_unique<SnaralAudioProcessor>();
        proc->prepareToPlay (48000.0, 256);
        SnaralEditorConfig cfg;
        cfg.settingsFolder = freshFolder (name);
        cfg.autoStartTour = false;
        editor = std::make_unique<SnaralAudioProcessorEditor> (*proc, cfg);
        editor->setSize (900, 560);
        pump (50);
    }
    ~Rig()
    {
        editor.reset();
        proc->releaseResources();
    }
    std::unique_ptr<SnaralAudioProcessor> proc;
    std::unique_ptr<SnaralAudioProcessorEditor> editor;
};
} // namespace

static void testEveryParameterHasAControl()
{
    Rig rig ("registry");
    auto& reg = rig.editor->registry();
    int missing = 0;
    for (const auto& d : parameterTable)
    {
        const std::string id (d.id);
        const juce::String jid (id);
        CHECK (rig.proc->getParameters().getParameter (jid) != nullptr);
        if (intentionallyHidden.count (id) != 0)
            continue;
        if (reg.primaryCount (jid) < 1)
        {
            std::fprintf (stderr, "No visible control for parameter '%s'\n", id.c_str());
            ++missing;
        }
        else
            CHECK (reg.primaryCount (jid) == 1);   // exactly one primary control per parameter
    }
    CHECK (missing == 0);
    for (const auto& e : reg.all())
        CHECK (rig.proc->getParameters().getParameter (e.id) != nullptr);
    std::printf ("  %d parameters, %d registered controls\n", parameterCount, static_cast<int> (reg.all().size()));
}

static void testTweakEveryControl()
{
    Rig rig ("tweak");
    auto& reg = rig.editor->registry();
    rig.editor->showAdvancedView();
    pump (50);
    int driven = 0;

    for (const auto& e : reg.all())
    {
        if (e.component == nullptr)
            continue;
        auto* prm = rig.proc->getParameters().getParameter (e.id);
        CHECK (prm != nullptr);
        const float before = prm->getValue();
        auto* comp = e.component.getComponent();

        auto expectChanged = [&] (bool changed)
        {
            if (! changed)
                std::fprintf (stderr, "Control for '%s' did not move the parameter\n", e.id.toRawUTF8());
            CHECK (changed);
            ++driven;
        };

        if (auto* ps = dynamic_cast<ui::ParamSlider*> (comp))
        {
            auto& s = ps->getSlider();
            const double target = s.getValue() > (s.getMinimum() + s.getMaximum()) * 0.5 ? s.getMinimum() : s.getMaximum();
            s.setValue (target, juce::sendNotificationSync);
            expectChanged (prm->getValue() != before);
            s.setValue (s.getMinimum() + (s.getMaximum() - s.getMinimum()) * 0.37, juce::sendNotificationSync);
        }
        else if (auto* pc = dynamic_cast<ui::ParamCombo*> (comp))
        {
            auto& cb = pc->getComboBox();
            const int n = cb.getNumItems();
            CHECK (n >= 2);
            cb.setSelectedItemIndex ((cb.getSelectedItemIndex() + 1) % n, juce::sendNotificationSync);
            expectChanged (prm->getValue() != before);
        }
        else if (auto* pt = dynamic_cast<ui::ParamToggle*> (comp))
        {
            pt->setToggleState (! pt->getToggleState(), juce::sendNotificationSync);
            expectChanged (prm->getValue() != before);
            pt->setToggleState (! pt->getToggleState(), juce::sendNotificationSync);
        }
        else if (auto* mk = dynamic_cast<ui::MacroKnob*> (comp))
        {
            mk->setValue (mk->getValue() > 50.0 ? 20.0 : 80.0, juce::sendNotificationSync);
            expectChanged (prm->getValue() != before);
        }
        else if (auto* te = dynamic_cast<ui::TrajectoryEditor*> (comp))
        {
            // drive the point that owns this parameter
            const auto xs = te->getX();
            const auto ys = te->getY();
            bool moved = false;
            for (int idx = 0; idx < 5 && ! moved; ++idx)
            {
                const auto before2 = std::make_pair (xs[static_cast<std::size_t> (idx)], ys[static_cast<std::size_t> (idx)]);
                const double nx = juce::jlimit (0.0, 1.0, before2.first + 0.01), ny = juce::jlimit (0.0, 1.0, before2.second > 0.5 ? 0.3 : 0.7);
                te->movePointTo (idx, nx, ny);
                moved = std::abs (prm->getValue() - before) > 1.0e-6f;
            }
            expectChanged (moved);
        }
        else if (auto* tb = dynamic_cast<juce::Button*> (comp))
        {
            if (tb->getClickingTogglesState() || tb->getToggleState() || tb->getRadioGroupId() == 0)
            {
                tb->setToggleState (! tb->getToggleState(), juce::sendNotificationSync);
                expectChanged (prm->getValue() != before);
                tb->setToggleState (! tb->getToggleState(), juce::sendNotificationSync);
            }
            else
            {
                tb->triggerClick();
                ++driven;
            }
        }
        else
        {
            std::fprintf (stderr, "Registry entry '%s' has an unsupported control type\n", e.id.toRawUTF8());
            CHECK (false);
        }
        pump (5);
    }
    renderBlocks (*rig.proc, 8);
    std::printf ("  drove %d controls\n", driven);
}

static void testActionsAndOverlays()
{
    Rig rig ("actions");
    auto& ed = *rig.editor;
    auto& ctl = ed.controller();
    auto& proc = *rig.proc;

    // resize through the supported extremes in both views
    for (auto [w, h] : { std::pair { 720, 480 }, std::pair { 900, 560 }, std::pair { 1440, 900 } })
    {
        ed.setSize (w, h);
        ed.showMainView();
        pump (30);
        ed.showAdvancedView();
        pump (30);
        ed.showMainView();
    }

    // undo / redo from a knob gesture
    auto& weight = ed.mainView().macro (0);
    const double w0 = weight.getValue();
    weight.setValue (w0 > 50 ? 10.0 : 90.0, juce::sendNotificationSync);
    pump (700);
    ctl.refreshNow();
    CHECK (ctl.canUndo());
    ctl.undo();
    pump (50);
    CHECK (std::abs (weight.getValue() - w0) < 0.6);
    ctl.redo();
    pump (50);
    CHECK (std::abs (weight.getValue() - w0) > 1.0);

    // keyboard shortcuts reach the controller
    ed.keyPressed (juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0));
    pump (30);
    ed.keyPressed (juce::KeyPress ('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0));
    pump (30);
    CHECK (ed.keyPressed (juce::KeyPress ('.', juce::ModifierKeys::commandModifier, 0)));

    // randomize logs a seed and changes the patch; reproduce gives the same patch
    const auto before = ctl.snapshot();
    ctl.randomize();
    pump (30);
    CHECK (ctl.seedHex().isNotEmpty());
    const auto afterRandom = ctl.snapshot();
    CHECK (before.v != afterRandom.v);
    ctl.resetPatch();
    pump (30);
    ctl.reproduceRandomize();
    pump (30);
    CHECK (ctl.snapshot().v == afterRandom.v);

    // A/B, copy, variations
    ctl.selectAb (AbCompare::Side::B);
    CHECK (ctl.abEngaged());
    ctl.copyAToB();
    ctl.selectAb (AbCompare::Side::A);
    ctl.storeVariation (1);
    CHECK (! ctl.variationEmpty (1));
    ctl.recallVariation (1);
    ctl.clearVariation (1);
    CHECK (ctl.variationEmpty (1));

    // presets: step through the factory list, browser and audition do not break anything
    ctl.rescanLibrary();
    ed.setBrowserOpen (true);
    pump (50);
    ctl.stepPreset (+1);
    pump (50);
    CHECK (ctl.presetName().isNotEmpty());
    ed.setBrowserOpen (false);

    // overlays
    ed.showOptions();   pump (30); ed.closeCard();
    ed.showHelp();      pump (30); ed.closeCard();
    ed.showMidiMap();   pump (30); ed.closeCard();
    ed.showExport();    pump (30); ed.closeCard();
    pump (50);

    // MIDI learn arm / cancel
    ctl.toggleLearn ("weight");
    CHECK (ctl.learnArmed());
    ctl.cancelLearn();
    CHECK (! ctl.learnArmed());

    // tour runs through and restores the patch
    const auto snap = ctl.snapshot();
    ed.startTour();
    for (int i = 0; i < 12 && ed.tourOverlay().isRunning(); ++i)
        ed.tourOverlay().next();
    CHECK (! ed.tourOverlay().isRunning());
    CHECK (ctl.snapshot().v == snap.v);
    CHECK (ed.options().isFirstRunComplete());

    renderBlocks (proc, 8);
}

static void testProjectStateRoundTrip()
{
    juce::MemoryBlock state;
    {
        Rig rig ("state1");
        rig.editor->options().setThemeId ("midnight");
        rig.editor->options().setScopeVisible (false);
        rig.editor->showAdvancedView();
        pump (30);
        rig.proc->getStateInformation (state);
    }
    Rig rig2 ("state2");
    const auto paramsBefore = rig2.editor->controller().snapshot();
    rig2.proc->setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    pump (150);
    CHECK (rig2.editor->options().getThemeId() == "midnight");
    CHECK (! rig2.editor->options().isScopeVisible());
    CHECK (rig2.editor->isAdvancedShown());
    // restoring UI state must not write host parameters
    CHECK (rig2.editor->controller().snapshot().v == paramsBefore.v);

    // a new editor on the restored processor opens with the same options
    SnaralEditorConfig cfg;
    cfg.settingsFolder = freshFolder ("state3");
    cfg.autoStartTour = false;
    SnaralAudioProcessorEditor third (*rig2.proc, cfg);
    CHECK (third.options().getThemeId() == "midnight");
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    testEveryParameterHasAControl();
    testTweakEveryControl();
    testActionsAndOverlays();
    testProjectStateRoundTrip();
    std::printf ("EditorTests: all passed\n");
    return 0;
}
