#pragma once

#include "ParamContext.h"
#include "ParamFormat.h"
#include "Theme.h"

namespace snaral::ui
{
/// Shared implementation of the validated copy / paste / reset actions for parameter widgets whose value can be
/// read and written as a domain value. Returns false (after reporting through hooks.onFeedback) when a paste was
/// invalid; in that case nothing was changed.
bool performParamValueAction (const ParamDef& def, ContextAction action, const ContextHooks& hooks,
                              const juce::String& label,
                              const std::function<float()>& getDomainValue,
                              const std::function<void (float)>& setDomainValue);

//==================================================================================================================
/// Horizontal slider row: LABEL | track | value box. The value box is click-to-edit with validation (invalid text
/// restores the previous value and reports through hooks.onFeedback). Bound to an APVTS parameter id from the table.
/// Use when a panel would hold only a few controls, or where a fader is clearer than a knob (e.g. Output Trim).
class ParamSlider : public juce::Component
{
public:
    ParamSlider (juce::AudioProcessorValueTreeState&, const juce::String& parameterId, int labelWidth = 104);
    ~ParamSlider() override;

    juce::Slider& getSlider() noexcept;
    const ParamDef& getParamDef() const noexcept { return *def; }
    ContextHooks hooks;

    static constexpr int preferredHeight = 32;
    std::vector<ContextAction> getContextActions() const;
    void performContextAction (ContextAction);

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    class Inner;
    void showMenu (juce::Point<int> screenPos);

    const ParamDef* def;
    int labelWidth;
    std::unique_ptr<Inner> slider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

//==================================================================================================================
/// Labelled combo box for choice parameters (names prettified from the table, e.g. "saw_stack" -> "Saw Stack").
class ParamCombo : public juce::Component
{
public:
    ParamCombo (juce::AudioProcessorValueTreeState&, const juce::String& parameterId);
    ~ParamCombo() override;

    juce::ComboBox& getComboBox() noexcept;
    const ParamDef& getParamDef() const noexcept { return *def; }
    ContextHooks hooks;

    static constexpr int preferredHeight = 14 + 4 + metrics::buttonHeight + 2;
    int getPreferredWidth() const;
    std::vector<ContextAction> getContextActions() const;
    void performContextAction (ContextAction);

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    class Inner;
    void showMenu (juce::Point<int> screenPos);

    const ParamDef* def;
    std::unique_ptr<Inner> combo;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
};

//==================================================================================================================
/// 28 px toggle button for bool parameters. On/off is shown by a check mark in the box and the accent tint.
class ParamToggle : public juce::ToggleButton
{
public:
    ParamToggle (juce::AudioProcessorValueTreeState&, const juce::String& parameterId);
    ~ParamToggle() override;

    const ParamDef& getParamDef() const noexcept { return *def; }
    ContextHooks hooks;

    int getPreferredWidth() const;
    std::vector<ContextAction> getContextActions() const;
    void performContextAction (ContextAction);

    void mouseDown (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    void showMenu (juce::Point<int> screenPos);

    const ParamDef* def;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
};
} // namespace snaral::ui
