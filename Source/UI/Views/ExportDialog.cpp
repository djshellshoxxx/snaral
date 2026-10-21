#include "ExportDialog.h"

#include "Core/WavWriter.h"
#include "Engine/SnaralEngine.h"

#include <ctime>

namespace snaral::ui
{
namespace
{
const int sampleRates[] = { 44100, 48000, 88200, 96000, 192000 };

juce::String utf8 (const std::string& s) { return juce::String::fromUTF8 (s.data(), static_cast<int> (s.size())); }

std::string utcDate()
{
    const std::time_t t = std::time (nullptr);
    std::tm tmv {};
   #if JUCE_WINDOWS
    gmtime_s (&tmv, &t);
   #else
    gmtime_r (&t, &tmv);
   #endif
    char buf[16];
    std::strftime (buf, sizeof buf, "%Y-%m-%d", &tmv);
    return buf;
}

juce::File defaultExportFolder (EditorPrefs& prefs)
{
    const auto last = prefs.getString ("exportFolder");
    if (last.isNotEmpty())
        return juce::File (last);
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Circuit Drift Labs").getChildFile ("Snaral")
        .getChildFile ("Exports");
}
} // namespace

void ExportDialog::Job::run()
{
    started = std::chrono::steady_clock::now();
    try
    {
        auto engine = std::make_unique<SnaralEngine>();   // separate instance: never the live audio engine
        render::OfflineRenderer<SnaralEngine> renderer (*engine);
        render::Callbacks cb;
        cb.cancelled = [this] { return cancel.load() || threadShouldExit(); };
        cb.progress = [this] (const render::Progress& p) { rendered.store (p.renderedFrames); total.store (juce::jmax<std::uint64_t> (1, p.totalFrames)); };
        result = renderer.renderToFile (patch, phrase, settings, destination, cb);
    }
    catch (const std::exception& e)
    {
        result = {};
        result.status = render::ExportStatus::EngineError;
        result.message = std::string ("The render stopped unexpectedly: ") + e.what();
    }
    catch (...)
    {
        result = {};
        result.status = render::ExportStatus::EngineError;
        result.message = "The render stopped unexpectedly.";
    }
    finished.store (true);
}

ExportDialog::ExportDialog (EditorController& c) : ctl (c), phrases (render::exportPhrasePresets())
{
    setTitle ("Export WAV");
    setWantsKeyboardFocus (true);
    setFocusContainerType (FocusContainerType::focusContainer);

    disclosure.setText ("Renders Snaral only. Host effects, host automation and host transport are not included. "
                        "Bypass, solo and mute are ignored.", juce::dontSendNotification);
    disclosure.setFont (fonts::body());
    disclosure.setMinimumHorizontalScale (1.0f);
    addAndMakeVisible (disclosure);

    int id = 1;
    for (const auto& p : phrases)
        phraseBox.addItem (utf8 (p.name), id++);
    phraseBox.addItem ("MIDI file (.mid)...", id);
    phraseBox.setSelectedId (1, juce::dontSendNotification);
    phraseBox.setTitle ("Phrase");
    phraseBox.onChange = [this]
    {
        if (phraseBox.getSelectedId() == static_cast<int> (phrases.size()) + 1 && ! midiFile.has_value())
            chooseMidi();
        else
            settingsChanged();
    };
    addAndMakeVisible (phraseBox);

    midiBtn.onClick = [this] { chooseMidi(); };
    midiBtn.setTooltip ("Pick a Standard MIDI file (type 0 or 1). Tempo changes inside the file are ignored.");
    addAndMakeVisible (midiBtn);

    for (auto* e : { &bpmEdit, &noteEdit, &velEdit, &tailEdit })
    {
        e->setFont (fonts::value());
        e->setSelectAllWhenFocused (true);
        e->setInputRestrictions (6, "0123456789.");
        e->onTextChange = [this] { settingsChanged(); };
        addAndMakeVisible (*e);
    }
    bpmEdit.setText ("174", false);
    bpmEdit.setTitle ("Tempo in beats per minute");
    noteEdit.setText ("36", false);
    noteEdit.setTitle ("Start note, 24 to 84");
    velEdit.setText ("100", false);
    velEdit.setTitle ("Velocity, 1 to 127");
    tailEdit.setText ("2.0", false);
    tailEdit.setTitle ("Tail in seconds, 0 to 30");

    for (int r : sampleRates)
        rateBox.addItem (juce::String (r) + " Hz", r);
    rateBox.setTitle ("Sample rate");
    rateBox.onChange = [this] { settingsChanged(); };
    addAndMakeVisible (rateBox);
    channelsBox.addItem ("Stereo", 2);
    channelsBox.addItem ("Mono (L+R)/2", 1);
    channelsBox.setSelectedId (2, juce::dontSendNotification);
    channelsBox.setTitle ("Channels");
    addAndMakeVisible (channelsBox);
    bitsBox.addItem ("24-bit PCM", 24);
    bitsBox.addItem ("16-bit PCM (dithered)", 16);
    bitsBox.addItem ("32-bit float", 32);
    bitsBox.setSelectedId (24, juce::dontSendNotification);
    bitsBox.setTitle ("Bit depth");
    bitsBox.onChange = [this] { settingsChanged(); };
    addAndMakeVisible (bitsBox);
    normalise.setTooltip ("One gain after the render so the sample peak is exactly -1 dBFS");
    addAndMakeVisible (normalise);

    destEdit.setFont (fonts::body().withHeight (12.0f));
    destEdit.setTitle ("Destination file");
    destEdit.onTextChange = [this] { if (! updatingFields) destinationEdited = true; };
    addAndMakeVisible (destEdit);
    browseBtn.onClick = [this] { chooseDestination(); };
    addAndMakeVisible (browseBtn);

    tempoNote.setFont (fonts::small());
    tempoNote.setMinimumHorizontalScale (1.0f);
    addAndMakeVisible (tempoNote);
    progressLabel.setFont (fonts::value().withHeight (12.0f));
    addAndMakeVisible (progressLabel);
    resultLabel.setFont (fonts::body());
    resultLabel.setMinimumHorizontalScale (1.0f);
    resultLabel.setTitle ("Export result");
    addAndMakeVisible (resultLabel);
    addAndMakeVisible (bar);
    bar.setVisible (false);

    renderBtn.onClick = [this] { if (isRendering()) cancelRender(); else startRender(); };
    addAndMakeVisible (renderBtn);

    // restore last settings
    auto& p = ctl.prefs();
    bitsBox.setSelectedId (p.getInt ("exportBits", 24), juce::dontSendNotification);
    channelsBox.setSelectedId (p.getInt ("exportChannels", 2), juce::dontSendNotification);
    tailEdit.setText (juce::String (p.getDouble ("exportTail", 2.0), 1), false);
    normalise.setToggleState (p.getBool ("exportNormalise", false), juce::dontSendNotification);
    prepareForOpen();
}

ExportDialog::~ExportDialog()
{
    stopTimer();
    job.reset();   // cancels and joins; the renderer removes its temporary files
}

void ExportDialog::prepareForOpen()
{
    if (isRendering())
        return;
    const double hostRate = ctl.processor().getSampleRate();
    int rate = 48000;
    for (int r : sampleRates)
        if (std::abs (hostRate - r) < 0.5)
            rate = r;
    if (rateBox.getSelectedId() == 0 || ! destinationEdited)
        rateBox.setSelectedId (ctl.prefs().getInt ("exportRate", rate), juce::dontSendNotification);
    if (rateBox.getSelectedId() == 0)
        rateBox.setSelectedId (rate, juce::dontSendNotification);
    destinationEdited = false;
    updateDefaultFileName();
    settingsChanged();
    resultLabel.setText ({}, juce::dontSendNotification);
}

void ExportDialog::updateDefaultFileName()
{
    if (destinationEdited)
        return;
    const auto fmt = bitsBox.getSelectedId() == 16 ? wav::SampleFormat::Pcm16 : (bitsBox.getSelectedId() == 32 ? wav::SampleFormat::Float32 : wav::SampleFormat::Pcm24);
    const auto name = wav::defaultExportFileName (ctl.presetName().toStdString() == "Init" ? std::string() : ctl.presetName().toStdString(),
                                                  juce::jmax (1, rateBox.getSelectedId()), fmt);
    const juce::ScopedValueSetter<bool> g (updatingFields, true);
    destEdit.setText (defaultExportFolder (ctl.prefs()).getChildFile (juce::String (name)).getFullPathName(), false);
}

void ExportDialog::settingsChanged()
{
    const bool builtin = phraseBox.getSelectedId() == 1;
    noteEdit.setEnabled (builtin);
    velEdit.setEnabled (builtin);
    const bool isMidi = phraseBox.getSelectedId() == static_cast<int> (phrases.size()) + 1;
    if (isMidi && midiFile.has_value() && midiFile->ok)
    {
        const auto& i = midiFile->info;
        tempoNote.setText (midiName + ": " + juce::String (i.noteCount) + " notes, " + juce::String (i.lengthSeconds, 1) + " s at "
                               + juce::String (i.bpm, 1) + " BPM."
                               + (i.tempoChanges ? " Tempo changes in the file are ignored; one constant tempo is used." : juce::String()),
                           juce::dontSendNotification);
    }
    else
        tempoNote.setText (builtin ? "Eight quarter notes of a C natural minor scale from the start note." : juce::String(), juce::dontSendNotification);
    updateDefaultFileName();
    repaint();
}

bool ExportDialog::loadMidiFile (const juce::File& file)
{
    juce::MemoryBlock data;
    if (! file.loadFileAsData (data))
    {
        showProblem ("The MIDI file could not be read.");
        return false;
    }
    if (data.getSize() > midifile::maxFileBytes)
    {
        showProblem ("The MIDI file is larger than 4 MB, which is more than Snaral will read.");
        return false;
    }
    auto parsed = midifile::parse (static_cast<const std::uint8_t*> (data.getData()), data.getSize());
    if (! parsed.ok)
    {
        showProblem ("MIDI file rejected: " + utf8 (parsed.error));
        return false;
    }
    midiName = file.getFileName();
    midiFile = std::move (parsed);
    phraseBox.setSelectedId (static_cast<int> (phrases.size()) + 1, juce::dontSendNotification);
    bpmEdit.setText (juce::String (midiFile->info.bpm, 1), false);
    resultLabel.setText ({}, juce::dontSendNotification);
    settingsChanged();
    return true;
}

void ExportDialog::chooseMidi()
{
    chooser = std::make_unique<juce::FileChooser> ("Choose a MIDI file", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.mid;*.midi");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
    {
        const auto f = fc.getResult();
        if (f != juce::File())
            loadMidiFile (f);
        else if (! midiFile.has_value())
            phraseBox.setSelectedId (1, juce::sendNotification);
    });
}

void ExportDialog::chooseDestination()
{
    chooser = std::make_unique<juce::FileChooser> ("Save the WAV file", juce::File (destEdit.getText()), "*.wav");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc)
    {
        auto f = fc.getResult();
        if (f == juce::File())
            return;
        if (! f.hasFileExtension ("wav"))
            f = f.withFileExtension ("wav");
        destEdit.setText (f.getFullPathName(), false);
        destinationEdited = true;
    });
}

void ExportDialog::showProblem (const juce::String& text)
{
    resultLabel.setText (text, juce::dontSendNotification);
    resultLabel.setColour (juce::Label::textColourId, SnaralLookAndFeel::tokensFor (*this).error);
    ctl.status (Severity::Error, text, 10000);
}

void ExportDialog::cancelRender()
{
    if (job != nullptr)
        job->cancel.store (true);
}

void ExportDialog::startRender (bool skipOverwriteCheck)
{
    if (isRendering())
        return;
    resultLabel.setColour (juce::Label::textColourId, SnaralLookAndFeel::tokensFor (*this).textPrimary);

    const double bpm = bpmEdit.getText().getDoubleValue();
    const double tail = tailEdit.getText().getDoubleValue();
    if (! (bpm >= 20.0 && bpm <= 400.0)) { showProblem ("Tempo must be between 20 and 400 BPM."); return; }
    if (! (tail >= 0.0 && tail <= 30.0)) { showProblem ("Tail must be between 0 and 30 seconds."); return; }
    if (rateBox.getSelectedId() == 0) { showProblem ("Choose a sample rate."); return; }

    auto next = std::make_unique<Job>();
    const bool isMidi = phraseBox.getSelectedId() == static_cast<int> (phrases.size()) + 1;
    if (isMidi)
    {
        if (! midiFile.has_value() || ! midiFile->ok) { showProblem ("Choose a MIDI file first, or pick one of the built-in phrases."); return; }
        next->phrase = midiFile->phrase;
    }
    else
    {
        const auto idx = static_cast<std::size_t> (phraseBox.getSelectedId() - 1);
        next->phrase = phrases[idx].phrase;
        if (idx == 0)
        {
            const int note = noteEdit.getText().getIntValue();
            const int vel = velEdit.getText().getIntValue();
            if (note < 24 || note > 84) { showProblem ("The start note must be between 24 and 84."); return; }
            if (vel < 1 || vel > 127) { showProblem ("Velocity must be between 1 and 127."); return; }
            next->phrase = render::builtinPhrase (note, vel);
        }
    }

    auto& s = next->settings;
    s.bpm = bpm;
    s.sampleRate = rateBox.getSelectedId();
    s.channels = channelsBox.getSelectedId() == 1 ? 1 : 2;
    s.sample = bitsBox.getSelectedId() == 16 ? wav::SampleFormat::Pcm16 : (bitsBox.getSelectedId() == 32 ? wav::SampleFormat::Float32 : wav::SampleFormat::Pcm24);
    s.tailSeconds = tail;
    s.normalise = normalise.getToggleState();
    s.blockSize = 512;
    s.metadata.software = "Snaral " + ctl.versionText().toStdString();
    s.metadata.title = ctl.presetName().toStdString();
    s.metadata.dateUtc = utcDate();

    // the patch is a copy taken now; later edits do not affect the render. Utility state (bypass, solo, mutes) is ignored.
    next->patch = ctl.snapshot();
    for (auto id : { P::bypass, P::layer_solo, P::sub_mute, P::mid_mute })
        next->patch[id] = parameterTable[static_cast<std::size_t> (id)].defaultValue;
    if (! ctl.mappings().empty())
    {
        next->learnTable = midi::MidiLearnTable::build (ctl.mappings());
        s.learnTable = next->learnTable.get();
    }

    juce::File dest (destEdit.getText().trim());
    if (dest.getFullPathName().isEmpty() || dest.isDirectory()) { showProblem ("Choose a file name for the WAV file."); return; }
    if (! dest.hasFileExtension ("wav"))
        dest = dest.withFileExtension ("wav");
    next->destination = std::filesystem::path (dest.getFullPathName().toStdString());

    // validation by the renderer before any file is touched
    render::ExportStatus st;
    if (const auto problem = render::validateExport (s, next->phrase, st); ! problem.empty())
    {
        showProblem (utf8 (problem));
        return;
    }

    if (dest.exists() && ! skipOverwriteCheck && ctl.showDialog)
    {
        DialogSpec d;
        d.title = "File exists";
        d.message = "\"" + dest.getFileName() + "\" already exists. Nothing is overwritten unless you choose Replace.";
        d.buttons = { "Save with a number", "Replace", "Cancel" };
        d.onResult = [this, dest] (int button, const juce::StringArray&)
        {
            if (button == 0)
            {
                const auto unique = wav::uniqueFilePath (std::filesystem::path (dest.getFullPathName().toStdString()));
                destEdit.setText (juce::String (unique.string()), false);
                startRender (true);
            }
            else if (button == 1)
                startRender (true);
        };
        ctl.showDialog (std::move (d));
        return;
    }

    ctl.prefs().set ("exportFolder", dest.getParentDirectory().getFullPathName());
    ctl.prefs().set ("exportBits", bitsBox.getSelectedId());
    ctl.prefs().set ("exportChannels", channelsBox.getSelectedId());
    ctl.prefs().set ("exportRate", rateBox.getSelectedId());
    ctl.prefs().set ("exportTail", tail);
    ctl.prefs().set ("exportNormalise", normalise.getToggleState());

    job = std::move (next);
    progress = 0.0;
    bar.setVisible (true);
    renderBtn.setButtonText ("Cancel render");
    resultLabel.setText ({}, juce::dontSendNotification);
    job->startThread();
    startTimerHz (10);
    ctl.status (Severity::Info, "Rendering to " + dest.getFileName() + "...", 4000);
    resized();
}

void ExportDialog::timerCallback()
{
    if (job == nullptr)
    {
        stopTimer();
        return;
    }
    const auto total = job->total.load();
    progress = juce::jlimit (0.0, 1.0, static_cast<double> (job->rendered.load()) / static_cast<double> (total));
    const double elapsed = std::chrono::duration<double> (std::chrono::steady_clock::now() - job->started).count();
    progressLabel.setText (juce::String (progress * 100.0, 0) + " %   elapsed " + juce::String (elapsed, 1) + " s", juce::dontSendNotification);
    if (job->finished.load())
        finishJob();
}

void ExportDialog::finishJob()
{
    stopTimer();
    job->stopThread (5000);
    const auto r = job->result;
    job.reset();
    bar.setVisible (false);
    progressLabel.setText ({}, juce::dontSendNotification);
    renderBtn.setButtonText ("Render");
    const auto& t = SnaralLookAndFeel::tokensFor (*this);
    if (r.status == render::ExportStatus::Ok)
    {
        const auto text = "Saved " + juce::String (r.path.string()) + "\nLength " + juce::String (r.durationSeconds, 2) + " s, peak "
                          + juce::String (r.peakDbfs, 1) + " dBFS, clipped samples " + juce::String (static_cast<juce::int64> (r.clippedSamples))
                          + ", rendered in " + juce::String (r.elapsedSeconds, 1) + " s";
        resultLabel.setColour (juce::Label::textColourId, t.success);
        resultLabel.setText (text, juce::dontSendNotification);
        ctl.status (Severity::Success, "Exported " + juce::File (juce::String (r.path.string())).getFileName() + " (" + juce::String (r.durationSeconds, 1)
                                           + " s, peak " + juce::String (r.peakDbfs, 1) + " dBFS)", 10000);
    }
    else if (r.status == render::ExportStatus::Cancelled)
    {
        resultLabel.setColour (juce::Label::textColourId, t.warning);
        resultLabel.setText ("Export cancelled. No file was written.", juce::dontSendNotification);
        ctl.status (Severity::Info, "Export cancelled. No file was written.", 5000);
    }
    else
    {
        resultLabel.setColour (juce::Label::textColourId, t.error);
        resultLabel.setText ("Export failed: " + utf8 (r.message) + " Any existing file was left untouched.", juce::dontSendNotification);
        ctl.status (Severity::Error, "Export failed: " + utf8 (r.message), 12000);
    }
    resized();
    if (onFinished)
        onFinished();
}

void ExportDialog::paint (juce::Graphics& g)
{
    const auto& t = SnaralLookAndFeel::tokensFor (*this);
    g.setColour (t.textMuted);
    g.setFont (fonts::label());
    auto cap = [&] (juce::Component& c, const juce::String& text)
    {
        g.drawText (text.toUpperCase(), c.getX(), c.getY() - 16, juce::jmax (80, c.getWidth()), 14, juce::Justification::centredLeft, false);
    };
    cap (phraseBox, "Phrase");
    cap (bpmEdit, "Tempo (BPM)");
    cap (noteEdit, "Start note");
    cap (velEdit, "Velocity");
    cap (rateBox, "Sample rate");
    cap (channelsBox, "Channels");
    cap (bitsBox, "Bit depth");
    cap (tailEdit, "Tail (s)");
    cap (destEdit, "Destination file");
}

void ExportDialog::resized()
{
    auto r = getLocalBounds().reduced (6, 0);
    disclosure.setBounds (r.removeFromTop (38));
    r.removeFromTop (22);
    const int rowH = 30;
    const int colW = (r.getWidth() - 16) / 2;
    auto left = r.removeFromLeft (colW);
    r.removeFromLeft (16);
    auto right = r;

    // left column: phrase, midi button, tempo / note / velocity
    phraseBox.setBounds (left.removeFromTop (rowH));
    left.removeFromTop (6);
    midiBtn.setBounds (left.removeFromTop (rowH));
    left.removeFromTop (22);
    auto triple = left.removeFromTop (rowH);
    const int tw = (triple.getWidth() - 12) / 3;
    bpmEdit.setBounds (triple.removeFromLeft (tw));
    triple.removeFromLeft (6);
    noteEdit.setBounds (triple.removeFromLeft (tw));
    triple.removeFromLeft (6);
    velEdit.setBounds (triple);
    left.removeFromTop (4);
    tempoNote.setBounds (left.removeFromTop (46));

    // right column: rate, channels, bits, tail, normalise
    auto pair = right.removeFromTop (rowH);
    rateBox.setBounds (pair.removeFromLeft ((pair.getWidth() - 8) / 2));
    pair.removeFromLeft (8);
    channelsBox.setBounds (pair);
    right.removeFromTop (22);
    pair = right.removeFromTop (rowH);
    bitsBox.setBounds (pair.removeFromLeft ((pair.getWidth() - 8) * 2 / 3));
    pair.removeFromLeft (8);
    tailEdit.setBounds (pair);
    right.removeFromTop (8);
    normalise.setBounds (right.removeFromTop (metrics::minHitTarget));

    auto bottom = getLocalBounds().reduced (6, 0);
    bottom.removeFromTop (std::max (left.getY(), right.getY()) - bottom.getY());
    bottom.removeFromTop (6);
    bottom.removeFromTop (16);
    auto dest = bottom.removeFromTop (rowH);
    browseBtn.setBounds (dest.removeFromRight (96));
    dest.removeFromRight (6);
    destEdit.setBounds (dest);
    bottom.removeFromTop (8);
    auto btnRow = bottom.removeFromBottom (metrics::minHitTarget + 2);
    renderBtn.setBounds (btnRow.removeFromRight (140));
    progressLabel.setBounds (btnRow);
    bottom.removeFromBottom (6);
    if (bar.isVisible())
    {
        bar.setBounds (bottom.removeFromTop (16));
        bottom.removeFromTop (4);
    }
    resultLabel.setBounds (bottom);
}
} // namespace snaral::ui
