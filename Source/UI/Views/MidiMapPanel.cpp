#include "MidiMapPanel.h"

#include "UI/ParamFormat.h"

namespace snaral::ui
{
class MidiMapPanel::Row : public juce::Component
{
public:
    Row (MidiMapPanel& o, int rowIndex) : owner (o), index (rowIndex)
    {
        target.setFont (fonts::body());
        addAndMakeVisible (target);
        for (auto* e : { &cc, &minEd, &maxEd })
        {
            e->setFont (fonts::value());
            e->setSelectAllWhenFocused (true);
            e->setInputRestrictions (e == &cc ? 3 : 7, e == &cc ? "0123456789" : "0123456789.");
            addAndMakeVisible (*e);
        }
        channel.addItem ("Omni", 1);
        for (int c = 1; c <= 16; ++c)
            channel.addItem ("Ch " + juce::String (c), c + 1);
        addAndMakeVisible (channel);
        invert.setButtonText ("Invert");
        addAndMakeVisible (invert);
        remove.setButtonText ("Remove");
        addAndMakeVisible (remove);

        cc.onReturnKey = cc.onFocusLost = [this] { commit(); };
        minEd.onReturnKey = minEd.onFocusLost = [this] { commit(); };
        maxEd.onReturnKey = maxEd.onFocusLost = [this] { commit(); };
        channel.onChange = [this] { commit(); };
        invert.onClick = [this] { commit(); };
        remove.onClick = [this]
        {
            const int t = current().target;
            juce::MessageManager::callAsync ([o = juce::Component::SafePointer<MidiMapPanel> (&owner), t]
            {
                if (o != nullptr) { o->ctl.removeMapping (t); o->refresh(); }
            });
        };
        load();
    }

    midi::MidiMapping current() const
    {
        const auto& all = owner.ctl.mappings();
        return index >= 0 && index < static_cast<int> (all.size()) ? all[static_cast<std::size_t> (index)] : midi::MidiMapping {};
    }

    void load()
    {
        const auto m = current();
        loading = true;
        const auto* d = m.target >= 0 ? &parameterTable[static_cast<std::size_t> (m.target)] : nullptr;
        target.setText (d != nullptr ? toJuce (d->name) : juce::String(), juce::dontSendNotification);
        cc.setText (juce::String (m.cc), false);
        minEd.setText (juce::String (m.min * 100.0f, 1), false);
        maxEd.setText (juce::String (m.max * 100.0f, 1), false);
        channel.setSelectedId (m.channel + 1, juce::dontSendNotification);
        invert.setToggleState (m.invert, juce::dontSendNotification);
        const auto n = target.getText();
        cc.setTitle (n + " CC number");
        minEd.setTitle (n + " minimum percent");
        maxEd.setTitle (n + " maximum percent");
        channel.setTitle (n + " MIDI channel");
        invert.setTitle (n + " invert");
        remove.setTitle ("Remove mapping for " + n);
        loading = false;
    }

    void commit()
    {
        if (loading)
            return;
        auto m = current();
        if (m.target < 0)
            return;
        m.cc = cc.getText().getIntValue();
        m.min = minEd.getText().getFloatValue() / 100.0f;
        m.max = maxEd.getText().getFloatValue() / 100.0f;
        m.channel = juce::jmax (0, channel.getSelectedId() - 1);
        m.invert = invert.getToggleState();
        if (m == current())
            return;
        if (! owner.ctl.updateMapping (m))
            load();   // rejected: show the stored values again (the status bar says why)
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (4, 4);
        remove.setBounds (r.removeFromRight (80));
        r.removeFromRight (6);
        invert.setBounds (r.removeFromRight (74));
        r.removeFromRight (6);
        maxEd.setBounds (r.removeFromRight (58));
        r.removeFromRight (6);
        minEd.setBounds (r.removeFromRight (58));
        r.removeFromRight (6);
        channel.setBounds (r.removeFromRight (84));
        r.removeFromRight (6);
        cc.setBounds (r.removeFromRight (52));
        r.removeFromRight (8);
        target.setBounds (r);
    }

    MidiMapPanel& owner;
    int index;
    bool loading = false;
    juce::Label target;
    juce::TextEditor cc, minEd, maxEd;
    juce::ComboBox channel;
    juce::ToggleButton invert;
    juce::TextButton remove;
};

class MidiMapPanel::Model : public juce::ListBoxModel
{
public:
    explicit Model (MidiMapPanel& o) : owner (o) {}
    int getNumRows() override { return static_cast<int> (owner.ctl.mappings().size()); }
    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool) override
    {
        if (row % 2 == 1)
        {
            g.setColour (SnaralLookAndFeel::tokensFor (owner).panelRaised.withAlpha (0.35f));
            g.fillRect (0, 0, w, h);
        }
    }
    juce::Component* refreshComponentForRow (int row, bool, juce::Component* existing) override
    {
        if (row >= getNumRows())
        {
            delete existing;
            return nullptr;
        }
        auto* r = dynamic_cast<Row*> (existing);
        if (r == nullptr)
        {
            delete existing;
            r = new Row (owner, row);
        }
        else
        {
            r->index = row;
            r->load();
        }
        return r;
    }
    juce::String getNameForRow (int row) override
    {
        const auto& all = owner.ctl.mappings();
        return row >= 0 && row < static_cast<int> (all.size()) ? juce::String (midi::describeMapping (all[static_cast<std::size_t> (row)])) : juce::String();
    }
    MidiMapPanel& owner;
};

MidiMapPanel::MidiMapPanel (EditorController& c) : ctl (c)
{
    setTitle ("MIDI mappings");
    model = std::make_unique<Model> (*this);
    list.setModel (model.get());
    list.setRowHeight (40);
    list.setTitle ("MIDI mapping list");
    list.setOutlineThickness (1);
    addAndMakeVisible (list);
    hint.setText ("No MIDI mappings yet. Right-click a knob or control and choose MIDI learn, then move a controller.", juce::dontSendNotification);
    hint.setFont (fonts::body());
    hint.setJustificationType (juce::Justification::centred);
    hint.setInterceptsMouseClicks (false, false);
    list.addChildComponent (hint);
    clearAll.setTooltip ("Remove every MIDI mapping");
    clearAll.onClick = [this] { ctl.clearMappings(); refresh(); };
    addAndMakeVisible (clearAll);
    count.setFont (fonts::body());
    addAndMakeVisible (count);
    ctl.addListener (this);
    refresh();
}

MidiMapPanel::~MidiMapPanel()
{
    ctl.removeListener (this);
    list.setModel (nullptr);
}

int MidiMapPanel::getNumRows() const { return static_cast<int> (ctl.mappings().size()); }

void MidiMapPanel::controllerStateChanged() { refresh(); }

void MidiMapPanel::refresh()
{
    list.updateContent();
    for (int i = 0; i < getNumRows(); ++i)
        if (auto* r = dynamic_cast<Row*> (list.getComponentForRowNumber (i)))
        {
            r->index = i;
            r->load();
        }
    const int n = getNumRows();
    count.setText (juce::String (n) + " of " + juce::String (midi::maxMappings) + " mappings", juce::dontSendNotification);
    clearAll.setEnabled (n > 0);
    hint.setVisible (n == 0);
    hint.setBounds (list.getLocalBounds().reduced (24, 40));
    repaint();
}

void MidiMapPanel::paint (juce::Graphics& g)
{
    const auto& t = SnaralLookAndFeel::tokensFor (*this);
    g.setColour (t.textMuted);
    g.setFont (fonts::label());
    auto head = getLocalBounds().removeFromTop (22).reduced (4, 0);
    head.removeFromRight (80 + 6);
    g.drawText ("TARGET", head.getX() + 4, head.getY(), 160, head.getHeight(), juce::Justification::centredLeft, false);
    int x = head.getRight();
    x -= 74 + 6; g.drawText ("INVERT", x, head.getY(), 74, head.getHeight(), juce::Justification::centred, false);
    x -= 58 + 6; g.drawText ("MAX %", x, head.getY(), 58, head.getHeight(), juce::Justification::centred, false);
    x -= 58 + 6; g.drawText ("MIN %", x, head.getY(), 58, head.getHeight(), juce::Justification::centred, false);
    x -= 84 + 6; g.drawText ("CHANNEL", x, head.getY(), 84, head.getHeight(), juce::Justification::centred, false);
    x -= 52 + 6; g.drawText ("CC", x, head.getY(), 52, head.getHeight(), juce::Justification::centred, false);
    if (getNumRows() == 0)
    {
        g.setColour (t.textMuted);
        g.setFont (fonts::body());
        g.drawFittedText ("No MIDI mappings yet. Right-click a knob, choose MIDI Learn, then move a controller on your keyboard or pad.",
                          list.getBounds().reduced (24, 20), juce::Justification::centredTop, 3);
    }
}

void MidiMapPanel::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop (24);
    auto foot = r.removeFromBottom (36);
    r.removeFromBottom (6);
    clearAll.setBounds (foot.removeFromRight (100).reduced (0, 2));
    count.setBounds (foot);
    list.setBounds (r);
    hint.setBounds (list.getLocalBounds().reduced (24, 40));
}
} // namespace snaral::ui
