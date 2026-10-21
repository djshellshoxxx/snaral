#pragma once

#include "ParamContext.h"
#include "ParamFormat.h"
#include "Theme.h"

namespace snaral::ui
{
/// Rotary control with label, unit-aware value readout, MIDI-learn indicators and the shared context menu.
///
///  * Use for the five macros (Size::Large) and for every continuous parameter in Advanced (Size::Small / Normal).
///  * Drag (vertical/rotary), mouse wheel (only while the knob has keyboard focus, so scrolling a panel never changes a
///    value), arrow keys (1 % / Shift 10 % / Ctrl 0.1 %, or whole units for integer parameters), Home/End, Delete = reset,
///    Enter or F2 = type a value, Shift+F10 or the Menu key = context menu, double-click = reset, click on the
///    readout = type a value (Enter commits one host gesture, Escape restores).
///  * bind() ties it to an APVTS parameter id: range, default, label, unit, formatting and parsing come from
///    snaral::parameterTable; host automation gestures are begun/ended by the JUCE attachment.
///  * It is a juce::Slider, so JUCE's accessibility value interface (screen readers) works as usual.
class MacroKnob : public juce::Slider,
                  private juce::TextEditor::Listener,
                  private juce::Timer
{
public:
    enum class Size { Small, Normal, Large };
    enum class LearnState { None, Armed, Learned };

    explicit MacroKnob (const juce::String& label = {}, Size size = Size::Normal);
    ~MacroKnob() override;

    /// Binds to a parameter of the table (see parameterTable). Also sets label, unit, range, default and tooltip.
    void bind (juce::AudioProcessorValueTreeState&, const juce::String& parameterId);
    const ParamDef* getParamDef() const noexcept { return def; }

    // ---- look ---------------------------------------------------------------------------------------------------
    void setLabelText (const juce::String&);
    const juce::String& getLabelText() const noexcept { return labelText; }
    void setUnit (const juce::String&);
    void setKnobSize (Size);
    Size getSize() const noexcept { return size; }
    void setBipolar (bool);
    /// false = the value is only shown while hovering, focused or dragging (baseline 2.3). Default true.
    void setValueAlwaysVisible (bool);
    /// Preferred outer size (logical px) for the current size and label length.
    juce::Point<int> getPreferredSize() const;
    static int knobDiameter (Size);

    // ---- value text ---------------------------------------------------------------------------------------------
    using Formatter = std::function<juce::String (double)>;
    using Parser = std::function<bool (const juce::String&, double&, juce::String* error)>;
    void setValueFormatter (Formatter);
    void setValueParser (Parser);
    void setDefaultValue (double);
    bool hasDefaultValue() const noexcept { return defaultKnown; }
    double getDefaultValue() const noexcept { return defaultValue; }
    juce::String getReadoutText() const;

    // ---- MIDI learn indicators ----------------------------------------------------------------------------------
    void setLearnState (LearnState, const juce::String& detail = {});
    LearnState getLearnState() const noexcept { return learn; }

    // ---- context menu / actions ---------------------------------------------------------------------------------
    ContextHooks hooks;
    std::vector<ContextAction> getContextActions() const;
    void performContextAction (ContextAction);

    // ---- value entry --------------------------------------------------------------------------------------------
    void beginValueEntry();
    bool isEditingValue() const noexcept { return entry != nullptr; }
    /// Validates `text` and applies it as one change. Returns false (and keeps the last valid value) when invalid.
    bool commitValueEntry (const juce::String& text);
    void cancelValueEntry();
    bool isEntryInvalid() const noexcept { return entryInvalid; }

    // ---- juce overrides -----------------------------------------------------------------------------------------
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void focusGained (FocusChangeType) override;
    void focusLost (FocusChangeType) override;
    void enablementChanged() override;

private:
    juce::Rectangle<int> knobArea() const;
    juce::Rectangle<int> labelArea() const;
    juce::Rectangle<int> readoutArea() const;
    juce::Rectangle<int> badgeArea() const;
    int labelLineCount() const;
    void updateAccessibleText();
    void closeEntry (bool deferred);
    void nudge (double proportionDelta);
    void showMenu (juce::Point<int> screenPos = {});
    bool parseValue (const juce::String& text, double& out, juce::String* error) const;
    juce::String formatValue (double v) const;
    bool valueVisible() const;

    // TextEditor::Listener
    void textEditorTextChanged (juce::TextEditor&) override;
    void textEditorReturnKeyPressed (juce::TextEditor&) override;
    void textEditorEscapeKeyPressed (juce::TextEditor&) override;
    void textEditorFocusLost (juce::TextEditor&) override;
    void timerCallback() override;

    Size size;
    juce::String labelText, unit;
    const ParamDef* def = nullptr;
    Formatter formatter;
    Parser parser;
    double defaultValue = 0.0;
    bool defaultKnown = false;
    bool bipolar = false;
    bool alwaysShowValue = true;
    bool hot = false;
    bool entryClick = false;
    bool ignoreGesture = false;
    unsigned pendingEntryToken = 0;
    bool entryInvalid = false;
    bool closingEntry = false;
    bool pulsePhase = false;
    LearnState learn = LearnState::None;
    juce::String learnDetail;
    std::unique_ptr<juce::TextEditor> entry;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};
} // namespace snaral::ui
