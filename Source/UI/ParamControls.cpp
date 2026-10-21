#include "ParamControls.h"

#include <cmath>

namespace snaral::ui
{
bool performParamValueAction (const ParamDef& def, ContextAction action, const ContextHooks& hooks,
                              const juce::String& label, const std::function<float()>& get,
                              const std::function<void (float)>& set)
{
    switch (action)
    {
        case ContextAction::Reset:
            set (def.defaultValue);
            return true;
        case ContextAction::CopyValue:
            Clipboard::write (plainDomainValueText (def, get()));
            return true;
        case ContextAction::PasteValue:
        {
            float v = 0.0f;
            juce::String err;
            if (parseParamValue (def, Clipboard::read(), v, &err))
            {
                set (v);
                return true;
            }
            sendFeedback (hooks, Severity::Error, "Cannot paste into " + label + ": " + err + ". The value was not changed.");
            return false;
        }
        case ContextAction::EnterValue:
        case ContextAction::MidiLearn:
        case ContextAction::ClearMidiMapping:
        case ContextAction::RandomizeThis:
            break;
    }
    return true;
}

namespace
{
void dispatchHookAction (ContextAction a, const ContextHooks& h)
{
    if (a == ContextAction::MidiLearn && h.onMidiLearn)              h.onMidiLearn();
    if (a == ContextAction::ClearMidiMapping && h.onClearMidiMapping) h.onClearMidiMapping();
    if (a == ContextAction::RandomizeThis && h.onRandomizeThis)       h.onRandomizeThis();
}

void drawRowLabel (juce::Graphics& g, const ThemeTokens& t, const juce::String& text, juce::Rectangle<int> area,
                   juce::Justification j, bool enabled)
{
    g.setFont (fonts::label());
    g.setColour (enabled ? t.textPrimary : t.textMuted);
    g.drawFittedText (text.toUpperCase(), area, j, 2, 0.9f);
}
} // namespace

//==================================================================================================================
// ParamSlider
//==================================================================================================================
class ParamSlider::Inner : public juce::Slider
{
public:
    explicit Inner (ParamSlider& o) : owner (o) {}

    // The slider's value box is created by the LookAndFeel at creation / look-and-feel change time. A slider built before
    // it joined a themed hierarchy would keep the default colours, so refresh it whenever the hierarchy changes.
    void parentHierarchyChanged() override
    {
        Slider::parentHierarchyChanged();
        if (&getLookAndFeel() != lastLookAndFeel)
        {
            lastLookAndFeel = &getLookAndFeel();
            lookAndFeelChanged();
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        ignore = e.mods.isPopupMenu();
        if (ignore)
            owner.showMenu (e.getScreenPosition());
        else
            Slider::mouseDown (e);
    }
    void mouseDrag (const juce::MouseEvent& e) override { if (! ignore) Slider::mouseDrag (e); }
    void mouseUp (const juce::MouseEvent& e) override   { if (! ignore) Slider::mouseUp (e); ignore = false; }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        if (hasKeyboardFocus (false))
            Slider::mouseWheelMove (e, w);
        else
            Component::mouseWheelMove (e, w);
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        const auto code = key.getKeyCode();
        const auto mods = key.getModifiers();
        if (key == juce::KeyPress::returnKey || key == juce::KeyPress::F2Key)
        {
            showTextBox();
            return true;
        }
        if (code == juce::KeyPress::F10Key && mods.isShiftDown())
        {
            owner.showMenu ({});
            return true;
        }
        if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
        {
            setValue (static_cast<double> (owner.def->defaultValue), juce::sendNotificationSync);
            return true;
        }
        if (key == juce::KeyPress::homeKey) { setValue (getMinimum(), juce::sendNotificationSync); return true; }
        if (key == juce::KeyPress::endKey)  { setValue (getMaximum(), juce::sendNotificationSync); return true; }

        double dir = 0.0;
        if (code == juce::KeyPress::rightKey || code == juce::KeyPress::upKey)   dir = 1.0;
        if (code == juce::KeyPress::leftKey || code == juce::KeyPress::downKey)  dir = -1.0;
        if (dir == 0.0)
            return false;

        double step = mods.isShiftDown() ? 0.10 : 0.01;
        if (mods.isCtrlDown() || mods.isAltDown() || mods.isCommandDown())
            step = 0.001;
        const double v = getValue();
        double nv = proportionOfLengthToValue (juce::jlimit (0.0, 1.0, valueToProportionOfLength (v) + dir * step));
        if (getInterval() > 0.0 && std::abs (nv - v) < getInterval())
            nv = v + dir * getInterval();
        setValue (nv, juce::sendNotificationSync);
        return true;
    }

private:
    ParamSlider& owner;
    bool ignore = false;
    juce::LookAndFeel* lastLookAndFeel = nullptr;
};

ParamSlider::ParamSlider (juce::AudioProcessorValueTreeState& apvts, const juce::String& id, int labelW)
    : def (findParamDef (id)), labelWidth (labelW)
{
    jassert (def != nullptr);
    slider = std::make_unique<Inner> (*this);
    slider->setSliderStyle (juce::Slider::LinearHorizontal);
    slider->setTextBoxStyle (juce::Slider::TextBoxRight, false, 80, 24);
    slider->setWantsKeyboardFocus (true);
    addAndMakeVisible (*slider);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, id, *slider);
    const ParamDef* d = def;
    slider->textFromValueFunction = [d] (double v) { return formatParamValue (*d, static_cast<float> (v)); };
    slider->valueFromTextFunction = [this] (const juce::String& s)
    {
        float f = 0.0f;
        juce::String err;
        if (parseParamValue (*def, s, f, &err))
            return static_cast<double> (f);
        sendFeedback (hooks, Severity::Error, toJuce (def->name) + ": '" + s.trim() + "' rejected. " + err
                                                  + ". The value was not changed.");
        return slider->getValue();
    };
    slider->setDoubleClickReturnValue (true, static_cast<double> (def->defaultValue));
    slider->updateText();

    const auto name = toJuce (def->name);
    slider->setTitle (name);
    slider->setDescription (name + " slider. Range " + describeParamRange (*def) + ". Press Enter to type a value; double-click to reset.");
    slider->setTooltip (name + ": " + describeParamRange (*def) + ". Double-click to reset, right-click for more.");
    setSize (260, preferredHeight);
}

ParamSlider::~ParamSlider()
{
    attachment.reset();
}

juce::Slider& ParamSlider::getSlider() noexcept { return *slider; }

void ParamSlider::resized()
{
    slider->setBounds (getLocalBounds().withTrimmedLeft (labelWidth + 4));
}

void ParamSlider::paint (juce::Graphics& g)
{
    drawRowLabel (g, SnaralLookAndFeel::tokensFor (*this), toJuce (def->name), getLocalBounds().withWidth (labelWidth),
                  juce::Justification::centredLeft, isEnabled());
}

std::vector<ContextAction> ParamSlider::getContextActions() const
{
    ContextMenuSpec spec;
    spec.hooks = &hooks;
    return availableContextActions (spec);
}

void ParamSlider::showMenu (juce::Point<int> pos)
{
    ContextMenuSpec spec;
    spec.hooks = &hooks;
    spec.perform = [this] (ContextAction a) { performContextAction (a); };
    showContextMenu (*this, spec, pos);
}

void ParamSlider::performContextAction (ContextAction a)
{
    if (a == ContextAction::EnterValue)
    {
        slider->showTextBox();
        return;
    }
    performParamValueAction (*def, a, hooks, toJuce (def->name),
                             [this] { return static_cast<float> (slider->getValue()); },
                             [this] (float v) { slider->setValue (static_cast<double> (v), juce::sendNotificationSync); });
    dispatchHookAction (a, hooks);
}

//==================================================================================================================
// ParamCombo
//==================================================================================================================
class ParamCombo::Inner : public juce::ComboBox
{
public:
    explicit Inner (ParamCombo& o) : owner (o) {}

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
            owner.showMenu (e.getScreenPosition());
        else
            ComboBox::mouseDown (e);
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key.getKeyCode() == juce::KeyPress::F10Key && key.getModifiers().isShiftDown())
        {
            owner.showMenu ({});
            return true;
        }
        return ComboBox::keyPressed (key);
    }

private:
    ParamCombo& owner;
};

ParamCombo::ParamCombo (juce::AudioProcessorValueTreeState& apvts, const juce::String& id) : def (findParamDef (id))
{
    jassert (def != nullptr && def->type == ParamType::Choice);
    combo = std::make_unique<Inner> (*this);
    for (int i = 0; i < def->numChoices; ++i)
        combo->addItem (prettyChoiceName (def->choices[static_cast<std::size_t> (i)]), i + 1);
    combo->setWantsKeyboardFocus (true);
    addAndMakeVisible (*combo);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (apvts, id, *combo);

    const auto name = toJuce (def->name);
    combo->setTitle (name);
    combo->setDescription (name + " selector. Choices: " + describeParamRange (*def) + ".");
    combo->setTooltip (name + ": " + describeParamRange (*def));
    setSize (getPreferredWidth(), preferredHeight);
}

ParamCombo::~ParamCombo()
{
    attachment.reset();
}

juce::ComboBox& ParamCombo::getComboBox() noexcept { return *combo; }

int ParamCombo::getPreferredWidth() const
{
    float w = textWidth (fonts::label(), toJuce (def->name).toUpperCase());
    const auto f = fonts::body().withHeight (12.5f);
    for (int i = 0; i < def->numChoices; ++i)
        w = juce::jmax (w, textWidth (f, prettyChoiceName (def->choices[static_cast<std::size_t> (i)])) + 36.0f);
    return juce::jlimit (112, 200, static_cast<int> (std::ceil (w)) + 4);
}

void ParamCombo::resized()
{
    combo->setBounds (getLocalBounds().withTrimmedTop (18).withHeight (metrics::buttonHeight));
}

void ParamCombo::paint (juce::Graphics& g)
{
    drawRowLabel (g, SnaralLookAndFeel::tokensFor (*this), toJuce (def->name),
                  getLocalBounds().removeFromTop (16), juce::Justification::centredLeft, isEnabled());
}

std::vector<ContextAction> ParamCombo::getContextActions() const
{
    ContextMenuSpec spec;
    spec.canEnterValue = false;
    spec.hooks = &hooks;
    return availableContextActions (spec);
}

void ParamCombo::showMenu (juce::Point<int> pos)
{
    ContextMenuSpec spec;
    spec.canEnterValue = false;
    spec.hooks = &hooks;
    spec.perform = [this] (ContextAction a) { performContextAction (a); };
    showContextMenu (*this, spec, pos);
}

void ParamCombo::performContextAction (ContextAction a)
{
    performParamValueAction (*def, a, hooks, toJuce (def->name),
                             [this] { return static_cast<float> (combo->getSelectedItemIndex()); },
                             [this] (float v) { combo->setSelectedItemIndex (static_cast<int> (v + 0.5f), juce::sendNotificationSync); });
    dispatchHookAction (a, hooks);
}

//==================================================================================================================
// ParamToggle
//==================================================================================================================
ParamToggle::ParamToggle (juce::AudioProcessorValueTreeState& apvts, const juce::String& id) : def (findParamDef (id))
{
    jassert (def != nullptr && def->type == ParamType::Bool);
    const auto name = toJuce (def->name);
    setButtonText (name);
    setClickingTogglesState (true);
    setWantsKeyboardFocus (true);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, id, *this);
    setTitle (name);
    setDescription (name + " toggle. Press Space to switch it on or off.");
    setTooltip (name + ": On or Off");
    setSize (getPreferredWidth(), metrics::buttonHeight);
}

ParamToggle::~ParamToggle()
{
    attachment.reset();
}

int ParamToggle::getPreferredWidth() const
{
    return juce::jmax (84, static_cast<int> (std::ceil (textWidth (fonts::body().withHeight (12.5f), getButtonText()))) + 46);
}

std::vector<ContextAction> ParamToggle::getContextActions() const
{
    ContextMenuSpec spec;
    spec.canEnterValue = false;
    spec.hooks = &hooks;
    return availableContextActions (spec);
}

void ParamToggle::showMenu (juce::Point<int> pos)
{
    ContextMenuSpec spec;
    spec.canEnterValue = false;
    spec.hooks = &hooks;
    spec.perform = [this] (ContextAction a) { performContextAction (a); };
    showContextMenu (*this, spec, pos);
}

void ParamToggle::performContextAction (ContextAction a)
{
    performParamValueAction (*def, a, hooks, toJuce (def->name),
                             [this] { return getToggleState() ? 1.0f : 0.0f; },
                             [this] (float v) { setToggleState (v >= 0.5f, juce::sendNotificationSync); });
    dispatchHookAction (a, hooks);
}

void ParamToggle::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        showMenu (e.getScreenPosition());
    else
        ToggleButton::mouseDown (e);
}

bool ParamToggle::keyPressed (const juce::KeyPress& key)
{
    if (key.getKeyCode() == juce::KeyPress::F10Key && key.getModifiers().isShiftDown())
    {
        showMenu ({});
        return true;
    }
    return ToggleButton::keyPressed (key);
}
} // namespace snaral::ui
