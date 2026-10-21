#pragma once

// Message-thread logic behind the editor: undo/redo feed from parameter gestures, dirty tracking, preset load / save /
// import / export / audition, Reset, bounded Randomize, A/B compare, variation slots, MIDI learn flow and panic.
// The views only call these methods and listen for controllerStateChanged(); nothing here touches the audio thread
// except through the processor's lock-free hooks (panic, UI notes, block-boundary snapshot publish, CC map publish).

#include "PluginProcessor.h"
#include "Core/MidiLearn.h"
#include "Core/Randomizer.h"
#include "EditorPrefs.h"
#include "UI/ParamContext.h"
#include "UI/UiCommon.h"
#include "UI/UiOptions.h"

#include <array>
#include <functional>
#include <memory>

namespace snaral::ui
{
/// A small in-editor modal question (no native alert windows: they are unreliable inside plug-in hosts).
struct DialogSpec
{
    struct Field
    {
        juce::String label, text;
        bool multiline = false;
    };

    juce::String title, message;
    std::vector<Field> fields;
    std::vector<juce::String> buttons;   ///< first = default (Enter), last = cancel (Escape)
    std::function<void (int button, const juce::StringArray& values)> onResult;
};

class EditorController : private juce::AudioProcessorParameter::Listener,
                         private juce::Timer
{
public:
    struct Listener
    {
        virtual ~Listener() = default;
        /// Coalesced (at most 30 Hz): history, dirty state, preset name, A/B, variation slots or MIDI-learn state changed.
        virtual void controllerStateChanged() {}
    };

    EditorController (SnaralAudioProcessor&, OptionsModel&, EditorPrefs&);
    ~EditorController() override;

    // ---- hooks installed by the editor ----
    std::function<void (Severity, const juce::String&, int ttlMs)> onStatus;
    std::function<void (DialogSpec)> showDialog;
    std::function<void()> onPanicPerformed;          ///< e.g. to reset the Options reference-tone button

    void addListener (Listener* l) { listeners.add (l); }
    void removeListener (Listener* l) { listeners.remove (l); }

    SnaralAudioProcessor& processor() noexcept { return proc; }
    juce::AudioProcessorValueTreeState& apvts() noexcept { return proc.getParameters(); }
    OptionsModel& options() noexcept { return opts; }
    EditorPrefs& prefs() noexcept { return prefs_; }
    bool isStandalone() const noexcept { return proc.wrapperType == juce::AudioProcessor::wrapperType_Standalone; }

    void status (Severity, const juce::String&, int ttlMs = 5000);
    ParamSnapshot snapshot() const;

    // ---- history ----
    bool canUndo() const { return proc.undoHistory().canUndo(); }
    bool canRedo() const { return proc.undoHistory().canRedo(); }
    juce::String undoLabel() const;
    juce::String redoLabel() const;
    void undo();
    void redo();

    // ---- common actions ----
    void panic();
    void resetPatch();
    void randomize();
    void reproduceRandomize();
    juce::String seedHex() const;
    bool setSeedFromText (const juce::String& hex);
    rnd::LockMask randomizeLocks() const noexcept { return locks_; }
    void setRandomizeLocks (rnd::LockMask);
    bool hasRandomized() const noexcept { return haveSeed_; }

    // ---- A/B ----
    bool abEngaged() const { return proc.abCompare().engaged(); }
    AbCompare::Side abSide() const { return proc.abCompare().activeSide(); }
    void selectAb (AbCompare::Side);
    void copyAToB();
    void copyBToA();

    // ---- variation slots ----
    void recallVariation (int slot);
    void storeVariation (int slot);
    void clearVariation (int slot);
    void renameVariation (int slot);
    juce::String variationLabel (int slot) const;
    bool variationEmpty (int slot) const { return proc.variationBank().isEmpty (slot); }
    int activeVariation() const { return proc.variationBank().activeIndex(); }

    // ---- presets ----
    PresetBrowserModel& browser() noexcept { return proc.presetBrowser(); }
    PresetLibrary& library() noexcept { return proc.presetLibrary(); }
    juce::String presetName() const;
    bool isModified() const;
    void rescanLibrary();
    void requestLoadPreset (const std::string& id);
    void stepPreset (int direction);
    void savePreset();
    void savePresetAs();
    void importPreset();
    void exportPreset();
    void toggleFavourite (const std::string& id);
    void beginAudition (const std::string& id);
    void confirmAudition();
    void cancelAudition();
    bool isAuditioning() const { return proc.presetBrowser().auditionActive(); }

    // ---- MIDI learn ----
    bool learnArmed() const noexcept { return learn_.isArmed(); }
    int learnTarget() const noexcept { return learn_.armedTarget(); }
    void toggleLearn (const juce::String& paramId);
    void cancelLearn();
    void unlink (const juce::String& paramId);
    bool isLearned (const juce::String& paramId) const;
    juce::String learnDetail (const juce::String& paramId) const;
    const std::vector<midi::MidiMapping>& mappings() const noexcept { return learn_.mappings(); }
    bool updateMapping (const midi::MidiMapping&);
    void removeMapping (int paramIndex);
    void clearMappings();
    std::uint32_t learnRevision() const noexcept { return learn_.revision(); }
    /// Fills the context-menu hooks of a control. Does nothing for parameters that cannot be learned.
    void installHooks (const juce::String& paramId, ContextHooks&);

    // ---- misc ----
    void markPatchEdited();      ///< re-evaluates the dirty flag on the next tick
    void refreshNow();           ///< runs the periodic work immediately (tests)
    juce::String versionText() const;
    juce::String latencyText() const;
    int latencySamples() const;

private:
    // juce::AudioProcessorParameter::Listener
    void parameterValueChanged (int parameterIndex, float newValue) override;
    void parameterGestureChanged (int parameterIndex, bool starting) override;
    void timerCallback() override;

    static std::int64_t nowMs() noexcept { return static_cast<std::int64_t> (juce::Time::getMillisecondCounter()); }
    int toTableIndex (int apvtsIndex) const;
    void publishTransaction (const juce::String& label, const ParamSnapshot& newState);
    void syncPresetInfoFromBrowser();
    void initialiseBrowserFromProcessor();
    void loadPresetNow (const std::string& id, bool fromAudition);
    void saveCurrentAs (const juce::String& name, const juce::String& author, const juce::String& tagsCsv, const juce::String& description,
                        bool overwrite);
    PresetData presetDataFromCurrent (const juce::String& name, const juce::String& author, const juce::String& tagsCsv,
                                      const juce::String& description) const;
    void commitMappings();
    void pollLearn();
    void handleConflict (const midi::MidiLearnModel::CaptureResult&);
    void persistRandomState();
    void playAuditionNote();
    void releaseAuditionNote();
    static juce::String plainSaveError (const SaveResult&);

    struct Signature
    {
        std::uint64_t undoRevision = 0;
        bool canUndo = false, canRedo = false, abEngaged = false, modified = false, auditioning = false, armed = false;
        int abSide = 0, activeVariation = 0, loadedHash = 0;
        std::array<std::string, 4> variationNames;
        std::array<bool, 4> variationEmpty {};
        std::uint32_t learnRevision = 0;
        std::uint64_t restoreSequence = 0;
        bool operator== (const Signature&) const = default;
    };
    Signature makeSignature() const;

    SnaralAudioProcessor& proc;
    OptionsModel& opts;
    EditorPrefs& prefs_;
    juce::ListenerList<Listener> listeners;

    std::array<std::atomic<float>*, static_cast<std::size_t> (parameterCount)> raw_ {};
    std::vector<int> tableIndexOfApvts;   // APVTS/processor parameter index -> snaral::P::Index (-1 unknown)
    std::atomic<bool> pendingSync_ { false };
    std::atomic<bool> dirtyCheck_ { true };
    Signature lastSignature_;
    std::uint64_t seenRestoreSequence_ = ~0ull;

    midi::MidiLearnModel learn_;
    std::uint32_t seed_ = 0;
    bool haveSeed_ = false;
    rnd::LockMask locks_ = rnd::lockNone;

    std::int64_t auditionNoteOffAt_ = 0;
    int auditionNote_ = -1;
    ParamSnapshot auditionOriginal_;
    std::string auditionLoadedId_;

    std::unique_ptr<juce::FileChooser> chooser_;
    bool destroyed_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EditorController)
};
} // namespace snaral::ui
