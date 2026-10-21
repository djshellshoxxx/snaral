#include "MacroKnob.h"

#include <cmath>

namespace snaral::ui
{
namespace
{
constexpr int badgeHeight = 14;
constexpr int arcMargin = 8;           // 4 px gap + 3 px arc + 1 px focus slack
constexpr int readoutHeight = 18;
constexpr int labelLineHeight = 13;

int preferredWidthFor (MacroKnob::Size s)
{
    switch (s)
    {
        case MacroKnob::Size::Small:  return 72;
        case MacroKnob::Size::Normal: return 80;
        case MacroKnob::Size::Large:  return 96;
    }
    return 80;
}
} // namespace

int MacroKnob::knobDiameter (Size s)
{
    switch (s)
    {
        case Size::Small:  return metrics::knobSmall;
        case Size::Normal: return metrics::knobNormal;
        case Size::Large:  return metrics::knobLarge;
    }
    return metrics::knobNormal;
}

MacroKnob::MacroKnob (const juce::String& label, Size s) : size (s), labelText (label)
{
    setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    setScrollWheelEnabled (true);
    setWantsKeyboardFocus (true);
    setMouseClickGrabsKeyboardFocus (true);
    setRange (0.0, 100.0, 0.0);
    setMouseDragSensitivity (180);
    const auto pref = getPreferredSize();
    setSize (pref.x, pref.y);
    updateAccessibleText();
}

MacroKnob::~MacroKnob()
{
    stopTimer();
    attachment.reset();
}

// ---- configuration ----------------------------------------------------------------------------------------------
void MacroKnob::setLabelText (const juce::String& t)
{
    labelText = t;
    updateAccessibleText();
    repaint();
}

void MacroKnob::setUnit (const juce::String& u)  { unit = u; repaint(); }
void MacroKnob::setKnobSize (Size s)              { size = s; resized(); repaint(); }
void MacroKnob::setBipolar (bool b)               { bipolar = b; repaint(); }
void MacroKnob::setValueAlwaysVisible (bool b)    { alwaysShowValue = b; repaint(); }
void MacroKnob::setValueFormatter (Formatter f)   { formatter = std::move (f); repaint(); }
void MacroKnob::setValueParser (Parser p)         { parser = std::move (p); }

void MacroKnob::setDefaultValue (double v)
{
    defaultValue = v;
    defaultKnown = true;
    setDoubleClickReturnValue (true, v);
    updateAccessibleText();
}

void MacroKnob::bind (juce::AudioProcessorValueTreeState& apvts, const juce::String& id)
{
    def = findParamDef (id);
    jassert (def != nullptr);                    // ids must come from snaral::parameterTable
    attachment.reset();
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, id, *this);

    if (def != nullptr)
    {
        labelText = toJuce (def->name);
        unit = toJuce (def->unit);
        bipolar = def->minimum < 0.0f && def->maximum > 0.0f && def->type == ParamType::Float;
        setDefaultValue (static_cast<double> (def->defaultValue));
    }
    // The attachment installs its own text conversion; ours keeps units and formatting identical everywhere.
    textFromValueFunction = nullptr;
    valueFromTextFunction = nullptr;
    const auto pref = getPreferredSize();
    setSize (pref.x, pref.y);
    updateAccessibleText();
    repaint();
}

juce::Point<int> MacroKnob::getPreferredSize() const
{
    const int w = preferredWidthFor (size);
    const int d = knobDiameter (size);
    const auto font = fonts::label();
    const bool wraps = textWidth (font, labelText.toUpperCase()) > static_cast<float> (w - 4);
    const int lines = wraps ? 2 : 1;
    return { w, badgeHeight + d + 2 * arcMargin + 2 + lines * labelLineHeight + readoutHeight + 2 };
}

int MacroKnob::labelLineCount() const
{
    return textWidth (fonts::label(), labelText.toUpperCase()) > static_cast<float> (getWidth() - 4) ? 2 : 1;
}

void MacroKnob::updateAccessibleText()
{
    setTitle (labelText);
    juce::String d = labelText + " knob.";
    if (def != nullptr)
        d << " Range " << describeParamRange (*def) << ".";
    if (defaultKnown)
        d << " Default " << formatValue (defaultValue) << ". Press Delete or double-click to reset.";
    d << " Press Enter to type a value.";
    if (learn == LearnState::Armed)
        d << " MIDI learn is armed: move a controller to assign it.";
    else if (learn == LearnState::Learned)
        d << " MIDI mapped" << (learnDetail.isNotEmpty() ? " to " + learnDetail : juce::String()) << ".";
    setDescription (d);
    setTooltip (labelText + (def != nullptr ? ": " + describeParamRange (*def) : juce::String())
                + (defaultKnown ? ". Double-click to reset, right-click for more." : "."));
}

void MacroKnob::setLearnState (LearnState s, const juce::String& detail)
{
    learn = s;
    learnDetail = detail;
    pulsePhase = false;
    if (s == LearnState::Armed && SnaralLookAndFeel::animationsFor (*this))
        startTimerHz (4);
    else
        stopTimer();
    updateAccessibleText();
    repaint();
}

void MacroKnob::timerCallback()
{
    pulsePhase = ! pulsePhase;
    repaint (badgeArea());
}

// ---- text -------------------------------------------------------------------------------------------------------
juce::String MacroKnob::formatValue (double v) const
{
    if (formatter)
        return formatter (v);
    if (def != nullptr)
        return formatParamValue (*def, static_cast<float> (v));
    return formatPlainNumber (v, unit);
}

juce::String MacroKnob::getReadoutText() const { return formatValue (getValue()); }

bool MacroKnob::parseValue (const juce::String& text, double& out, juce::String* error) const
{
    if (parser)
        return parser (text, out, error);
    if (def != nullptr)
    {
        float f = 0.0f;
        if (! parseParamValue (*def, text, f, error))
            return false;
        out = static_cast<double> (f);
        return true;
    }

    // generic numeric parse with range validation
    const auto t = text.trim();
    if (t.isEmpty() || ! t.retainCharacters ("0123456789.+-").containsAnyOf ("0123456789"))
    {
        if (error != nullptr) *error = "Not a number";
        return false;
    }
    const double v = t.getDoubleValue();
    if (v < getMinimum() || v > getMaximum())
    {
        if (error != nullptr)
            *error = "Out of range (" + formatPlainNumber (getMinimum(), unit) + " to " + formatPlainNumber (getMaximum(), unit) + ")";
        return false;
    }
    out = v;
    return true;
}

bool MacroKnob::valueVisible() const
{
    return alwaysShowValue || hot || hasKeyboardFocus (true) || isMouseButtonDown() || entry != nullptr;
}

// ---- geometry ---------------------------------------------------------------------------------------------------
juce::Rectangle<int> MacroKnob::badgeArea() const
{
    return { getWidth() - 46, 0, 46, badgeHeight };
}

juce::Rectangle<int> MacroKnob::knobArea() const
{
    const int s = knobDiameter (size) + 2 * arcMargin;
    return juce::Rectangle<int> (s, s).withCentre ({ getWidth() / 2, badgeHeight + s / 2 });
}

juce::Rectangle<int> MacroKnob::labelArea() const
{
    const auto k = knobArea();
    return { 2, k.getBottom() + 1, getWidth() - 4, labelLineCount() * labelLineHeight };
}

juce::Rectangle<int> MacroKnob::readoutArea() const
{
    const auto l = labelArea();
    const int w = juce::jmin (getWidth(), 88);
    return juce::Rectangle<int> (w, readoutHeight).withX ((getWidth() - w) / 2).withY (l.getBottom() + 1);
}

void MacroKnob::resized()
{
    if (entry != nullptr)
        entry->setBounds (readoutArea().expanded (0, 3).withTrimmedBottom (0));
}

// ---- painting ---------------------------------------------------------------------------------------------------
void MacroKnob::paint (juce::Graphics& g)
{
    const auto& t = SnaralLookAndFeel::tokensFor (*this);
    const auto ka = knobArea();
    const auto d = static_cast<float> (knobDiameter (size));

    KnobPaint kp;
    kp.position01 = static_cast<float> (valueToProportionOfLength (getValue()));
    kp.bipolar = bipolar;
    kp.enabled = isEnabled();
    kp.focused = hasKeyboardFocus (false) && entry == nullptr;
    kp.hot = hot;
    kp.dragging = isMouseButtonDown() && ! entryClick;
    kp.invalid = entryInvalid;
    SnaralLookAndFeel::drawKnob (g, t, juce::Rectangle<float> (d, d).withCentre (ka.toFloat().getCentre()), kp);

    // label (11 px upper-case, wraps to two lines when needed)
    g.setFont (fonts::label());
    g.setColour (isEnabled() ? t.textPrimary : t.textMuted);
    g.drawFittedText (labelText.toUpperCase(), labelArea(), juce::Justification::centredTop, labelLineCount(), 0.9f);

    // readout
    if (entry == nullptr && valueVisible())
    {
        g.setFont (fonts::value());
        g.setColour (kp.dragging ? t.accentText : (isEnabled() ? t.textPrimary : t.textMuted));
        g.drawFittedText (getReadoutText(), readoutArea(), juce::Justification::centred, 1, 0.8f);
    }
    if (entryInvalid)
        SnaralLookAndFeel::drawSeverityIcon (g, Severity::Error, juce::Rectangle<float> (readoutArea().getX() - 14.0f,
                                             readoutArea().getY() + 2.0f, 14.0f, 14.0f), t.error);

    // MIDI learn badge: text chip so it works without colour (outlined "LEARN" while armed, filled "MIDI"/CC when learned)
    if (learn != LearnState::None)
    {
        const auto chip = badgeArea().toFloat().reduced (1.0f);
        const auto text = learn == LearnState::Armed ? juce::String ("LEARN")
                                                     : (learnDetail.isNotEmpty() ? learnDetail.toUpperCase() : juce::String ("MIDI"));
        g.setFont (fonts::small().withHeight (10.0f).withExtraKerningFactor (0.04f));
        if (learn == LearnState::Armed)
        {
            const float a = (pulsePhase && SnaralLookAndFeel::animationsFor (*this)) ? 0.55f : 1.0f;
            g.setColour (t.secondary.withAlpha (a));
            g.drawRoundedRectangle (chip, 3.0f, 1.5f);
            g.drawFittedText (text, chip.toNearestInt(), juce::Justification::centred, 1, 0.8f);
        }
        else
        {
            g.setColour (t.secondary);
            g.fillRoundedRectangle (chip, 3.0f);
            g.setColour (t.background);
            g.drawFittedText (text, chip.toNearestInt(), juce::Justification::centred, 1, 0.8f);
        }
    }
}

// ---- mouse / keyboard -------------------------------------------------------------------------------------------
void MacroKnob::mouseEnter (const juce::MouseEvent& e)  { hot = true; repaint(); Slider::mouseEnter (e); }
void MacroKnob::mouseExit (const juce::MouseEvent& e)   { hot = false; repaint(); Slider::mouseExit (e); }

void MacroKnob::mouseDown (const juce::MouseEvent& e)
{
    ignoreGesture = false;
    entryClick = false;
    if (e.mods.isPopupMenu())
    {
        ignoreGesture = true;
        showMenu (e.getScreenPosition());
        return;
    }
    if (entry == nullptr && isEnabled() && valueVisible() && readoutArea().contains (e.getPosition()))
    {
        ignoreGesture = true;
        entryClick = true;
        grabKeyboardFocus();
        return;
    }
    Slider::mouseDown (e);
}

void MacroKnob::mouseDrag (const juce::MouseEvent& e)
{
    if (! ignoreGesture)
        Slider::mouseDrag (e);
}

void MacroKnob::mouseUp (const juce::MouseEvent& e)
{
    if (ignoreGesture)
    {
        ignoreGesture = false;
        if (entryClick && readoutArea().contains (e.getPosition()) && ! e.mouseWasDraggedSinceMouseDown())
        {
            // Delay slightly so a double-click on the readout still resets instead of opening the editor.
            const auto token = ++pendingEntryToken;
            juce::Component::SafePointer<MacroKnob> safe (this);
            juce::Timer::callAfterDelay (260, [safe, token]
            {
                if (safe != nullptr && safe->pendingEntryToken == token)
                    safe->beginValueEntry();
            });
        }
        entryClick = false;
        return;
    }
    Slider::mouseUp (e);
}

void MacroKnob::mouseDoubleClick (const juce::MouseEvent& e)
{
    ++pendingEntryToken;                                   // cancels a pending "open editor"
    Slider::mouseDoubleClick (e);
}

void MacroKnob::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (hasKeyboardFocus (false) && isEnabled())
        Slider::mouseWheelMove (e, w);
    else
        Component::mouseWheelMove (e, w);                   // lets the enclosing scroll panel scroll
}

void MacroKnob::nudge (double proportionDelta)
{
    const double v = getValue();
    const double p = valueToProportionOfLength (v);
    double nv = proportionOfLengthToValue (juce::jlimit (0.0, 1.0, p + proportionDelta));
    const double interval = getInterval();
    if (interval > 0.0 && std::abs (nv - v) < interval)
        nv = v + (proportionDelta > 0.0 ? interval : -interval);
    setValue (nv, juce::sendNotificationSync);
}

bool MacroKnob::keyPressed (const juce::KeyPress& key)
{
    if (! isEnabled())
        return false;

    const auto mods = key.getModifiers();
    const auto code = key.getKeyCode();

    if (key == juce::KeyPress::returnKey || key == juce::KeyPress::F2Key)
    {
        beginValueEntry();
        return true;
    }
    if (code == juce::KeyPress::F10Key && mods.isShiftDown())
    {
        showMenu();
        return true;
    }
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        if (defaultKnown)
            setValue (defaultValue, juce::sendNotificationSync);
        return true;
    }
    if (key == juce::KeyPress::homeKey)  { setValue (getMinimum(), juce::sendNotificationSync); return true; }
    if (key == juce::KeyPress::endKey)   { setValue (getMaximum(), juce::sendNotificationSync); return true; }

    double dir = 0.0;
    if (code == juce::KeyPress::rightKey || code == juce::KeyPress::upKey || code == juce::KeyPress::pageUpKey)    dir = 1.0;
    if (code == juce::KeyPress::leftKey  || code == juce::KeyPress::downKey || code == juce::KeyPress::pageDownKey) dir = -1.0;
    if (dir != 0.0)
    {
        double step = 0.01;
        if (mods.isShiftDown() || code == juce::KeyPress::pageUpKey || code == juce::KeyPress::pageDownKey) step = 0.10;
        if (mods.isCtrlDown() || mods.isAltDown() || mods.isCommandDown()) step = 0.001;
        nudge (dir * step);
        return true;
    }
    return Slider::keyPressed (key);
}

void MacroKnob::focusGained (FocusChangeType t)   { repaint(); Slider::focusGained (t); }
void MacroKnob::focusLost (FocusChangeType t)     { repaint(); Slider::focusLost (t); }
void MacroKnob::enablementChanged()               { repaint(); }

// ---- context menu -----------------------------------------------------------------------------------------------
std::vector<ContextAction> MacroKnob::getContextActions() const
{
    ContextMenuSpec spec;
    spec.canReset = defaultKnown;
    spec.hooks = &hooks;
    return availableContextActions (spec);
}

void MacroKnob::showMenu (juce::Point<int> screenPos)
{
    ContextMenuSpec spec;
    spec.canReset = defaultKnown;
    spec.hooks = &hooks;
    spec.perform = [this] (ContextAction a) { performContextAction (a); };
    showContextMenu (*this, spec, screenPos);
}

void MacroKnob::performContextAction (ContextAction a)
{
    switch (a)
    {
        case ContextAction::Reset:
            if (defaultKnown)
                setValue (defaultValue, juce::sendNotificationSync);
            break;
        case ContextAction::CopyValue:
            Clipboard::write (def != nullptr && ! formatter ? plainDomainValueText (*def, static_cast<float> (getValue()))
                                                              : formatValue (getValue()));
            break;
        case ContextAction::PasteValue:
        {
            const auto text = Clipboard::read();
            double v = 0.0;
            juce::String err;
            if (parseValue (text, v, &err))
                setValue (v, juce::sendNotificationSync);
            else
                sendFeedback (hooks, Severity::Error, "Cannot paste into " + labelText + ": " + err
                                                         + ". The value was not changed.");
            break;
        }
        case ContextAction::EnterValue:      beginValueEntry(); break;
        case ContextAction::MidiLearn:       if (hooks.onMidiLearn) hooks.onMidiLearn(); break;
        case ContextAction::ClearMidiMapping: if (hooks.onClearMidiMapping) hooks.onClearMidiMapping(); break;
        case ContextAction::RandomizeThis:   if (hooks.onRandomizeThis) hooks.onRandomizeThis(); break;
    }
}

// ---- numeric entry ----------------------------------------------------------------------------------------------
void MacroKnob::beginValueEntry()
{
    if (entry != nullptr || ! isEnabled())
        return;

    entryInvalid = false;
    entry = std::make_unique<juce::TextEditor>();
    entry->setMultiLine (false);
    entry->setReturnKeyStartsNewLine (false);
    entry->setScrollbarsShown (false);
    entry->setFont (fonts::value());
    entry->setJustification (juce::Justification::centred);
    entry->setSelectAllWhenFocused (true);
    entry->setIndents (2, 0);
    entry->setTitle (labelText + " value");
    entry->setDescription ("Type a value" + (def != nullptr ? " from " + describeParamRange (*def) : juce::String())
                           + ". Enter applies, Escape cancels.");
    entry->setText (formatValue (getValue()), false);
    entry->addListener (this);
    addAndMakeVisible (*entry);
    entry->setBounds (readoutArea().expanded (0, 3));
    entry->grabKeyboardFocus();
    entry->selectAll();
    repaint();
}

void MacroKnob::closeEntry (bool deferred)
{
    if (entry == nullptr)
        return;
    closingEntry = true;
    entry->removeListener (this);
    entry->setVisible (false);
    if (deferred)
    {
        auto* raw = entry.release();
        juce::MessageManager::callAsync ([raw] { delete raw; });
    }
    else
        entry.reset();
    closingEntry = false;
    entryInvalid = false;
    if (isShowing())
        grabKeyboardFocus();
    repaint();
}

bool MacroKnob::commitValueEntry (const juce::String& text)
{
    double v = 0.0;
    juce::String err;
    if (! parseValue (text, v, &err))
    {
        entryInvalid = true;
        if (entry != nullptr)
        {
            entry->setColour (juce::TextEditor::outlineColourId, SnaralLookAndFeel::tokensFor (*this).error);
            entry->setColour (juce::TextEditor::focusedOutlineColourId, SnaralLookAndFeel::tokensFor (*this).error);
        }
        sendFeedback (hooks, Severity::Error, labelText + ": '" + text.trim() + "' rejected. " + err
                                                  + ". Press Escape to keep the previous value.");
        repaint();
        return false;
    }
    setValue (v, juce::sendNotificationSync);            // one complete host gesture via the attachment
    closeEntry (false);
    return true;
}

void MacroKnob::cancelValueEntry()
{
    closeEntry (false);
}

void MacroKnob::textEditorTextChanged (juce::TextEditor& e)
{
    double v = 0.0;
    const bool ok = parseValue (e.getText(), v, nullptr);
    const auto& t = SnaralLookAndFeel::tokensFor (*this);
    entryInvalid = ! ok && e.getText().isNotEmpty();
    e.setColour (juce::TextEditor::outlineColourId, entryInvalid ? t.error : t.outline);
    e.setColour (juce::TextEditor::focusedOutlineColourId, entryInvalid ? t.error : t.focusRing);
    e.repaint();
    repaint();
}

void MacroKnob::textEditorReturnKeyPressed (juce::TextEditor& e)
{
    double v = 0.0;
    juce::String err;
    if (! parseValue (e.getText(), v, &err))
    {
        commitValueEntry (e.getText());            // reports the error, keeps the editor open
        return;
    }
    setValue (v, juce::sendNotificationSync);
    closeEntry (true);
}

void MacroKnob::textEditorEscapeKeyPressed (juce::TextEditor&)   { closeEntry (true); }

void MacroKnob::textEditorFocusLost (juce::TextEditor& e)
{
    if (closingEntry || entry == nullptr)
        return;
    double v = 0.0;
    if (parseValue (e.getText(), v, nullptr))
        setValue (v, juce::sendNotificationSync);
    closeEntry (true);
}
} // namespace snaral::ui
