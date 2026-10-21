#include "MainView.h"

#include "UI/HelpContent.h"

namespace snaral::ui
{
namespace
{
constexpr int rowGap = 8;
constexpr int hit = metrics::minHitTarget;

int buttonWidth (const juce::TextButton& b, int minWidth = 44)
{
    return juce::jmax (minWidth, static_cast<int> (std::ceil (textWidth (fonts::body().withHeight (12.5f), b.getButtonText()))) + 24);
}

const char* const lockNames[] = { "Voice", "Sub", "Sources", "Motion", "Drive", "Texture", "Effects" };

/// Seed field, Reproduce, and the lock groups (a per-user preference, not preset state).
class RandomizePopover : public juce::Component
{
public:
    explicit RandomizePopover (EditorController& c) : ctl (c)
    {
        title.setText ("Randomize", juce::dontSendNotification);
        title.setFont (fonts::heading());
        addAndMakeVisible (title);
        hint.setText ("Random values stay inside safe windows. The same seed and locks give the same sound.", juce::dontSendNotification);
        hint.setFont (fonts::small());
        hint.setMinimumHorizontalScale (1.0f);
        addAndMakeVisible (hint);

        seed.setText (ctl.hasRandomized() ? ctl.seedHex() : juce::String(), false);
        seed.setInputRestrictions (8, "0123456789abcdefABCDEF");
        seed.setFont (fonts::value());
        seed.setTitle ("Seed, 8 hexadecimal digits");
        seed.setTextToShowWhenEmpty ("seed", juce::Colours::grey);
        addAndMakeVisible (seed);

        reproduce.setTitle ("Reproduce the sound for this seed");
        reproduce.onClick = [this]
        {
            if (seed.getText().isNotEmpty() && ! ctl.setSeedFromText (seed.getText()))
                return;
            ctl.reproduceRandomize();
            seed.setText (ctl.seedHex(), false);
        };
        addAndMakeVisible (reproduce);
        fresh.setTitle ("Randomize with a new seed");
        fresh.onClick = [this]
        {
            ctl.randomize();
            seed.setText (ctl.seedHex(), false);
        };
        addAndMakeVisible (fresh);

        locksLabel.setText ("Lock groups (kept as they are)", juce::dontSendNotification);
        locksLabel.setFont (fonts::label());
        addAndMakeVisible (locksLabel);
        for (int i = 0; i < 7; ++i)
        {
            auto t = std::make_unique<juce::ToggleButton> (lockNames[i]);
            t->setToggleState ((ctl.randomizeLocks() & (1u << i)) != 0, juce::dontSendNotification);
            t->setTitle (juce::String ("Lock ") + lockNames[i]);
            t->onClick = [this] { applyLocks(); };
            addAndMakeVisible (*t);
            toggles.push_back (std::move (t));
        }
        setSize (320, 262);
    }

    void applyLocks()
    {
        rnd::LockMask m = 0;
        for (std::size_t i = 0; i < toggles.size(); ++i)
            if (toggles[i]->getToggleState())
                m |= 1u << i;
        ctl.setRandomizeLocks (m);
    }

    void paint (juce::Graphics& g) override { g.fillAll (SnaralLookAndFeel::tokensFor (*this).panel); }

    void resized() override
    {
        auto r = getLocalBounds().reduced(12);
        title.setBounds (r.removeFromTop (22));
        hint.setBounds (r.removeFromTop (34));
        r.removeFromTop (4);
        auto row = r.removeFromTop (32);
        seed.setBounds (row.removeFromLeft (110));
        row.removeFromLeft (6);
        reproduce.setBounds (row.removeFromLeft (92));
        row.removeFromLeft (6);
        fresh.setBounds (row);
        r.removeFromTop (10);
        locksLabel.setBounds (r.removeFromTop (18));
        const int colW = r.getWidth() / 2;
        auto left = r.removeFromLeft (colW);
        for (std::size_t i = 0; i < toggles.size(); ++i)
            toggles[i]->setBounds ((i < 4 ? left : r).removeFromTop (hit));
    }

private:
    EditorController& ctl;
    juce::Label title, hint, locksLabel;
    juce::TextEditor seed;
    juce::TextButton reproduce { "Reproduce" }, fresh { "New seed" };
    std::vector<std::unique_ptr<juce::ToggleButton>> toggles;
};
} // namespace

template <class C>
void MainView::registerSecondary (const juce::String& id, C& c)
{
    registry.add (id, &c, false);
    ctl.installHooks (id, c.hooks);
}

MainView::MainView (EditorController& controller, ControlRegistry& reg, DiagnosticsSources& src)
    : ctl (controller), registry (reg), sources (src)
{
    setTitle ("Main view");
    setFocusContainerType (FocusContainerType::focusContainer);
    auto& apvts = ctl.apvts();

    // ---- action row ----
    const struct { juce::TextButton* b; const char* name; const char* tip; } actions[] = {
        { &panicBtn, "Panic", "Stop all sound now (Ctrl or Cmd + Period)" },
        { &resetBtn, "Reset", "Load the initialized patch. One undo step." },
        { &randomBtn, "Randomize", "Randomize within safe ranges. One undo step." },
        { &seedBtn, "Randomize seed and locks", "Show the seed, reproduce a sound, lock groups" },
        { &bypassBtn, "Bypass", "Silence the instrument output. Voices keep running." },
        { &undoBtn, "Undo", "Undo (Ctrl or Cmd + Z)" },
        { &redoBtn, "Redo", "Redo (Shift + Ctrl or Cmd + Z)" },
        { &abA, "Compare A", "Switch to compare slot A" },
        { &abB, "Compare B", "Switch to compare slot B" },
        { &copyBtn, "Copy A to B", "Copy the active compare slot into the other one" },
        { &saveBtn, "Save", "Save the preset to your library" },
        { &exportBtn, "Export WAV", "Render the current sound and a phrase to a WAV file" },
        { &moreBtn, "More actions", "More actions" },
    };
    for (const auto& a : actions)
    {
        a.b->setTitle (a.name);
        a.b->setTooltip (a.tip);
        a.b->setWantsKeyboardFocus (true);
        addAndMakeVisible (*a.b);
    }
    seedBtn.setLookAndFeel (nullptr);
    panicBtn.onClick = [this] { ctl.panic(); };
    panic2Btn.setTitle ("Panic");
    panic2Btn.setTooltip ("Stop all sound now (Ctrl or Cmd + Period)");
    panic2Btn.onClick = [this] { ctl.panic(); };
    addAndMakeVisible (panic2Btn);
    resetBtn.onClick = [this] { ctl.resetPatch(); };
    randomBtn.onClick = [this] { ctl.randomize(); };
    seedBtn.onClick = [this] { showRandomizePopover(); };
    undoBtn.onClick = [this] { ctl.undo(); };
    redoBtn.onClick = [this] { ctl.redo(); };
    abA.onClick = [this] { ctl.selectAb (AbCompare::Side::A); };
    abB.onClick = [this] { ctl.selectAb (AbCompare::Side::B); };
    copyBtn.onClick = [this] { ctl.copyAToB(); };
    saveBtn.onClick = [this] { ctl.savePreset(); };
    exportBtn.onClick = [this] { if (onExportWav) onExportWav(); };
    moreBtn.onClick = [this] { showMoreMenu(); };
    bypassBtn.setClickingTogglesState (true);
    bypassAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, "bypass", bypassBtn);
    registry.add ("bypass", &bypassBtn, true);
    actionOrder = { &panicBtn, &resetBtn, &randomBtn, &seedBtn, &bypassBtn, &undoBtn, &redoBtn, &abA, &abB, &copyBtn, &saveBtn, &exportBtn, &moreBtn };

    // ---- macros ----
    for (int i = 0; i < 5; ++i)
    {
        const auto* d = findParamDef (macroIds[i]);
        auto k = std::make_unique<MacroKnob> (toJuce (d->name), MacroKnob::Size::Large);
        k->bind (apvts, macroIds[i]);
        k->setTitle (toJuce (d->name) + " macro");
        ctl.installHooks (macroIds[i], k->hooks);
        registry.add (macroIds[i], k.get(), true);
        addAndMakeVisible (*k);
        knobs[static_cast<std::size_t> (i)] = std::move (k);
    }

    // ---- signal ----
    scope.setSource (&sources);
    meter.setSource (&sources);
    cpu.setSource (&sources);
    addAndMakeVisible (scope);
    addAndMakeVisible (meter);
    addAndMakeVisible (cpu);
    meter.setTitle ("Output level");
    cpu.setTitle ("Snaral DSP CPU");

    // ---- performance row (secondary duplicates; the Advanced tabs own the primary controls) ----
    voiceMode = std::make_unique<ParamCombo> (apvts, "voice_mode");
    lfoMode = std::make_unique<ParamCombo> (apvts, "lfo_mode");
    lfoDivision = std::make_unique<ParamCombo> (apvts, "lfo_sync_division");
    oversampling = std::make_unique<ParamCombo> (apvts, "oversampling_mode");
    legato = std::make_unique<ParamToggle> (apvts, "legato");
    glide = std::make_unique<ParamSlider> (apvts, "glide_ms", 56);
    lfoRate = std::make_unique<ParamSlider> (apvts, "lfo_rate_hz", 68);
    trim = std::make_unique<ParamSlider> (apvts, "output_trim_db", 56);
    ceiling = std::make_unique<ParamSlider> (apvts, "limiter_ceiling_db", 56);
    registerSecondary ("voice_mode", *voiceMode);
    registerSecondary ("lfo_mode", *lfoMode);
    registerSecondary ("lfo_sync_division", *lfoDivision);
    registerSecondary ("oversampling_mode", *oversampling);
    registerSecondary ("legato", *legato);
    registerSecondary ("glide_ms", *glide);
    registerSecondary ("lfo_rate_hz", *lfoRate);
    registerSecondary ("output_trim_db", *trim);
    registerSecondary ("limiter_ceiling_db", *ceiling);
    for (juce::Component* c : { static_cast<juce::Component*> (voiceMode.get()), static_cast<juce::Component*> (lfoMode.get()),
                                static_cast<juce::Component*> (lfoDivision.get()), static_cast<juce::Component*> (oversampling.get()),
                                static_cast<juce::Component*> (legato.get()), static_cast<juce::Component*> (glide.get()),
                                static_cast<juce::Component*> (lfoRate.get()), static_cast<juce::Component*> (trim.get()),
                                static_cast<juce::Component*> (ceiling.get()) })
        addAndMakeVisible (*c);
    lfoMode->getComboBox().onChange = [this] { resized(); repaint(); };

    qualityLabel.setFont (fonts::value().withHeight (12.0f));
    qualityLabel.setJustificationType (juce::Justification::centredRight);
    qualityLabel.setTitle ("Quality");
    addAndMakeVisible (qualityLabel);

    for (int i = 0; i < 4; ++i)
    {
        auto b = std::make_unique<juce::TextButton>();
        b->setWantsKeyboardFocus (true);
        b->onClick = [this, i] { ctl.isAuditioning() ? void() : ctl.recallVariation (i); };
        b->addMouseListener (this, false);
        addAndMakeVisible (*b);
        varBtn[static_cast<std::size_t> (i)] = std::move (b);
    }

    ctl.addListener (this);
    refreshFromController();
    tick();
}

MainView::~MainView()
{
    ctl.removeListener (this);
    seedBtn.setLookAndFeel (nullptr);
}

void MainView::setSignalVisibility (bool scopeVisible, bool cpuVisible)
{
    scope.setVisible (scopeVisible);
    scope.setDisplayEnabled (scopeVisible);
    cpu.setVisible (cpuVisible);
    cpu.setDisplayEnabled (cpuVisible);
    resized();
}

void MainView::controllerStateChanged() { refreshFromController(); }

void MainView::refreshLearnStates()
{
    for (const auto& e : registry.all())
        if (auto* k = dynamic_cast<MacroKnob*> (e.component.getComponent()))
        {
            if (ctl.learnArmed() && findParameterIndex (e.id.toStdString()) == ctl.learnTarget())
                k->setLearnState (MacroKnob::LearnState::Armed);
            else if (ctl.isLearned (e.id))
                k->setLearnState (MacroKnob::LearnState::Learned, ctl.learnDetail (e.id));
            else
                k->setLearnState (MacroKnob::LearnState::None);
        }
}

void MainView::refreshFromController()
{
    undoBtn.setEnabled (ctl.canUndo());
    redoBtn.setEnabled (ctl.canRedo());
    undoBtn.setTooltip (ctl.canUndo() ? "Undo " + ctl.undoLabel() + " (Ctrl or Cmd + Z)" : juce::String ("Nothing to undo"));
    redoBtn.setTooltip (ctl.canRedo() ? "Redo " + ctl.redoLabel() + " (Shift + Ctrl or Cmd + Z)" : juce::String ("Nothing to redo"));
    const bool engaged = ctl.abEngaged();
    const bool onA = ! engaged || ctl.abSide() == AbCompare::Side::A;
    abA.setToggleState (engaged && onA, juce::dontSendNotification);
    abB.setToggleState (engaged && ! onA, juce::dontSendNotification);
    abA.setTitle (engaged && onA ? "Compare A, active" : "Compare A");
    abB.setTitle (engaged && ! onA ? "Compare B, active" : "Compare B");
    copyBtn.setButtonText (onA ? "A > B" : "B > A");
    copyBtn.setTitle (onA ? "Copy A to B" : "Copy B to A");
    seedBtn.setButtonText (ctl.hasRandomized() ? ctl.seedHex() : juce::String ("seed"));
    seedBtn.setTooltip ("Seed of the last Randomize: " + ctl.seedHex() + ". Click to reproduce it or lock groups.");

    for (int i = 0; i < 4; ++i)
    {
        auto& b = *varBtn[static_cast<std::size_t> (i)];
        const auto name = ctl.variationLabel (i);
        const bool isDefault = name == juce::String::charToString (static_cast<juce::juce_wchar> ('A' + i));
        juce::String text = isDefault ? "Var " + name : name;
        const bool empty = ctl.variationEmpty (i);
        if (empty)
            text << " (empty)";
        b.setButtonText (text);
        b.setToggleState (! empty && ctl.activeVariation() == i, juce::dontSendNotification);
        b.setTitle ("Variation " + name + (empty ? ", empty" : (ctl.activeVariation() == i ? ", active" : "")));
        b.setTooltip (empty ? "Variation " + name + " is empty. Click to store the current sound. Right-click for more."
                            : "Recall variation " + name + ". Right-click to store, rename or clear.");
        b.onClick = [this, i, empty]
        {
            if (empty)
                ctl.storeVariation (i);
            else
                ctl.recallVariation (i);
        };
    }
    refreshLearnStates();
    resized();
    repaint();
}

void MainView::tick()
{
    const auto os = ctl.processor().getParameters().getRawParameterValue ("oversampling_mode")->load();
    const int latency = ctl.latencySamples();
    qualityLabel.setText ("OS " + juce::String (os < 0.5f ? "off" : (os < 1.5f ? "2x" : "4x")) + ", " + juce::String (latency) + " smp",
                          juce::dontSendNotification);
    qualityLabel.setTooltip (ctl.latencyText());
    qualityLabel.setDescription (ctl.latencyText());

    auto raw = [this] (const char* id) { return ctl.processor().getParameters().getRawParameterValue (id)->load(); };
    juce::String chip;
    const int solo = static_cast<int> (raw ("layer_solo") + 0.5f);
    if (solo == 1) chip = "SOLO SUB";
    else if (solo == 2) chip = "SOLO MID";
    if (raw ("sub_mute") >= 0.5f) chip << (chip.isEmpty() ? "" : "  ") << "SUB MUTED";
    if (raw ("mid_mute") >= 0.5f) chip << (chip.isEmpty() ? "" : "  ") << "MID MUTED";
    if (chip != layerChipText)
    {
        layerChipText = chip;
        resized();
        repaint();
    }
}

// ---- menus and popovers ----------------------------------------------------------------------------------------
void MainView::showMoreMenu()
{
    juce::PopupMenu m;
    m.addItem (1, "Reset to initialized patch", ! compactActions ? true : true);
    m.addItem (2, "Randomize");
    m.addItem (3, "Randomize seed and locks...");
    m.addSeparator();
    m.addItem (4, "Save");
    m.addItem (5, "Save as...");
    m.addItem (6, "Import preset...");
    m.addItem (7, "Export preset...");
    m.addItem (8, "Export WAV...");
    m.addSeparator();
    m.addItem (9, "Copy A to B", ctl.abEngaged());
    m.addItem (10, "Copy B to A", ctl.abEngaged());
    m.addItem (11, "Rescan preset library");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&moreBtn), [this] (int r)
    {
        switch (r)
        {
            case 1: ctl.resetPatch(); break;
            case 2: ctl.randomize(); break;
            case 3: showRandomizePopover(); break;
            case 4: ctl.savePreset(); break;
            case 5: if (onSaveAs) onSaveAs(); break;
            case 6: if (onImportPreset) onImportPreset(); break;
            case 7: if (onExportPreset) onExportPreset(); break;
            case 8: if (onExportWav) onExportWav(); break;
            case 9: ctl.copyAToB(); break;
            case 10: ctl.copyBToA(); break;
            case 11: if (onRescan) onRescan(); break;
            default: break;
        }
    });
}

void MainView::showRandomizePopover()
{
    auto* target = seedBtn.isVisible() ? &seedBtn : (randomBtn.isVisible() ? &randomBtn : &moreBtn);
    juce::CallOutBox::launchAsynchronously (std::make_unique<RandomizePopover> (ctl), target->getScreenBounds(), getTopLevelComponent());
}

void MainView::showVariationMenu (int slot)
{
    juce::PopupMenu m;
    m.addItem (1, "Store current sound here");
    m.addItem (2, "Recall", ! ctl.variationEmpty (slot));
    m.addItem (3, "Rename...");
    m.addItem (4, "Clear", ! ctl.variationEmpty (slot));
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (varBtn[static_cast<std::size_t> (slot)].get()), [this, slot] (int r)
    {
        switch (r)
        {
            case 1: ctl.storeVariation (slot); break;
            case 2: ctl.recallVariation (slot); break;
            case 3: ctl.renameVariation (slot); break;
            case 4: ctl.clearVariation (slot); break;
            default: break;
        }
    });
}

void MainView::mouseDown (const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu())
        return;
    for (int i = 0; i < 4; ++i)
        if (e.eventComponent == varBtn[static_cast<std::size_t> (i)].get())
            showVariationMenu (i);
}

// ---- layout ----------------------------------------------------------------------------------------------------
void MainView::layoutActionRow (juce::Rectangle<int> row)
{
    // visible set, in priority order of removal: Copy, Export WAV, Save, seed, Randomize, Reset
    struct Item { juce::Component* c; int w; bool optional; };
    const auto widthOf = [] (juce::TextButton& b) { return buttonWidth (b, 48); };
    std::vector<Item> items = {
        { &panicBtn, juce::jmax (72, widthOf (panicBtn)), false },
        { &resetBtn, widthOf (resetBtn), true },
        { &randomBtn, widthOf (randomBtn), true },
        { &seedBtn, 96, true },
        { &bypassBtn, widthOf (bypassBtn), false },
        { &undoBtn, widthOf (undoBtn), false },
        { &redoBtn, widthOf (redoBtn), false },
        { &abA, 40, false },
        { &abB, 40, false },
        { &copyBtn, 60, true },
        { &saveBtn, widthOf (saveBtn), true },
        { &exportBtn, widthOf (exportBtn), true },
    };
    const int gap = 6;
    auto total = [&] (bool includeMore)
    {
        int t = 0;
        int n = 0;
        for (const auto& i : items)
            if (i.c != nullptr) { t += i.w; ++n; }
        if (includeMore) { t += widthOf (moreBtn); ++n; }
        return t + gap * juce::jmax (0, n - 1);
    };
    // drop optional items until everything fits (the More menu carries them)
    const juce::Component* dropOrder[] = { &copyBtn, &exportBtn, &saveBtn, &seedBtn, &randomBtn, &resetBtn };
    bool dropped = false;
    std::size_t next = 0;
    while (total (dropped) > row.getWidth() && next < std::size(dropOrder))
    {
        for (auto& i : items)
            if (i.c == dropOrder[next])
                i.c = nullptr;
        dropped = true;
        ++next;
    }
    compactActions = dropped;
    for (auto& i : items)
        if (i.c != nullptr)
        {
            i.c->setVisible (true);
            i.c->setBounds (row.removeFromLeft (i.w).withHeight (hit).withY (row.getY()));
            row.removeFromLeft (gap);
        }
    for (auto* c : actionOrder)
    {
        bool placed = false;
        for (const auto& i : items) placed = placed || i.c == c;
        if (c != &moreBtn && ! placed)
            c->setVisible (false);
    }
    moreBtn.setVisible (dropped);
    if (dropped)
        moreBtn.setBounds (row.removeFromLeft (widthOf (moreBtn)).withHeight (hit));
}

void MainView::resized()
{
    auto r = getLocalBounds().reduced (12, 6);
    const bool wide = getWidth() >= 860;

    layoutActionRow (r.removeFromTop (hit));
    r.removeFromTop (rowGap);

    // ---- variation row (bottom) ----
    {
        auto row = r.removeFromBottom (hit);
        r.removeFromBottom (rowGap);
        panic2Btn.setBounds (row.removeFromRight (84));
        row.removeFromRight (rowGap);
        int chipW = layerChipText.isEmpty() ? 0 : static_cast<int> (textWidth (fonts::label(), layerChipText)) + 34;
        int qualityW = 112;
        int varW[4];
        int varTotal = 0;
        for (int i = 0; i < 4; ++i)
        {
            varW[i] = juce::jmax (84, buttonWidth (*varBtn[static_cast<std::size_t> (i)], 84));
            varTotal += varW[i] + 6;
        }
        if (varTotal + qualityW + chipW + 20 > row.getWidth())
            qualityW = 0;
        qualityLabel.setVisible (qualityW > 0);
        if (qualityW > 0)
            qualityLabel.setBounds (row.removeFromRight (qualityW));
        layerChipBounds = {};
        if (chipW > 0 && varTotal + chipW + 8 <= row.getWidth())
        {
            row.removeFromRight (rowGap);
            layerChipBounds = row.removeFromRight (chipW).withSizeKeepingCentre (chipW, 26);
        }
        for (int i = 0; i < 4; ++i)
        {
            varBtn[static_cast<std::size_t> (i)]->setBounds (row.removeFromLeft (varW[i]));
            row.removeFromLeft (6);
        }
    }

    // ---- performance flow ----
    {
        const bool sync = ctl.processor().getParameters().getRawParameterValue ("lfo_mode")->load() >= 0.5f;
        lfoRate->setVisible (! sync);
        lfoDivision->setVisible (sync);
        struct Cell { juce::Component* c; int w, h; };
        std::vector<Cell> cells = {
            { voiceMode.get(), voiceMode->getPreferredWidth(), ParamCombo::preferredHeight },
            { legato.get(), legato->getPreferredWidth() + 8, metrics::buttonHeight },
            { glide.get(), 200, ParamSlider::preferredHeight },
            { lfoMode.get(), lfoMode->getPreferredWidth(), ParamCombo::preferredHeight },
            { sync ? static_cast<juce::Component*> (lfoDivision.get()) : static_cast<juce::Component*> (lfoRate.get()),
              sync ? lfoDivision->getPreferredWidth() : 200, sync ? ParamCombo::preferredHeight : ParamSlider::preferredHeight },
            { oversampling.get(), oversampling->getPreferredWidth(), ParamCombo::preferredHeight },
        };
        const int gap = 12;
        std::vector<std::vector<Cell>> rows(1);
        int x = 0;
        for (const auto& c : cells)
        {
            if (x > 0 && x + c.w > r.getWidth())
            {
                rows.emplace_back();
                x = 0;
            }
            rows.back().push_back (c);
            x += c.w + gap;
        }
        int totalH = 0;
        for (const auto& rw : rows)
        {
            int h = 0;
            for (const auto& c : rw) h = juce::jmax (h, c.h);
            totalH += h + 6;
        }
        totalH -= 6;
        auto area = r.removeFromBottom (totalH);
        r.removeFromBottom (rowGap);
        for (const auto& rw : rows)
        {
            int h = 0;
            for (const auto& c : rw) h = juce::jmax (h, c.h);
            auto line = area.removeFromTop (h);
            area.removeFromTop (6);
            for (const auto& c : rw)
            {
                auto cell = line.removeFromLeft (c.w);
                line.removeFromLeft (gap);
                c.c->setBounds (cell.withSizeKeepingCentre (c.w, c.h));
            }
        }
        // the hidden alternative keeps a sane size
        (sync ? static_cast<juce::Component*> (lfoRate.get()) : static_cast<juce::Component*> (lfoDivision.get()))->setBounds (0, 0, 10, 10);
    }

    // ---- macros + signal block ----
    const auto size = wide ? MacroKnob::Size::Large : MacroKnob::Size::Normal;
    for (auto& k : knobs)
        if (k->getSize() != size)
            k->setKnobSize (size);
    const int cellW = knobs[0]->getPreferredSize().x + (wide ? 6 : 0);
    auto macroArea = r.removeFromLeft (cellW * 5);
    for (auto& k : knobs)
    {
        const auto pref = k->getPreferredSize();
        auto cell = macroArea.removeFromLeft (cellW);
        k->setBounds (cell.withSizeKeepingCentre (pref.x, pref.y));
    }
    r.removeFromLeft (14);
    signalBlock = r;
    auto sig = r;
    // output controls under the scope
    const int outputH = ParamSlider::preferredHeight * 2 + 6;
    auto outputs = sig.removeFromBottom (outputH);
    sig.removeFromBottom (8);
    trim->setBounds (outputs.removeFromTop (ParamSlider::preferredHeight));
    outputs.removeFromTop (6);
    ceiling->setBounds (outputs);
    auto strip = sig.removeFromRight (cpu.isVisible() || true ? 60 : 0);
    sig.removeFromRight (8);
    if (cpu.isVisible())
    {
        cpu.setBounds (strip.removeFromTop (36));
        strip.removeFromTop (6);
    }
    meter.setBounds (strip);
    scope.setBounds (sig);
}

void MainView::paint (juce::Graphics& g)
{
    if (layerChipBounds.isEmpty())
        return;
    const auto& t = SnaralLookAndFeel::tokensFor (*this);
    const auto b = layerChipBounds.toFloat();
    g.setColour (t.accentTint (t.panelRaised));
    g.fillRoundedRectangle (b, 4.0f);
    g.setColour (t.warning);
    g.drawRoundedRectangle (b.reduced (0.5f), 4.0f, 1.5f);
    SnaralLookAndFeel::drawSeverityIcon (g, Severity::Warning, juce::Rectangle<float> (b.getX() + 6, b.getCentreY() - 7, 14, 14), t.warning);
    g.setColour (t.textPrimary);
    g.setFont (fonts::label());
    g.drawText (layerChipText, b.withTrimmedLeft (24).toNearestInt(), juce::Justification::centredLeft, false);
}
} // namespace snaral::ui
