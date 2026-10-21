#include "AdvancedTabs.h"

#include <algorithm>
#include <set>

namespace snaral::ui
{
const char* advancedTabName (AdvancedTab t)
{
    switch (t)
    {
        case AdvancedTab::Voice:        return "Voice";
        case AdvancedTab::Sub:          return "Sub";
        case AdvancedTab::Sources:      return "Sources";
        case AdvancedTab::FilterMotion: return "Filter & Motion";
        case AdvancedTab::Drive:        return "Drive";
        case AdvancedTab::Texture:      return "Texture";
        case AdvancedTab::Effects:      return "Effects";
        case AdvancedTab::Output:       return "Output";
    }
    return "";
}

const std::vector<AdvancedSectionSpec>& advancedSections()
{
    using T = AdvancedTab;
    static const std::vector<AdvancedSectionSpec> specs = {
        { T::Voice, "Voice Mode",      { "voice_mode", "legato", "glide_ms" } },
        { T::Voice, "Pitch & Tuning",  { "pitch_bend_range", "master_tune_hz", "transpose_semitones" } },
        { T::Voice, "Velocity",        { "velocity_to_level", "velocity_to_attack" } },

        { T::Sub, "Sub Oscillator",    { "sub_waveform", "sub_tune_cents", "sub_level" } },
        { T::Sub, "Sub Envelope",      { "sub_amp_attack_ms", "sub_amp_decay_ms", "sub_amp_sustain", "sub_amp_release_ms" } },
        { T::Sub, "Sub Anchor",        { "anchor_enabled", "anchor_note", "anchor_strength" } },

        { T::Sources, "Mid Sources",   { "mid_source_a", "mid_source_b", "mid_blend", "mid_table_position", "mid_pulse_width",
                                         "mid_tune_cents", "mid_level" } },
        { T::Sources, "Mid Envelope",  { "mid_amp_attack_ms", "mid_amp_decay_ms", "mid_amp_sustain", "mid_amp_release_ms" } },

        { T::FilterMotion, "Filter",   { "filter_type", "filter_cutoff_hz", "filter_resonance" } },
        { T::FilterMotion, "Contour",  { "contour_attack_ms", "contour_decay_ms", "contour_sustain", "contour_release_ms",
                                         "contour_shape", "contour_depth" } },
        { T::FilterMotion, "LFO",      { "lfo_mode", "lfo_rate_hz", "lfo_sync_division", "lfo_waveform", "lfo_retrigger",
                                         "lfo_phase", "lfo_depth" } },
        { T::FilterMotion, "Snarl Trajectory", { "traj_enabled", "traj_time_mode", "traj_length_ms", "traj_length_div", "traj_dest",
                                                 "traj_depth", "traj_curve", "traj_loop" } },

        { T::Drive, "Drive",           { "drive_type", "drive_amount", "drive_tone", "drive_mix" } },

        { T::Texture, "Noise",         { "noise_level", "noise_tone" } },
        { T::Texture, "Metal",         { "metal_level", "metal_decay_ms", "metal_tune" } },

        { T::Effects, "Formant Furnace",  { "furnace_enabled", "furnace_vowel", "furnace_spacing", "furnace_resonance",
                                            "furnace_env_amount", "furnace_lfo_amount", "furnace_mix" } },
        { T::Effects, "Harmonic Fracture", { "fracture_enabled", "fracture_crossover_hz", "fracture_drive_low", "fracture_drive_mid",
                                             "fracture_drive_high", "fracture_symmetry", "fracture_band_mix", "fracture_comp_db",
                                             "fracture_mix" } },

        { T::Output, "Output",         { "output_trim_db", "limiter_enabled", "limiter_ceiling_db" } },
        { T::Output, "Quality",        { "oversampling_mode" } },   // ui-layout.md: Quality lives in the Output tab
    };
    return specs;
}

std::vector<juce::String> advancedGroupsForTab (AdvancedTab t)
{
    switch (t)
    {
        case AdvancedTab::Voice:        return { "Voice" };
        case AdvancedTab::Sub:          return { "Sub" };
        case AdvancedTab::Sources:      return { "Mid" };
        case AdvancedTab::FilterMotion: return { "Filter", "Motion", "Trajectory" };
        case AdvancedTab::Drive:        return { "Drive" };
        case AdvancedTab::Texture:      return { "Texture" };
        case AdvancedTab::Effects:      return { "Furnace", "Fracture" };
        case AdvancedTab::Output:       return { "Output", "Quality" };
    }
    return {};
}

bool isEditedByDedicatedWidget (const juce::String& id)
{
    return id.matchesWildcard ("traj_x?", false) || id.matchesWildcard ("traj_y?", false);
}

bool isMainViewParameter (const juce::String& id)
{
    const auto* d = findParamDef (id);
    return d != nullptr && (toJuce (d->group) == "Macros" || toJuce (d->group) == "Utility");
}

//==================================================================================================================
// ParamGrid
//==================================================================================================================
void ParamGrid::addItem (juce::Component& c)
{
    items.push_back (&c);
    addAndMakeVisible (c);
}

void ParamGrid::addOwned (std::unique_ptr<juce::Component> c)
{
    addItem (*c);
    owned.push_back (std::move (c));
}

int ParamGrid::layout (int width, bool apply)
{
    int x = 0, y = 0, rowH = 0;
    std::vector<juce::Component*> row;
    auto flush = [&]
    {
        if (apply)
            for (auto* c : row)
                c->setTopLeftPosition (c->getX(), y + (rowH - c->getHeight()) / 2);
        y += rowH + spacing;
        rowH = 0;
        row.clear();
    };

    for (auto* c : items)
    {
        if (x > 0 && x + c->getWidth() > width)
        {
            flush();
            x = 0;
        }
        if (apply)
            c->setTopLeftPosition (x, 0);
        row.push_back (c);
        rowH = juce::jmax (rowH, c->getHeight());
        x += c->getWidth() + spacing;
    }
    if (! row.empty())
        flush();
    return juce::jmax (0, y - spacing);
}

int ParamGrid::getPreferredHeight (int width) const
{
    return const_cast<ParamGrid*> (this)->layout (width, false);
}

void ParamGrid::resized()
{
    layout (getWidth(), true);
}

//==================================================================================================================
// AdvancedTabContent
//==================================================================================================================
AdvancedTabContent::AdvancedTabContent (AdvancedTab t, juce::AudioProcessorValueTreeState& apvts, ControlVisitor visitor)
    : tab (t)
{
    std::set<juce::String> listed;
    auto addSection = [&] (const juce::String& title, const std::vector<juce::String>& ids)
    {
        if (ids.empty())
            return;
        auto grid = std::make_unique<ParamGrid>();
        for (const auto& id : ids)
        {
            const auto* d = findParamDef (id);
            if (d == nullptr)
                continue;
            std::unique_ptr<juce::Component> control;
            ContextHooks* hooks = nullptr;
            switch (d->type)
            {
                case ParamType::Float:
                {
                    auto k = std::make_unique<MacroKnob> (juce::String(), MacroKnob::Size::Small);
                    k->bind (apvts, id);
                    k->setValueAlwaysVisible (true);
                    hooks = &k->hooks;
                    control = std::move (k);
                    break;
                }
                case ParamType::Choice:
                {
                    auto c = std::make_unique<ParamCombo> (apvts, id);
                    hooks = &c->hooks;
                    control = std::move (c);
                    break;
                }
                case ParamType::Bool:
                {
                    auto b = std::make_unique<ParamToggle> (apvts, id);
                    hooks = &b->hooks;
                    control = std::move (b);
                    break;
                }
            }
            if (visitor && hooks != nullptr)
                visitor (id, *hooks, *control);
            controls.push_back ({ id, control.get() });
            grid->addOwned (std::move (control));
        }

        auto panel = std::make_unique<SectionPanel> (title, grid.get());
        panel->onLayoutChanged = [this] { relayout(); };
        addAndMakeVisible (*panel);
        sectionBodies.push_back (std::move (grid));
        sections.push_back (std::move (panel));
    };

    for (const auto& spec : advancedSections())
    {
        if (spec.tab != tab)
            continue;
        std::vector<juce::String> ids;
        for (const auto* id : spec.parameterIds)
        {
            ids.emplace_back (id);
            listed.insert (id);
        }
        addSection (spec.title, ids);
    }

    // Any parameter of this tab's groups that no section lists lands in "More" (never silently hidden).
    std::vector<juce::String> more;
    const auto groups = advancedGroupsForTab (tab);
    for (const auto& d : parameterTable)
    {
        const auto id = toJuce (d.id);
        if (std::find (groups.begin(), groups.end(), toJuce (d.group)) == groups.end())
            continue;
        if (listed.count (id) == 0 && ! isEditedByDedicatedWidget (id))
            more.push_back (id);
    }
    addSection ("More", more);
}

AdvancedTabContent::~AdvancedTabContent()
{
    // sections hold raw pointers to their bodies; destroy sections first
    sections.clear();
    sectionBodies.clear();
}

SectionPanel& AdvancedTabContent::addCustomSection (const juce::String& title, std::unique_ptr<juce::Component> body, int atIndex)
{
    auto panel = std::make_unique<SectionPanel> (title, body.get());
    panel->onLayoutChanged = [this] { relayout(); };
    addAndMakeVisible (*panel);
    auto* raw = panel.get();
    const auto idx = atIndex < 0 || atIndex > getNumSections() ? sections.size() : static_cast<std::size_t> (atIndex);
    sections.insert (sections.begin() + static_cast<std::ptrdiff_t> (idx), std::move (panel));
    sectionBodies.push_back (std::move (body));
    relayout();
    return *raw;
}

SectionPanel* AdvancedTabContent::findSection (const juce::String& title)
{
    for (auto& s : sections)
        if (s->getSectionTitle() == title)
            return s.get();
    return nullptr;
}

juce::Component* AdvancedTabContent::findControl (const juce::String& id) const
{
    for (const auto& e : controls)
        if (e.id == id)
            return e.control;
    return nullptr;
}

std::vector<juce::String> AdvancedTabContent::getParameterIds() const
{
    std::vector<juce::String> ids;
    for (const auto& e : controls)
        ids.push_back (e.id);
    return ids;
}

int AdvancedTabContent::getPreferredHeight (int width) const
{
    int h = 0;
    for (const auto& s : sections)
        h += s->getPreferredHeight (width) + gap;
    return juce::jmax (0, h - gap);
}

void AdvancedTabContent::resized()
{
    int y = 0;
    for (auto& s : sections)
    {
        const int h = s->getPreferredHeight (getWidth());
        s->setBounds (0, y, getWidth(), h);
        y += h + gap;
    }
}

void AdvancedTabContent::relayout()
{
    setSize (getWidth(), getPreferredHeight (getWidth()));
    resized();
    if (onPreferredHeightChanged)
        onPreferredHeightChanged();
}

//==================================================================================================================
// TabbedAdvanced
//==================================================================================================================
class TabbedAdvanced::TabButton : public juce::Button
{
public:
    TabButton (TabbedAdvanced& o, AdvancedTab t) : juce::Button (advancedTabName (t)), owner (o), tab (t)
    {
        setClickingTogglesState (false);
        setRadioGroupId (0x5a41);
        setWantsKeyboardFocus (true);
        setTitle (juce::String (advancedTabName (t)) + " tab");
        setTooltip (juce::String (advancedTabName (t)) + " controls");
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    int getIdealWidth() const
    {
        return static_cast<int> (std::ceil (textWidth (fonts::heading().withHeight (12.5f), getButtonText()))) + 2 * padding;
    }

    int padding = 14;

    void paintButton (juce::Graphics& g, bool highlighted, bool) override
    {
        const auto& t = SnaralLookAndFeel::tokensFor (*this);
        const bool sel = getToggleState();
        auto r = getLocalBounds().toFloat();
        if (sel)
        {
            g.setColour (t.panel);
            g.fillRoundedRectangle (r.withTrimmedBottom (-6.0f), 6.0f);
            g.setColour (t.accent);
            g.fillRect (r.removeFromBottom (3.0f).reduced (6.0f, 0.0f));          // underline: selected cue beyond colour
        }
        else if (highlighted)
        {
            g.setColour (t.panelRaised);
            g.fillRoundedRectangle (r.reduced (1.0f, 3.0f), 4.0f);
        }
        g.setFont (sel ? fonts::heading().withHeight (12.5f) : fonts::body().withHeight (12.5f));
        g.setColour (sel ? t.textPrimary : t.textMuted);
        g.drawFittedText (getButtonText(), getLocalBounds().reduced (4, 0), juce::Justification::centred, 1, 0.8f);
        if (hasKeyboardFocus (true))
            SnaralLookAndFeel::drawFocusRing (g, t, getLocalBounds().toFloat().reduced (2.0f, 3.0f), 4.0f);
    }

    void clicked() override { owner.setCurrentTab (tab, true); }

    bool keyPressed (const juce::KeyPress& k) override
    {
        int delta = 0;
        if (k.isKeyCode (juce::KeyPress::rightKey)) delta = 1;
        if (k.isKeyCode (juce::KeyPress::leftKey))  delta = -1;
        if (delta == 0 && ! k.isKeyCode (juce::KeyPress::homeKey) && ! k.isKeyCode (juce::KeyPress::endKey))
            return juce::Button::keyPressed (k);
        int idx = static_cast<int> (tab) + delta;
        if (k.isKeyCode (juce::KeyPress::homeKey)) idx = 0;
        if (k.isKeyCode (juce::KeyPress::endKey))  idx = numAdvancedTabs - 1;
        idx = juce::jlimit (0, numAdvancedTabs - 1, idx);
        owner.setCurrentTab (static_cast<AdvancedTab> (idx), true);
        owner.getTabButton (static_cast<AdvancedTab> (idx)).grabKeyboardFocus();
        return true;
    }

private:
    TabbedAdvanced& owner;
    AdvancedTab tab;
};

TabbedAdvanced::TabbedAdvanced()
{
    for (int i = 0; i < numAdvancedTabs; ++i)
    {
        const auto t = static_cast<AdvancedTab> (i);
        auto b = std::make_unique<TabButton> (*this, t);
        addAndMakeVisible (*b);
        tabButtons[static_cast<std::size_t> (i)] = std::move (b);

        auto vp = std::make_unique<juce::Viewport> (juce::String (advancedTabName (t)) + " controls");
        vp->setScrollBarsShown (true, false);
        vp->setScrollBarThickness (12);
        vp->setSingleStepSizes (24, 24);
        vp->setTitle (juce::String (advancedTabName (t)) + " controls, scrollable");
        vp->setVisible (false);
        addChildComponent (*vp);
        viewports[static_cast<std::size_t> (i)] = std::move (vp);
    }
    setCurrentTab (AdvancedTab::Voice, false);
    setWantsKeyboardFocus (false);
}

TabbedAdvanced::~TabbedAdvanced()
{
    for (auto& vp : viewports)
        if (vp != nullptr)
            vp->setViewedComponent (nullptr, false);
}

juce::Button& TabbedAdvanced::getTabButton (AdvancedTab t) noexcept { return *tabButtons[static_cast<std::size_t> (t)]; }

void TabbedAdvanced::setTabContent (AdvancedTab t, std::unique_ptr<juce::Component> content)
{
    const auto i = static_cast<std::size_t> (t);
    viewports[i]->setViewedComponent (nullptr, false);
    contents[i] = std::move (content);
    if (contents[i] != nullptr)
    {
        if (auto* atc = dynamic_cast<AdvancedTabContent*> (contents[i].get()))
            atc->onPreferredHeightChanged = [this, t] { layoutContent (t); };
        viewports[i]->setViewedComponent (contents[i].get(), false);
        layoutContent (t);
    }
}

juce::Component* TabbedAdvanced::getTabContent (AdvancedTab t) const
{
    return contents[static_cast<std::size_t> (t)].get();
}

void TabbedAdvanced::buildDefaultTabs (juce::AudioProcessorValueTreeState& apvts, AdvancedTabContent::ControlVisitor visitor)
{
    for (int i = 0; i < numAdvancedTabs; ++i)
        setTabContent (static_cast<AdvancedTab> (i), std::make_unique<AdvancedTabContent> (static_cast<AdvancedTab> (i), apvts, visitor));
}

void TabbedAdvanced::setCurrentTab (AdvancedTab t, bool notify)
{
    current = t;
    for (int i = 0; i < numAdvancedTabs; ++i)
    {
        tabButtons[static_cast<std::size_t> (i)]->setToggleState (i == static_cast<int> (t), juce::dontSendNotification);
        viewports[static_cast<std::size_t> (i)]->setVisible (i == static_cast<int> (t));
    }
    layoutContent (t);
    repaint();
    if (notify && onTabChanged)
        onTabChanged (t);
}

int TabbedAdvanced::getScrollPosition (AdvancedTab t) const
{
    return viewports[static_cast<std::size_t> (t)]->getViewPositionY();
}

void TabbedAdvanced::setScrollPosition (AdvancedTab t, int y)
{
    viewports[static_cast<std::size_t> (t)]->setViewPosition (0, y);
}

void TabbedAdvanced::scrollToBottom (AdvancedTab t)
{
    auto& vp = *viewports[static_cast<std::size_t> (t)];
    vp.setViewPosition (0, juce::jmax (0, vp.getViewedComponent() != nullptr ? vp.getViewedComponent()->getHeight() - vp.getHeight() : 0));
}

void TabbedAdvanced::layoutContent (AdvancedTab t)
{
    auto& vp = *viewports[static_cast<std::size_t> (t)];
    auto* c = contents[static_cast<std::size_t> (t)].get();
    if (c == nullptr)
        return;
    const int w = juce::jmax (1, vp.getWidth() - vp.getScrollBarThickness() - 2);
    int h = c->getHeight();
    if (auto* p = dynamic_cast<PreferredHeightProvider*> (c))
        h = p->getPreferredHeight (w);
    const auto pos = vp.getViewPosition();
    c->setSize (w, juce::jmax (1, h));
    vp.setViewPosition (pos);
}

void TabbedAdvanced::refreshLayout()
{
    for (int i = 0; i < numAdvancedTabs; ++i)
        layoutContent (static_cast<AdvancedTab> (i));
}

void TabbedAdvanced::resized()
{
    auto r = getLocalBounds();
    auto bar = r.removeFromTop (tabBarHeight);

    int total = 0;
    for (auto& b : tabButtons)
    {
        b->padding = 14;
        total += b->getIdealWidth();
    }
    int pad = 14;
    while (total > bar.getWidth() && pad > 4)
    {
        pad -= 2;
        total = 0;
        for (auto& b : tabButtons)
        {
            b->padding = pad;
            total += b->getIdealWidth();
        }
    }
    for (auto& b : tabButtons)
        b->setBounds (bar.removeFromLeft (juce::jmin (b->getIdealWidth(), bar.getWidth())));

    for (int i = 0; i < numAdvancedTabs; ++i)
    {
        viewports[static_cast<std::size_t> (i)]->setBounds (r);
        layoutContent (static_cast<AdvancedTab> (i));
    }
}

void TabbedAdvanced::paint (juce::Graphics& g)
{
    const auto& t = SnaralLookAndFeel::tokensFor (*this);
    g.setColour (t.border);
    g.drawHorizontalLine (tabBarHeight - 1, 0.0f, static_cast<float> (getWidth()));
}

void TabbedAdvanced::focusOfChildComponentChanged (FocusChangeType)
{
    ensureFocusedChildVisible();
}

void TabbedAdvanced::ensureFocusedChildVisible()
{
    auto* focused = juce::Component::getCurrentlyFocusedComponent();
    auto& vp = *viewports[static_cast<std::size_t> (current)];
    auto* content = vp.getViewedComponent();
    if (focused == nullptr || content == nullptr || ! content->isParentOf (focused))
        return;

    // Prefer the control's whole cell (a MacroKnob is its own cell; nested editors use their own bounds).
    const auto r = content->getLocalArea (focused, focused->getLocalBounds()).expanded (0, 6);
    const int top = vp.getViewPositionY(), bottom = top + vp.getViewHeight();
    if (r.getY() < top)
        vp.setViewPosition (0, juce::jmax (0, r.getY()));
    else if (r.getBottom() > bottom)
        vp.setViewPosition (0, r.getBottom() - vp.getViewHeight());
}
} // namespace snaral::ui
