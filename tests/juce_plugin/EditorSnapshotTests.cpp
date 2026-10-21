// Renders the REAL editor (not the kit stand-in) headlessly to PNG: every theme at 720x480, 900x560 and 1440x900, with
// the preset browser open, in Advanced, plus overlays. Usage: snaral_plugin_editorsnapshottests [output-dir]
#include <JuceHeader.h>

#include "PluginEditor.h"
#include "PluginProcessor.h"

#include <cstdio>

using namespace snaral;
namespace ui = snaral::ui;

namespace
{
juce::File outDir;
int written = 0;

void pump (int ms)
{
    const auto end = juce::Time::getMillisecondCounter() + static_cast<juce::uint32> (ms);
    while (juce::Time::getMillisecondCounter() < end)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
}

SnaralAudioProcessor* liveProc = nullptr;
SnaralAudioProcessorEditor* liveEditor = nullptr;
void sound (SnaralAudioProcessor& p, int blocks);

void save (juce::Component& c, const juce::String& name)
{
    pump (60);
    if (liveProc != nullptr && liveEditor != nullptr)
        for (int i = 0; i < 8; ++i)   // no peer, so the display timers are idle: step them by hand with live audio behind them
        {
            sound (*liveProc, 2);
            liveEditor->mainView().getScope().refreshNow();
            liveEditor->mainView().getMeter().refreshNow();
            liveEditor->mainView().getCpu().refreshNow();
        }
    auto img = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
    auto file = outDir.getChildFile (name + ".png");
    file.deleteFile();
    juce::FileOutputStream os (file);
    juce::PNGImageFormat png;
    if (os.openedOk() && png.writeImageToStream (img, os))
        ++written;
    else
        std::fprintf (stderr, "could not write %s\n", file.getFullPathName().toRawUTF8());
}

void sound (SnaralAudioProcessor& p, int blocks)
{
    for (int b = 0; b < blocks; ++b)
    {
        juce::AudioBuffer<float> buf (2, 512);
        buf.clear();
        juce::MidiBuffer m;
        if (b == 0)
            m.addEvent (juce::MidiMessage::noteOn (1, 36, 0.9f), 0);
        p.processBlock (buf, m);
    }
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    outDir = argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile (argv[1])
                      : juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("snaral-editor-snapshots");
    outDir.createDirectory();

    SnaralAudioProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    SnaralEditorConfig cfg;
    cfg.settingsFolder = outDir.getChildFile ("settings");
    cfg.settingsFolder.deleteRecursively();
    cfg.settingsFolder.createDirectory();
    cfg.autoStartTour = false;
    SnaralAudioProcessorEditor ed (proc, cfg);
    ed.options().setUiScale (1.0f);
    liveProc = &proc;
    liveEditor = &ed;
    sound (proc, 80);

    const struct { int w, h; } sizes[] = { { 720, 480 }, { 900, 560 }, { 1440, 900 } };
    for (const auto& id : ui::ThemeCatalog::ids())
    {
        ed.options().setThemeId (id);
        for (const auto& s : sizes)
        {
            const auto tag = id + "_" + juce::String (s.w) + "x" + juce::String (s.h);
            ed.setSize (s.w, s.h);
            ed.showMainView();
            sound (proc, 10);
            save (ed, "real_main_" + tag);
            if (s.w <= 900)
            {
                ed.setBrowserOpen (true);
                save (ed, "real_browser_" + tag);
                ed.setBrowserOpen (false);
            }
        }
        ed.setSize (900, 560);
        ed.showAdvancedView();
        save (ed, "real_advanced_" + id + "_900x560");
        ed.setSize (720, 480);
        save (ed, "real_advanced_" + id + "_720x480");
        ed.showMainView();
    }

    // the CDL theme in detail: every Advanced tab, overlays, dialogs
    ed.options().setThemeId ("cdl");
    ed.setSize (900, 560);
    ed.showAdvancedView();
    for (int t = 0; t < ui::numAdvancedTabs; ++t)
    {
        ed.advancedView().tabs().setCurrentTab (static_cast<ui::AdvancedTab> (t));
        save (ed, "real_tab" + juce::String (t) + "_cdl_900x560");
    }
    ed.showMainView();
    ed.showOptions();  save (ed, "real_options_cdl_900x560");   ed.closeCard();
    ed.showHelp();     save (ed, "real_help_cdl_900x560");      ed.closeCard();
    ed.showMidiMap();  save (ed, "real_midimap_cdl_900x560");   ed.closeCard();
    ed.showExport();   save (ed, "real_export_cdl_900x560");    ed.closeCard();
    ed.controller().toggleLearn ("weight");
    save (ed, "real_learn_cdl_900x560");
    ed.controller().cancelLearn();
    ed.controller().randomize();
    save (ed, "real_randomized_cdl_900x560");
    ed.controller().status (ui::Severity::Warning, "Example warning toast", 7000);
    save (ed, "real_toast_cdl_900x560");
    ed.startTour();
    for (int i = 0; i < 10 && ed.tourOverlay().isRunning(); ++i)
    {
        save (ed, "real_tour" + juce::String (i) + "_cdl_900x560");
        ed.tourOverlay().next();
    }

    std::printf ("EditorSnapshotTests: wrote %d PNGs to %s\n", written, outDir.getFullPathName().toRawUTF8());
    return written > 0 ? 0 : 1;
}
