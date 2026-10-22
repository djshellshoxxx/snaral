#include "PluginEditor.h"
#include "Parameters/ParameterRegistry.h"

#include <cmath>

namespace
{
const std::array<juce::Colour, 4> accentColours {
    juce::Colour::fromRGB (255, 104, 45),
    juce::Colour::fromRGB (50, 190, 170),
    juce::Colour::fromRGB (186, 122, 255),
    juce::Colour::fromRGB (230, 196, 68)
};
const std::array<juce::String, 4> themeNames { "EMBER", "TIDAL", "VIOLET", "BRASS" };
}

SnaralWaveform::SnaralWaveform (SnaralAudioProcessor& processor) : processor_ (processor)
{
    setTitle ("Live output waveform");
    setDescription ("Click or press Enter to switch between the live waveform and its output spectrum.");
    setWantsKeyboardFocus (true);
    startTimerHz (30);
}

void SnaralWaveform::setAnimationsEnabled (bool enabled)
{
    animationsEnabled_ = enabled;
    if (enabled) startTimerHz (30);
    else stopTimer();
    repaint();
}

void SnaralWaveform::timerCallback()
{
    processor_.copyVisualizationSamples (samples_);
    fftBuffer_.fill (0.0f);
    for (std::size_t i = 0; i < samples_.size(); ++i)
    {
        const auto window = 0.5f - 0.5f * std::cos (6.2831853f * static_cast<float> (i) / static_cast<float> (samples_.size() - 1));
        fftBuffer_[i] = samples_[i] * window;
    }
    fft_.performFrequencyOnlyForwardTransform (fftBuffer_.data());
    repaint();
}

void SnaralWaveform::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (juce::Colour::fromRGB (9, 12, 17));
    g.fillRoundedRectangle (bounds, 8.0f);
    g.setColour (juce::Colour::fromRGB (43, 51, 62));
    g.drawRoundedRectangle (bounds, 8.0f, 1.0f);
    const auto area = bounds.reduced (12.0f, 10.0f);
    g.setColour (juce::Colour::fromRGB (37, 44, 54));
    for (int i = 1; i < 4; ++i)
        g.drawHorizontalLine (juce::roundToInt (area.getY() + area.getHeight() * i / 4.0f), area.getX(), area.getRight());
    g.drawHorizontalLine (juce::roundToInt (area.getCentreY()), area.getX(), area.getRight());

    if (spectrumView_)
    {
        constexpr int barCount = 72;
        const auto barWidth = area.getWidth() / static_cast<float> (barCount);
        for (int bar = 0; bar < barCount; ++bar)
        {
            const auto normalized = static_cast<float> (bar + 1) / static_cast<float> (barCount);
            const auto bin = juce::jlimit (1, 511, juce::roundToInt (1.0f + normalized * normalized * 510.0f));
            const auto magnitude = juce::jlimit (0.0f, 1.0f, fftBuffer_[static_cast<std::size_t> (bin)] / 18.0f);
            const auto height = juce::jmax (1.0f, magnitude * area.getHeight() * 0.88f);
            const auto x = area.getX() + static_cast<float> (bar) * barWidth;
            g.setColour (juce::Colour::fromHSV (0.58f - normalized * 0.55f, 0.8f, 0.45f + magnitude * 0.55f, 0.78f));
            g.fillRoundedRectangle (x + 1.0f, area.getBottom() - height, juce::jmax (1.0f, barWidth - 2.0f), height, 2.0f);
        }
    }
    else
    {
        juce::Path signal;
        for (std::size_t i = 0; i < samples_.size(); ++i)
        {
            const auto x = area.getX() + area.getWidth() * static_cast<float> (i) / static_cast<float> (samples_.size() - 1);
            const auto y = area.getCentreY() - juce::jlimit (-1.0f, 1.0f, samples_[i]) * area.getHeight() * 0.44f;
            if (i == 0) signal.startNewSubPath (x, y); else signal.lineTo (x, y);
        }
        g.setColour (accentColours[0].withAlpha (0.28f));
        g.strokePath (signal, juce::PathStrokeType (5.0f));
        g.setColour (accentColours[0]);
        g.strokePath (signal, juce::PathStrokeType (1.8f));
    }
    g.setColour (juce::Colour::fromRGB (160, 170, 184));
    g.setFont (11.0f);
    g.drawText (spectrumView_ ? "OUTPUT SPECTRUM · CLICK OR ENTER FOR WAVEFORM" : "LIVE OUTPUT WAVEFORM · CLICK OR ENTER FOR SPECTRUM",
                getLocalBounds().reduced (12, 4).removeFromTop (18), juce::Justification::centredLeft);
    if (! animationsEnabled_)
        g.drawText ("ANIMATION PAUSED", getLocalBounds().reduced (12, 4).removeFromTop (18), juce::Justification::centredRight);
}

void SnaralWaveform::mouseUp (const juce::MouseEvent& event)
{
    if (event.mods.isLeftButtonDown())
    {
        spectrumView_ = ! spectrumView_;
        grabKeyboardFocus();
        repaint();
    }
}

bool SnaralWaveform::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::returnKey)
    {
        spectrumView_ = ! spectrumView_;
        repaint();
        return true;
    }
    return false;
}

SnaralKeyboardPanel::SnaralKeyboardPanel (juce::MidiKeyboardState& state, std::function<void()> onClose)
    : keyboard_ (state, juce::MidiKeyboardComponent::horizontalKeyboard), onClose_ (std::move (onClose))
{
    setAlwaysOnTop (true);
    setSize (640, 144);
    collapseButton_.onClick = [this] { setCollapsed (! collapsed_); };
    closeButton_.onClick = [this] { if (onClose_) onClose_(); };
    collapseButton_.setWantsKeyboardFocus (true);
    closeButton_.setWantsKeyboardFocus (true);
    addAndMakeVisible (collapseButton_);
    addAndMakeVisible (closeButton_);
    keyboard_.setAvailableRange (36, 84);
    keyboard_.setKeyPressBaseOctave (3);
    keyboard_.setKeyPressForNote (juce::KeyPress ('a'), 0);
    keyboard_.setKeyPressForNote (juce::KeyPress ('w'), 1);
    keyboard_.setKeyPressForNote (juce::KeyPress ('s'), 2);
    keyboard_.setKeyPressForNote (juce::KeyPress ('e'), 3);
    keyboard_.setKeyPressForNote (juce::KeyPress ('d'), 4);
    keyboard_.setKeyPressForNote (juce::KeyPress ('f'), 5);
    keyboard_.setKeyPressForNote (juce::KeyPress ('t'), 6);
    keyboard_.setKeyPressForNote (juce::KeyPress ('g'), 7);
    keyboard_.setKeyPressForNote (juce::KeyPress ('y'), 8);
    keyboard_.setKeyPressForNote (juce::KeyPress ('h'), 9);
    keyboard_.setKeyPressForNote (juce::KeyPress ('u'), 10);
    keyboard_.setKeyPressForNote (juce::KeyPress ('j'), 11);
    keyboard_.setKeyPressForNote (juce::KeyPress ('k'), 12);
    keyboard_.setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, accentColours[0].withAlpha (0.75f));
    addAndMakeVisible (keyboard_);
    constrainer_.setMinimumOnscreenAmounts (24, 24, 24, 24);
}

void SnaralKeyboardPanel::setCollapsed (bool shouldCollapse)
{
    collapsed_ = shouldCollapse;
    keyboard_.setVisible (! collapsed_);
    setSize (getWidth(), collapsed_ ? 28 : 144);
    collapseButton_.setButtonText (collapsed_ ? "KEYBOARD  ▸" : "KEYBOARD  ▾");
    resized();
}

void SnaralKeyboardPanel::resized()
{
    auto area = getLocalBounds();
    auto header = area.removeFromTop (26);
    closeButton_.setBounds (header.removeFromRight (28));
    collapseButton_.setBounds (header.removeFromLeft (130));
    if (! collapsed_)
        keyboard_.setBounds (area.reduced (4, 0));
}

void SnaralKeyboardPanel::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isLeftButtonDown()) dragger_.startDraggingComponent (this, e);
}

void SnaralKeyboardPanel::mouseDrag (const juce::MouseEvent& e)
{
    if (e.mods.isLeftButtonDown()) dragger_.dragComponent (this, e, &constrainer_);
}

void SnaralKeyboardPanel::mouseDoubleClick (const juce::MouseEvent&)
{
    setCollapsed (! collapsed_);
}

SnaralAudioProcessorEditor::SnaralAudioProcessorEditor (SnaralAudioProcessor& processor)
    : AudioProcessorEditor (&processor), processor_ (processor)
{
    setSize (900, 560);
    setResizable (true, true);
    setResizeLimits (720, 480, 1440, 900);
    setWantsKeyboardFocus (true);
    title_.setText ("SNARAL", juce::dontSendNotification);
    title_.setJustificationType (juce::Justification::centredLeft);
    title_.setFont (juce::Font (20.0f, juce::Font::bold));
    title_.setColour (juce::Label::textColourId, juce::Colour::fromRGB (230, 232, 236));
    addAndMakeVisible (title_);

    pitchBendSlider_.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    pitchBendSlider_.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 58, 18);
    pitchBendSlider_.setRange (-2.0, 2.0, 0.01);
    pitchBendSlider_.setDoubleClickReturnValue (true, 0.0);
    pitchBendSlider_.setNumDecimalPlacesToDisplay (1);
    pitchBendSlider_.setTitle ("Pitch Bend");
    pitchBendSlider_.setDescription ("Manual pitch bend, from two semitones down to two semitones up. Double-click to center.");
    pitchBendSlider_.setWantsKeyboardFocus (true);
    pitchBendSlider_.setColour (juce::Slider::rotarySliderFillColourId, accentColours[0]);
    pitchBendSlider_.onValueChange = [this] { processor_.enqueueUiPitchBend (static_cast<float> (pitchBendSlider_.getValue())); };
    addAndMakeVisible (pitchBendSlider_);
    pitchBendLabel_.setText ("PITCH BEND", juce::dontSendNotification);
    pitchBendLabel_.setJustificationType (juce::Justification::centred);
    pitchBendLabel_.setFont (juce::Font (10.0f));
    pitchBendLabel_.setColour (juce::Label::textColourId, juce::Colour::fromRGB (179, 187, 199));
    addAndMakeVisible (pitchBendLabel_);

    playButton_.setWantsKeyboardFocus (true);
    playButton_.setColour (juce::TextButton::buttonColourId, juce::Colour::fromRGB (45, 177, 115));
    playButton_.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
    playButton_.setTooltip ("Audition C3. Shortcut: Space.");
    playButton_.onClick = [this] { togglePlayback(); };
    addAndMakeVisible (playButton_);
    keyboardButton_.setWantsKeyboardFocus (true);
    keyboardButton_.setTooltip ("Show the virtual MIDI keyboard. Keyboard shortcut: K.");
    keyboardButton_.setColour (juce::TextButton::buttonColourId, juce::Colours::white);
    keyboardButton_.setColour (juce::TextButton::textColourOffId, juce::Colour::fromRGB (15, 18, 23));
    keyboardButton_.setColour (juce::TextButton::buttonOnColourId, juce::Colours::white);
    keyboardButton_.onClick = [this]
    {
        keyboardShown_ = ! keyboardShown_;
        if (keyboardPanel_ != nullptr) keyboardPanel_->setVisible (keyboardShown_);
        keyboardButton_.setToggleState (keyboardShown_, juce::dontSendNotification);
    };
    addAndMakeVisible (keyboardButton_);
    animationButton_.onClick = [this]
    {
        animationsEnabled_ = ! animationsEnabled_;
        animationButton_.setButtonText (animationsEnabled_ ? "ANIMATION: ON" : "ANIMATION: OFF");
        if (waveform_ != nullptr) waveform_->setAnimationsEnabled (animationsEnabled_);
    };
    animationButton_.setTooltip ("Turn waveform animation on or off. Shortcut: A.");
    addAndMakeVisible (animationButton_);
    themeButton_.onClick = [this] { themeIndex_ = (themeIndex_ + 1) % static_cast<int> (themeNames.size()); updateTheme(); };
    themeButton_.setTooltip ("Cycle the interface color theme. Shortcut: T.");
    addAndMakeVisible (themeButton_);
    importMidiButton_.setTooltip ("Load and audition a Standard MIDI File. Shortcut: I.");
    importMidiButton_.onClick = [this] { importMidiFile(); };
    addAndMakeVisible (importMidiButton_);
    exportMidiButton_.setTooltip ("Export notes played on the virtual keyboard or with the Play control as a Standard MIDI File. Shortcut: O.");
    exportMidiButton_.onClick = [this] { exportMidiFile(); };
    addAndMakeVisible (exportMidiButton_);

    macroHeading_.setText ("MACRO CONTROLS  ·  1–5 focus a macro  ·  Ctrl+R resets the focused macro", juce::dontSendNotification);
    macroHeading_.setJustificationType (juce::Justification::centredLeft);
    macroHeading_.setFont (juce::Font (12.0f, juce::Font::bold));
    macroHeading_.setColour (juce::Label::textColourId, juce::Colour::fromRGB (142, 153, 169));
    addAndMakeVisible (macroHeading_);

    for (std::size_t i = 0; i < snaral::macroParameterSpecs.size(); ++i)
    {
        const auto& spec = snaral::macroParameterSpecs[i];
        const auto id = juce::String::fromUTF8 (spec.id.data(), static_cast<int> (spec.id.size()));
        const auto name = juce::String::fromUTF8 (spec.displayName.data(), static_cast<int> (spec.displayName.size()));
        auto& label = macroLabels_[i];
        label.setText (name.toUpperCase(), juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setFont (juce::Font (12.0f, juce::Font::bold));
        label.setColour (juce::Label::textColourId, juce::Colour::fromRGB (216, 221, 228));
        label.setTitle (name + " macro");
        addAndMakeVisible (label);

        auto& slider = macroSliders_[i];
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 72, 22);
        slider.setTextValueSuffix (" %");
        slider.setNumDecimalPlacesToDisplay (0);
        slider.setRange (spec.minimum, spec.maximum, 1.0);
        slider.setDoubleClickReturnValue (true, spec.defaultValue);
        slider.setWantsKeyboardFocus (true);
        slider.setTitle (name);
        slider.setDescription (name + " macro control. Use the arrow keys to adjust; double-click to reset.");
        slider.setColour (juce::Slider::rotarySliderFillColourId, accentColours[themeIndex_]);
        slider.setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour::fromRGB (52, 61, 74));
        slider.setColour (juce::Slider::thumbColourId, juce::Colour::fromRGB (245, 247, 250));
        slider.setColour (juce::Slider::textBoxTextColourId, juce::Colour::fromRGB (230, 232, 236));
        slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colour::fromRGB (24, 29, 37));
        slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colour::fromRGB (52, 61, 74));
        addAndMakeVisible (slider);
        macroAttachments_[i] = std::make_unique<SliderAttachment> (processor.getParameters(), id, slider);
    }

    waveform_ = std::make_unique<SnaralWaveform> (processor);
    addAndMakeVisible (*waveform_);
    keyboardPanel_ = std::make_unique<SnaralKeyboardPanel> (processor.getKeyboardState(), [this]
    {
        keyboardShown_ = false;
        keyboardPanel_->setVisible (false);
        keyboardButton_.setToggleState (false, juce::dontSendNotification);
    });
    keyboardPanel_->setVisible (false);
    addAndMakeVisible (*keyboardPanel_);
    keyboardPanel_->setBounds (getWidth() - 660, getHeight() - 185, 640, 144);
    addMouseListener (this, true);

    status_.setText ("MIDI Sine Foundation  ·  Space: play/stop  ·  K: keyboard  ·  A: animation  ·  T: theme", juce::dontSendNotification);
    status_.setJustificationType (juce::Justification::centred);
    status_.setColour (juce::Label::textColourId, juce::Colour::fromRGB (138, 146, 158));
    addAndMakeVisible (status_);
    resized();
    updateTheme();
}

SnaralAudioProcessorEditor::~SnaralAudioProcessorEditor()
{
    processor_.getKeyboardState().allNotesOff (1);
}

void SnaralAudioProcessorEditor::togglePlayback()
{
    if (importedMidiPlaying_)
    {
        importedMidiPlaying_ = false;
        stopTimer();
        for (int channel = 1; channel <= 16; ++channel)
            processor_.getKeyboardState().allNotesOff (channel);
        pitchBendSlider_.setValue (0.0, juce::sendNotificationSync);
        auditioning_ = false;
        playButton_.setButtonText ("▶  PLAY");
        playButton_.setColour (juce::TextButton::buttonColourId, juce::Colour::fromRGB (45, 177, 115));
        status_.setText ("MIDI playback stopped", juce::dontSendNotification);
        return;
    }
    if (auditioning_)
    {
        processor_.getKeyboardState().noteOff (1, auditionNote_, 0.0f);
        auditioning_ = false;
        playButton_.setButtonText ("▶  PLAY");
        playButton_.setColour (juce::TextButton::buttonColourId, juce::Colour::fromRGB (45, 177, 115));
    }
    else
    {
        processor_.getKeyboardState().noteOn (1, auditionNote_, 0.8f);
        auditioning_ = true;
        playButton_.setButtonText ("■  STOP");
        playButton_.setColour (juce::TextButton::buttonColourId, juce::Colour::fromRGB (192, 60, 52));
    }
}

bool SnaralAudioProcessorEditor::keyPressed (const juce::KeyPress& key)
{
    if (auto* focused = juce::Component::getCurrentlyFocusedComponent(); dynamic_cast<juce::TextEditor*> (focused) != nullptr)
        return false;
    if (key == juce::KeyPress::spaceKey) { togglePlayback(); return true; }
    if (key.getTextCharacter() == 'k' || key.getTextCharacter() == 'K') { keyboardButton_.triggerClick(); return true; }
    if (key.getTextCharacter() == 'a' || key.getTextCharacter() == 'A') { animationButton_.triggerClick(); return true; }
    if (key.getTextCharacter() == 't' || key.getTextCharacter() == 'T') { themeButton_.triggerClick(); return true; }
    if (key.getTextCharacter() == 'i' || key.getTextCharacter() == 'I') { importMidiFile(); return true; }
    if (key.getTextCharacter() == 'o' || key.getTextCharacter() == 'O') { exportMidiFile(); return true; }
    if (key.getTextCharacter() >= '1' && key.getTextCharacter() <= '5')
    {
        const auto index = static_cast<std::size_t> (key.getTextCharacter() - '1');
        macroSliders_[index].grabKeyboardFocus();
        return true;
    }
    if (key == juce::KeyPress ('r', juce::ModifierKeys::commandModifier, 0))
    {
        for (std::size_t i = 0; i < macroSliders_.size(); ++i)
            if (macroSliders_[i].hasKeyboardFocus (true))
            {
                macroSliders_[i].setValue (snaral::macroParameterSpecs[i].defaultValue, juce::sendNotificationSync);
                return true;
            }
    }
    return false;
}

void SnaralAudioProcessorEditor::mouseDown (const juce::MouseEvent& event)
{
    if (! keyboardShown_ || keyboardPanel_ == nullptr)
        return;
    const auto* target = event.eventComponent;
    if (target != keyboardPanel_.get() && ! keyboardPanel_->isParentOf (target))
        keyboardPanel_->collapse();
}

void SnaralAudioProcessorEditor::importMidiFile()
{
    midiFileChooser_ = std::make_unique<juce::FileChooser> ("Import MIDI", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.mid;*.midi");
    const juce::Component::SafePointer<SnaralAudioProcessorEditor> safeThis (this);
    midiFileChooser_->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safeThis] (const juce::FileChooser& chooser)
        {
            auto* self = safeThis.getComponent();
            if (self == nullptr) return;
            const auto file = chooser.getResult();
            if (! file.existsAsFile()) return;
            juce::FileInputStream input (file);
            juce::MidiFile midi;
            if (! input.openedOk() || ! midi.readFrom (input))
            {
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "MIDI import failed", "The selected file is not a readable Standard MIDI File.", "OK", self);
                return;
            }
            midi.convertTimestampTicksToSeconds();
            self->importedMidi_.clear();
            for (int track = 0; track < midi.getNumTracks(); ++track)
            {
                if (const auto* sequence = midi.getTrack (track))
                    for (int eventIndex = 0; eventIndex < sequence->getNumEvents(); ++eventIndex)
                    {
                        const auto* event = sequence->getEventPointer (eventIndex);
                        if (event->message.isNoteOn() || event->message.isNoteOff() || event->message.isPitchWheel())
                            self->importedMidi_.addEvent (event->message);
                    }
            }
            self->importedMidi_.sort();
            if (self->importedMidi_.getNumEvents() == 0)
            {
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "No playable MIDI events", "The file contains no note or pitch-bend events.", "OK", self);
                return;
            }
            for (int channel = 1; channel <= 16; ++channel)
                self->processor_.getKeyboardState().allNotesOff (channel);
            self->auditioning_ = false;
            self->pitchBendSlider_.setValue (0.0, juce::sendNotificationSync);
            self->importedMidiEventIndex_ = 0;
            self->importedMidiStartTimeMs_ = juce::Time::getMillisecondCounterHiRes();
            self->importedMidiPlaying_ = true;
            self->playButton_.setButtonText ("■  STOP MIDI");
            self->status_.setText ("Playing MIDI file: " + file.getFileName(), juce::dontSendNotification);
            self->startTimerHz (60);
        });
}

void SnaralAudioProcessorEditor::exportMidiFile()
{
    const auto recorded = processor_.createRecordedMidiSequence();
    if (recorded.getNumEvents() == 0)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Nothing to export", "Play a note with the virtual keyboard or Play control first.", "OK", this);
        return;
    }
    midiFileChooser_ = std::make_unique<juce::FileChooser> ("Export MIDI", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Snaral-performance.mid"), "*.mid");
    const juce::Component::SafePointer<SnaralAudioProcessorEditor> safeThis (this);
    midiFileChooser_->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
        [safeThis, recorded] (const juce::FileChooser& chooser)
        {
            auto* self = safeThis.getComponent();
            if (self == nullptr) return;
            auto file = chooser.getResult();
            if (file == juce::File()) return;
            if (! file.hasFileExtension ("mid")) file = file.withFileExtension ("mid");
            juce::MidiMessageSequence ticks;
            ticks.addEvent (juce::MidiMessage::tempoMetaEvent (500000), 0.0);
            for (int eventIndex = 0; eventIndex < recorded.getNumEvents(); ++eventIndex)
            {
                const auto* event = recorded.getEventPointer (eventIndex);
                ticks.addEvent (event->message.withTimeStamp (event->message.getTimeStamp() * 1920.0));
            }
            ticks.sort();
            ticks.updateMatchedPairs();
            juce::MidiFile midi;
            midi.setTicksPerQuarterNote (960);
            midi.addTrack (ticks);
            juce::FileOutputStream output (file);
            auto succeeded = output.openedOk() && midi.writeTo (output, 0);
            if (succeeded)
            {
                output.flush();
                succeeded = ! output.getStatus().failed();
            }
            if (! succeeded)
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "MIDI export failed", "Snaral could not write the selected MIDI file.", "OK", self);
            else
                self->status_.setText ("Exported MIDI: " + file.getFileName(), juce::dontSendNotification);
        });
}

void SnaralAudioProcessorEditor::timerCallback()
{
    if (! importedMidiPlaying_)
    {
        stopTimer();
        return;
    }
    const auto elapsedSeconds = (juce::Time::getMillisecondCounterHiRes() - importedMidiStartTimeMs_) / 1000.0;
    while (importedMidiEventIndex_ < importedMidi_.getNumEvents())
    {
        const auto* event = importedMidi_.getEventPointer (importedMidiEventIndex_);
        if (event->message.getTimeStamp() > elapsedSeconds) break;
        const auto& message = event->message;
        const auto channel = message.getChannel();
        if (message.isNoteOn()) processor_.getKeyboardState().noteOn (channel, message.getNoteNumber(), message.getFloatVelocity());
        else if (message.isNoteOff()) processor_.getKeyboardState().noteOff (channel, message.getNoteNumber(), message.getFloatVelocity());
        else if (message.isPitchWheel())
        {
            const auto wheel = message.getPitchWheelValue();
            const auto bend = wheel < 8192 ? 2.0f * static_cast<float> (wheel - 8192) / 8192.0f
                                           : 2.0f * static_cast<float> (wheel - 8192) / 8191.0f;
            processor_.enqueueUiPitchBend (bend);
        }
        ++importedMidiEventIndex_;
    }
    if (importedMidiEventIndex_ >= importedMidi_.getNumEvents())
    {
        for (int channel = 1; channel <= 16; ++channel)
            processor_.getKeyboardState().allNotesOff (channel);
        pitchBendSlider_.setValue (0.0, juce::sendNotificationSync);
        importedMidiPlaying_ = false;
        stopTimer();
        playButton_.setButtonText ("▶  PLAY");
        playButton_.setColour (juce::TextButton::buttonColourId, juce::Colour::fromRGB (45, 177, 115));
        status_.setText ("MIDI playback complete", juce::dontSendNotification);
    }
}

void SnaralAudioProcessorEditor::updateTheme()
{
    themeButton_.setButtonText ("THEME: " + themeNames[static_cast<std::size_t> (themeIndex_)]);
    for (auto& slider : macroSliders_)
        slider.setColour (juce::Slider::rotarySliderFillColourId, accentColours[static_cast<std::size_t> (themeIndex_)]);
    if (keyboardPanel_ != nullptr)
        keyboardPanel_->repaint();
    repaint();
}

void SnaralAudioProcessorEditor::paint (juce::Graphics& graphics)
{
    graphics.fillAll (juce::Colour::fromRGB (14, 17, 22));
    graphics.setColour (accentColours[static_cast<std::size_t> (themeIndex_)]);
    graphics.drawLine (0.0f, 12.0f, 12.0f, 0.0f, 2.0f);
    graphics.setColour (juce::Colour::fromRGB (42, 48, 58));
    graphics.drawHorizontalLine (52, 20.0f, static_cast<float> (getWidth() - 20));
    auto panel = getLocalBounds().toFloat().reduced (20.0f);
    panel.removeFromTop (68.0f);
    panel.removeFromBottom (52.0f);
    graphics.setColour (juce::Colour::fromRGB (20, 24, 31));
    graphics.fillRoundedRectangle (panel, 10.0f);
    graphics.setColour (juce::Colour::fromRGB (42, 48, 58));
    graphics.drawRoundedRectangle (panel, 10.0f, 1.0f);
}

void SnaralAudioProcessorEditor::resized()
{
    title_.setBounds (20, 7, 180, 36);
    pitchBendLabel_.setBounds (203, 0, 82, 14);
    pitchBendSlider_.setBounds (206, 12, 78, 42);
    playButton_.setBounds (getWidth() - 404, 8, 92, 34);
    keyboardButton_.setBounds (getWidth() - 304, 8, 108, 34);
    animationButton_.setBounds (getWidth() - 188, 8, 92, 34);
    themeButton_.setBounds (getWidth() - 88, 8, 76, 34);
    importMidiButton_.setBounds (20, getHeight() - 36, 82, 28);
    exportMidiButton_.setBounds (108, getHeight() - 36, 86, 28);
    status_.setBounds (202, getHeight() - 34, getWidth() - 214, 22);

    auto content = getLocalBounds().reduced (30, 58);
    macroHeading_.setBounds (content.removeFromTop (22));
    content.removeFromTop (4);
    auto macroRow = content.removeFromTop (155);
    const auto columnWidth = macroRow.getWidth() / static_cast<int> (macroSliders_.size());
    for (std::size_t i = 0; i < macroSliders_.size(); ++i)
    {
        auto column = macroRow.removeFromLeft (i + 1 == macroSliders_.size() ? macroRow.getWidth() : columnWidth).reduced (5, 0);
        macroLabels_[i].setBounds (column.removeFromTop (22));
        macroSliders_[i].setBounds (column);
    }
    content.removeFromTop (4);
    if (waveform_ != nullptr)
        waveform_->setBounds (content.removeFromTop (juce::jmax (100, content.getHeight() - 8)));

    if (keyboardPanel_ != nullptr && keyboardShown_)
    {
        if (keyboardPanel_->getBounds().isEmpty())
            keyboardPanel_->setTopLeftPosition (getWidth() - 670, getHeight() - 190);
        keyboardPanel_->toFront (false);
    }
}
