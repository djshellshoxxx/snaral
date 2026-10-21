#include "AdvancedView.h"

#include "Dsp/Trajectory.h"

namespace snaral::ui
{
namespace
{
constexpr int headerH = 40;

/// Trajectory editor inside a section body; follows the width, keeps at least the 360 x 160 canvas of the spec.
class TrajectoryPanel : public juce::Component, public PreferredHeightProvider
{
public:
    explicit TrajectoryPanel (std::unique_ptr<TrajectoryEditor> e) : editor (std::move (e))
    {
        addAndMakeVisible (*editor);
    }
    int getPreferredHeight (int width) const override
    {
        return juce::jlimit (TrajectoryEditor::readoutHeight + 170, 300, static_cast<int> (width * 0.3f) + TrajectoryEditor::readoutHeight);
    }
    void resized() override { editor->setBounds (getLocalBounds()); }
    TrajectoryEditor* get() noexcept { return editor.get(); }

private:
    std::unique_ptr<TrajectoryEditor> editor;
};
} // namespace

class AdvancedView::Diagnostics : public juce::Component, public PreferredHeightProvider
{
public:
    explicit Diagnostics (EditorController& c) : ctl (c)
    {
        for (auto* l : { &voices, &limiter, &events })
        {
            l->setFont (fonts::value().withHeight (12.5f));
            l->setMinimumHorizontalScale (0.9f);
            addAndMakeVisible (*l);
        }
        voices.setTitle ("Active voices");
        limiter.setTitle ("Limiter gain reduction");
        events.setTitle ("Dropped events");
        update();
    }

    int getPreferredHeight (int) const override { return 3 * 22 + 4; }

    void update()
    {
        const auto& d = ctl.processor().diagnostics();
        voices.setText ("Active voices: " + juce::String (d.activeVoices()), juce::dontSendNotification);
        limiter.setText ("Limiter gain reduction: " + juce::String (d.limiterReductionDb(), 1) + " dB", juce::dontSendNotification);
        events.setText ("Dropped MIDI events: " + juce::String (static_cast<int> (d.droppedMidiEvents())) + "   Dropped commands: "
                            + juce::String (static_cast<int> (d.droppedCommands())),
                        juce::dontSendNotification);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        for (auto* l : { &voices, &limiter, &events })
            l->setBounds (r.removeFromTop (22));
    }

private:
    EditorController& ctl;
    juce::Label voices, limiter, events;
};

AdvancedView::AdvancedView (EditorController& controller, ControlRegistry& reg)
    : ctl (controller), registry (reg)
{
    setTitle ("Advanced view");
    setFocusContainerType (FocusContainerType::focusContainer);
    auto& apvts = ctl.apvts();

    for (auto* b : { &soloSub, &soloMid, &muteSub, &muteMid })
    {
        b->setClickingTogglesState (false);
        b->setWantsKeyboardFocus (true);
        addAndMakeVisible (*b);
    }
    soloSub.setTooltip ("Hear only the Sub layer");
    soloMid.setTooltip ("Hear only the Mid layer");
    muteSub.setTooltip ("Silence the Sub layer");
    muteMid.setTooltip ("Silence the Mid layer");
    soloSub.onClick = [this] { setSolo (soloSub.getToggleState() ? 0 : 1); };
    soloMid.onClick = [this] { setSolo (soloMid.getToggleState() ? 0 : 2); };
    muteSub.setClickingTogglesState (true);
    muteMid.setClickingTogglesState (true);
    muteSubAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, "sub_mute", muteSub);
    muteMidAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, "mid_mute", muteMid);
    soloAttach = std::make_unique<juce::ParameterAttachment> (*apvts.getParameter ("layer_solo"),
                                                              [this] (float v) { updateSoloButtons (v); }, nullptr);
    soloAttach->sendInitialUpdate();
    registry.add ("layer_solo", &soloSub, true);
    registry.add ("sub_mute", &muteSub, true);
    registry.add ("mid_mute", &muteMid, true);

    addAndMakeVisible (tabbed);
    tabbed.buildDefaultTabs (apvts, [this] (const juce::String& id, ContextHooks& hooks, juce::Component& c)
    {
        registry.add (id, &c, true);
        ctl.installHooks (id, hooks);
    });

    // Trajectory editor in Filter & Motion
    if (auto* fm = dynamic_cast<AdvancedTabContent*> (tabbed.getTabContent (AdvancedTab::FilterMotion)))
    {
        auto binding = std::make_unique<ApvtsTrajectoryBinding> (apvts);
        auto editor = std::make_unique<TrajectoryEditor> (std::move (binding));
        editor->setCurveEvaluator ([] (const std::array<float, 5>& x, const std::array<float, 5>& y, float curve01, float t01)
        {
            TrajectoryCurve curve;
            curve.set (x, y, curve01);   // the exact function the engine plays back (Dsp/Trajectory.h)
            return curve.eval (t01);
        });
        trajectory = editor.get();
        auto panel = std::make_unique<TrajectoryPanel> (std::move (editor));
        const int at = [&] {
            for (int i = 0; i < fm->getNumSections(); ++i)
                if (fm->getSection (i).getSectionTitle() == "Snarl Trajectory")
                    return i + 1;
            return -1;
        }();
        fm->addCustomSection ("Trajectory editor", std::move (panel), at);
        for (auto id : { "traj_x2", "traj_x3", "traj_x4", "traj_y1", "traj_y2", "traj_y3", "traj_y4", "traj_y5" })
        {
            registry.add (id, trajectory, true);
            
        }
    }

    // Effects captions: signal order and "mid only, before Drive"
    if (auto* fx = dynamic_cast<AdvancedTabContent*> (tabbed.getTabContent (AdvancedTab::Effects)))
    {
        if (auto* s = fx->findSection ("Formant Furnace"))
            s->setTitle ("Formant Furnace  (mid only, before Drive)");
        if (auto* s = fx->findSection ("Harmonic Fracture"))
            s->setTitle ("Harmonic Fracture  (mid only, before Drive)");
    }

    // Output: live latency readout inside Quality, diagnostics below
    if (auto* out = dynamic_cast<AdvancedTabContent*> (tabbed.getTabContent (AdvancedTab::Output)))
    {
        latencyLabel.setFont (fonts::value());
        latencyLabel.setJustificationType (juce::Justification::centredLeft);
        latencyLabel.setTitle ("Latency");
        latencyLabel.setSize (300, metrics::buttonHeight);
        if (auto* q = out->findSection ("Quality"))
            if (auto* grid = dynamic_cast<ParamGrid*> (q->getContent()))
                grid->addItem (latencyLabel);
        auto d = std::make_unique<Diagnostics> (ctl);
        diagnostics = d.get();
        out->addCustomSection ("Engine diagnostics", std::move (d));
    }

    // collapsed groups are a user preference
    for (int t = 0; t < numAdvancedTabs; ++t)
        if (auto* content = dynamic_cast<AdvancedTabContent*> (tabbed.getTabContent (static_cast<AdvancedTab> (t))))
            for (int i = 0; i < content->getNumSections(); ++i)
            {
                auto& section = content->getSection (i);
                juce::String title = section.getSectionTitle();
                if (const auto at = title.indexOf ("  ("); at > 0)
                    title = title.substring (0, at);
                const auto key = juce::String (advancedTabName (static_cast<AdvancedTab> (t))) + "/" + title;
                if (ctl.prefs().isCollapsed (key))
                    section.setExpanded (false, false);
                auto previous = section.onLayoutChanged;
                auto* sectionPtr = &section;
                section.onLayoutChanged = [this, key, sectionPtr, previous]
                {
                    ctl.prefs().setCollapsed (key, ! sectionPtr->isExpanded());
                    if (previous)
                        previous();
                };
            }
    tabbed.refreshLayout();
    tick();
}

AdvancedView::~AdvancedView() = default;

void AdvancedView::setSolo (int mode)
{
    if (auto* p = ctl.apvts().getParameter ("layer_solo"))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (static_cast<float> (mode)));
        p->endChangeGesture();
    }
}

void AdvancedView::updateSoloButtons (float rawChoice)
{
    const int m = static_cast<int> (rawChoice + 0.5f);
    soloSub.setToggleState (m == 1, juce::dontSendNotification);
    soloMid.setToggleState (m == 2, juce::dontSendNotification);
}

void AdvancedView::refreshLearnStates()
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

void AdvancedView::tick()
{
    latencyLabel.setText (ctl.latencyText(), juce::dontSendNotification);
    latencyLabel.setDescription (ctl.latencyText());
    if (diagnostics != nullptr)
        diagnostics->update();

    auto raw = [this] (const char* id) { return ctl.apvts().getRawParameterValue (id)->load(); };
    juce::String chip;
    const int solo = static_cast<int> (raw ("layer_solo") + 0.5f);
    if (solo == 1) chip = "SOLO SUB";
    else if (solo == 2) chip = "SOLO MID";
    if (raw ("sub_mute") >= 0.5f) chip << (chip.isEmpty() ? "" : "  ") << "SUB MUTED";
    if (raw ("mid_mute") >= 0.5f) chip << (chip.isEmpty() ? "" : "  ") << "MID MUTED";
    if (chip != chipText)
    {
        chipText = chip;
        resized();
        repaint();
    }
}

void AdvancedView::paint (juce::Graphics& g)
{
    const auto& t = SnaralLookAndFeel::tokensFor (*this);
    g.setColour (t.textMuted);
    g.setFont (fonts::label());
    g.drawText ("LAYERS", 12, 0, 60, headerH, juce::Justification::centredLeft, false);
    if (chipText.isNotEmpty() && ! chipBounds.isEmpty())
    {
        const auto b = chipBounds.toFloat();
        g.setColour (t.accentTint (t.panelRaised));
        g.fillRoundedRectangle (b, 4.0f);
        g.setColour (t.warning);
        g.drawRoundedRectangle (b.reduced (0.5f), 4.0f, 1.5f);
        SnaralLookAndFeel::drawSeverityIcon (g, Severity::Warning, juce::Rectangle<float> (b.getX() + 6, b.getCentreY() - 7, 14, 14), t.warning);
        g.setColour (t.textPrimary);
        g.setFont (fonts::label());
        g.drawText (chipText, b.withTrimmedLeft (24).toNearestInt(), juce::Justification::centredLeft, false);
    }
}

void AdvancedView::resized()
{
    auto r = getLocalBounds();
    auto head = r.removeFromTop (headerH).reduced (12, 4);
    head.removeFromLeft (64);
    for (auto* b : { &soloSub, &soloMid, &muteSub, &muteMid })
    {
        const int w = juce::jmax (84, static_cast<int> (textWidth (fonts::body(), b->getButtonText())) + 28);
        b->setBounds (head.removeFromLeft (w).withHeight (metrics::minHitTarget).withY (head.getY()));
        head.removeFromLeft (6);
    }
    head.removeFromLeft (8);
    const int chipW = chipText.isEmpty() ? 0 : static_cast<int> (textWidth (fonts::label(), chipText)) + 34;
    chipBounds = chipW > 0 && head.getWidth() >= chipW ? head.removeFromLeft (chipW).withSizeKeepingCentre (chipW, 26) : juce::Rectangle<int>();
    tabbed.setBounds (r.reduced (12, 0).withTrimmedBottom (6));
}
} // namespace snaral::ui
