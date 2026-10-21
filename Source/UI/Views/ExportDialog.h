#pragma once

// Offline WAV export dialog (docs/specs/cdl-offline-wav-export.md). The render runs on a worker thread with its own
// SnaralEngine configured from a copy of the current parameters; the live audio engine is never touched.

#include "EditorController.h"
#include "Core/ExportPhrases.h"
#include "Core/MidiFileReader.h"
#include "UI/Theme.h"

#include <atomic>

namespace snaral::ui
{
class ExportDialog : public juce::Component,
                     private juce::Timer
{
public:
    explicit ExportDialog (EditorController&);
    ~ExportDialog() override;

    /// Refreshes the default file name / sample rate from the current preset and host rate. Called when the dialog opens.
    void prepareForOpen();
    bool isRendering() const noexcept { return job != nullptr && ! job->finished.load(); }
    void cancelRender();

    // exposed for tests and automation
    juce::ComboBox& phraseCombo() noexcept { return phraseBox; }
    juce::ComboBox& rateCombo() noexcept { return rateBox; }
    juce::ComboBox& bitsCombo() noexcept { return bitsBox; }
    juce::ComboBox& channelsCombo() noexcept { return channelsBox; }
    juce::TextEditor& destinationEditor() noexcept { return destEdit; }
    juce::TextEditor& tempoEditor() noexcept { return bpmEdit; }
    juce::TextEditor& tailEditor() noexcept { return tailEdit; }
    juce::TextButton& renderButton() noexcept { return renderBtn; }
    juce::String getResultText() const { return resultLabel.getText(); }
    bool loadMidiFile (const juce::File&);
    void startRender (bool skipOverwriteCheck = false);
    juce::String disclosureText() const { return disclosure.getText(); }
    std::function<void()> onFinished;                       ///< render ended (any outcome)

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Job;
    void timerCallback() override;
    void settingsChanged();
    void updateDefaultFileName();
    void chooseMidi();
    void chooseDestination();
    void beginJob (const juce::File& destination);
    void finishJob();
    void showProblem (const juce::String&);

    EditorController& ctl;
    std::vector<render::ExportPhrasePreset> phrases;
    juce::Label disclosure, tempoNote, resultLabel, progressLabel;
    juce::ComboBox phraseBox, rateBox, channelsBox, bitsBox;
    juce::TextEditor bpmEdit, noteEdit, velEdit, tailEdit, destEdit;
    juce::ToggleButton normalise { "Normalise to -1 dBFS peak" };
    juce::TextButton midiBtn { "Choose .mid file..." }, browseBtn { "Browse..." }, renderBtn { "Render" };
    double progress = 0.0;
    juce::ProgressBar bar { progress };
    bool destinationEdited = false;
    bool updatingFields = false;
    std::optional<midifile::ParseResult> midiFile;
    juce::String midiName;
    std::unique_ptr<juce::FileChooser> chooser;

    struct Job : public juce::Thread
    {
        Job() : juce::Thread ("Snaral WAV export") {}
        ~Job() override { signalThreadShouldExit(); cancel.store (true); stopThread (10000); }
        void run() override;

        ParamSnapshot patch;
        render::MidiPhrase phrase;
        render::ExportSettings settings;
        std::unique_ptr<midi::MidiLearnTable> learnTable;
        std::filesystem::path destination;
        std::atomic<bool> cancel { false }, finished { false };
        std::atomic<std::uint64_t> rendered { 0 }, total { 1 };
        render::ExportResult result;
        std::chrono::steady_clock::time_point started;
    };
    std::unique_ptr<Job> job;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ExportDialog)
};
} // namespace snaral::ui
