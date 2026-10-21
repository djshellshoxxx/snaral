#include "HeaderBar.h"

namespace snaral::ui
{
HeaderBar::HeaderBar (EditorController& c) : ctl (c)
{
    setTitle ("Header");
    setFocusContainerType (FocusContainerType::focusContainer);

    auto setup = [this] (juce::TextButton& b, const juce::String& name, const juce::String& tip)
    {
        b.setTitle (name);
        b.setTooltip (tip);
        b.setWantsKeyboardFocus (true);
        addAndMakeVisible (b);
    };

    setup (prevBtn, "Previous preset", "Load the previous preset in the list");
    setup (presetBtn, "Preset name", "Current preset. Click to open the preset browser");
    setup (nextBtn, "Next preset", "Load the next preset in the list");
    setup (browseBtn, "Preset browser", "Search, filter and audition presets");
    setup (fileBtn, "File menu", "Save, Save As, Import, Export and Initialise");
    setup (mainBtn, "Main view", "Show the five macros and performance controls");
    setup (advBtn, "Advanced view", "Show every parameter in tabs");
    setup (midiBtn, "MIDI mappings", "Review, edit and remove MIDI-learn mappings");
    setup (optionsBtn, "Options", "Theme, scale, tooltips, reduced motion and about");
    setup (helpBtn, "Help", "Open help for every control, or run the introduction again");

    for (auto* b : { &mainBtn, &advBtn })
    {
        b->setClickingTogglesState (true);
        b->setRadioGroupId (4711);
    }
    mainBtn.setToggleState (true, juce::dontSendNotification);

    prevBtn.onClick = [this] { ctl.stepPreset (-1); };
    nextBtn.onClick = [this] { ctl.stepPreset (+1); };
    presetBtn.onClick = browseBtn.onClick = [this] { if (onBrowse) onBrowse(); };
    fileBtn.onClick = [this] { showFileMenu(); };
    mainBtn.onClick = [this] { if (mainBtn.getToggleState()) setView (View::Main, true); };
    advBtn.onClick = [this] { if (advBtn.getToggleState()) setView (View::Advanced, true); };
    midiBtn.onClick = [this] { if (onMidi) onMidi(); };
    optionsBtn.onClick = [this] { if (onOptions) onOptions(); };
    helpBtn.onClick = [this] { if (onHelp) onHelp(); };

    ctl.addListener (this);
    refresh();
}

HeaderBar::~HeaderBar() { ctl.removeListener (this); }

void HeaderBar::setView (View v, bool notify)
{
    view = v;
    mainBtn.setToggleState (v == View::Main, juce::dontSendNotification);
    advBtn.setToggleState (v == View::Advanced, juce::dontSendNotification);
    if (notify && onViewChanged)
        onViewChanged (v);
}

void HeaderBar::setBrowserOpen (bool open)
{
    browseBtn.setToggleState (open, juce::dontSendNotification);
}

void HeaderBar::refresh()
{
    const auto name = ctl.presetName();
    const bool mod = ctl.isModified();
    presetBtn.setButtonText (mod ? name + "  (edited)" : name);
    presetBtn.setTitle ("Preset: " + name + (mod ? ", edited since loading" : ""));
    repaint();
}

void HeaderBar::showFileMenu()
{
    juce::PopupMenu m;
    m.addItem (1, "Save");
    m.addItem (2, "Save As...");
    m.addItem (3, "Import preset...");
    m.addItem (4, "Export preset...");
    m.addSeparator();
    m.addItem (5, "Export WAV...");
    m.addSeparator();
    m.addItem (6, "Initialise patch");
    m.addItem (7, "Rescan preset folder");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (fileBtn).withMinimumWidth (180), [safe = juce::Component::SafePointer<HeaderBar> (this)] (int r)
    {
        if (safe == nullptr)
            return;
        auto& c = safe->ctl;
        switch (r)
        {
            case 1: c.savePreset(); break;
            case 2: c.savePresetAs(); break;
            case 3: c.importPreset(); break;
            case 4: c.exportPreset(); break;
            case 5: if (safe->onExportWav) safe->onExportWav(); break;
            case 6: c.resetPatch(); break;
            case 7: c.rescanLibrary(); break;
            default: break;
        }
    });
}

void HeaderBar::paint (juce::Graphics& g)
{
    const auto& t = SnaralLookAndFeel::tokensFor (*this);
    g.setColour (t.panel);
    g.fillRect (getLocalBounds());
    g.setColour (t.border);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, static_cast<float> (getWidth()));
    SnaralLookAndFeel::drawSignatureNotch (g, t);
    g.setColour (t.textPrimary);
    g.setFont (fonts::title());
    g.drawText ("SNARAL", 14, 0, 70, getHeight(), juce::Justification::centredLeft, false);
}

void HeaderBar::resized()
{
    auto r = getLocalBounds().reduced (8, 2);
    r.removeFromLeft (68);
    const int gap = 4;
    auto take = [&] (juce::Component& c, int w, bool fromRight)
    {
        auto s = fromRight ? r.removeFromRight (w) : r.removeFromLeft (w);
        c.setBounds (s);
        (fromRight ? r.removeFromRight (gap) : r.removeFromLeft (gap));
    };
    take (helpBtn, 30, true);
    take (optionsBtn, 62, true);
    take (midiBtn, 44, true);
    take (advBtn, 90, true);
    take (mainBtn, 68, true);
    take (fileBtn, 44, true);
    take (browseBtn, 60, true);
    take (prevBtn, 30, false);
    take (nextBtn, 30, true);
    presetBtn.setBounds (r);
}
} // namespace snaral::ui
