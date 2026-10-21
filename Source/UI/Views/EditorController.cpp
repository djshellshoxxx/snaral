#include "EditorController.h"

#include "Core/Randomizer.h"
#include "Core/WavWriter.h"
#include "State/Pure/AtomicFile.h"
#include "State/Pure/PresetFormat.h"
#include "UI/ParamFormat.h"

#include <algorithm>

namespace snaral::ui
{
namespace
{
constexpr int auditionNoteNumber = 36;   // C2: a representative bass note for the audition
constexpr int auditionNoteMs = 900;

juce::String stdToJuce (const std::string& s) { return juce::String::fromUTF8 (s.data(), static_cast<int> (s.size())); }
std::string juceToStd (const juce::String& s) { return s.toStdString(); }

} // namespace

EditorController::EditorController (SnaralAudioProcessor& p, OptionsModel& o, EditorPrefs& prefs)
    : proc (p), opts (o), prefs_ (prefs)
{
    auto& tree = apvts();
    tableIndexOfApvts.assign (static_cast<std::size_t> (proc.getParameters().processor.getParameters().size()), -1);
    for (int i = 0; i < parameterCount; ++i)
    {
        const auto idView = parameterTable[static_cast<std::size_t> (i)].id;
        const auto id = toJuce (idView);
        raw_[static_cast<std::size_t> (i)] = tree.getRawParameterValue (id);
        if (auto* prm = tree.getParameter (id))
        {
            const int ai = prm->getParameterIndex();
            if (ai >= 0 && ai < static_cast<int> (tableIndexOfApvts.size()))
                tableIndexOfApvts[static_cast<std::size_t> (ai)] = i;
            prm->addListener (this);
        }
    }

    locks_ = static_cast<rnd::LockMask> (prefs_.getInt ("rndLocks", 0)) & rnd::lockAll;
    {
        const auto stored = proc.uiTree().getProperty ("rndSeed").toString();
        std::uint32_t s = 0;
        if (stored.isNotEmpty() && rnd::seedFromHex (stored.toStdString(), s))
        {
            seed_ = s;
            haveSeed_ = true;
        }
    }

    proc.undoHistory().sync (snapshot());
    initialiseBrowserFromProcessor();
    {
        std::string err;
        learn_.load ({ proc.midiMappings() }, &err);
    }
    seenRestoreSequence_ = proc.restoreSequence();
    lastSignature_ = makeSignature();
    startTimerHz (30);
}

EditorController::~EditorController()
{
    destroyed_ = true;
    stopTimer();
    if (learn_.isArmed())
        cancelLearn();
    releaseAuditionNote();
    if (proc.presetBrowser().auditionActive())
        cancelAudition();
    for (int i = 0; i < parameterCount; ++i)
        if (auto* prm = apvts().getParameter (toJuce (parameterTable[static_cast<std::size_t> (i)].id)))
            prm->removeListener (this);
    chooser_.reset();
}

// ---------------------------------------------------------------------------------------------------------------
void EditorController::status (Severity s, const juce::String& text, int ttlMs)
{
    if (onStatus)
        onStatus (s, text, ttlMs);
}

ParamSnapshot EditorController::snapshot() const
{
    return project::snapshotFromApvts (const_cast<EditorController*> (this)->apvts());
}

juce::String EditorController::versionText() const
{
    return juce::String (JucePlugin_VersionString);
}

int EditorController::latencySamples() const
{
    return proc.diagnostics().latencySamples();
}

juce::String EditorController::latencyText() const
{
    const int n = latencySamples();
    const double sr = proc.getSampleRate() > 1.0 ? proc.getSampleRate() : 48000.0;
    return "Latency: " + juce::String (n) + " samples (" + juce::String (1000.0 * n / sr, 1) + " ms)";
}

int EditorController::toTableIndex (int apvtsIndex) const
{
    return apvtsIndex >= 0 && apvtsIndex < static_cast<int> (tableIndexOfApvts.size())
               ? tableIndexOfApvts[static_cast<std::size_t> (apvtsIndex)] : -1;
}

// ---- parameter listener --------------------------------------------------------------------------------------
void EditorController::parameterValueChanged (int parameterIndex, float newNormalised)
{
    dirtyCheck_.store (true, std::memory_order_relaxed);
    if (! juce::MessageManager::existsAndIsCurrentThread())
    {
        pendingSync_.store (true, std::memory_order_relaxed); // automation from a host thread: never a UI edit
        return;
    }
    const int ti = toTableIndex (parameterIndex);
    if (ti < 0)
        return;
    const auto param = static_cast<P::Index> (ti);
    // JUCE calls listeners newest-first, so the APVTS raw value may not be updated yet: convert the notified value instead.
    if (proc.undoHistory().gestureOpen (param))
        proc.undoHistory().edit (param, parameterTable[static_cast<std::size_t> (ti)].fromNormalized (newNormalised), nowMs());
    else
        pendingSync_.store (true, std::memory_order_relaxed); // MIDI-learn CC, host automation, snapshot publish
}

void EditorController::parameterGestureChanged (int parameterIndex, bool starting)
{
    if (! juce::MessageManager::existsAndIsCurrentThread())
        return;
    const int ti = toTableIndex (parameterIndex);
    if (ti < 0)
        return;
    if (starting)
        proc.undoHistory().beginGesture (static_cast<P::Index> (ti), nowMs());
    else
        proc.undoHistory().endGesture (static_cast<P::Index> (ti), nowMs());
    dirtyCheck_.store (true, std::memory_order_relaxed);
}

void EditorController::markPatchEdited() { dirtyCheck_.store (true, std::memory_order_relaxed); }

// ---- periodic work ------------------------------------------------------------------------------------------
EditorController::Signature EditorController::makeSignature() const
{
    Signature s;
    const auto& u = proc.undoHistory();
    s.undoRevision = u.revision();
    s.canUndo = u.canUndo();
    s.canRedo = u.canRedo();
    s.abEngaged = proc.abCompare().engaged();
    s.abSide = static_cast<int> (proc.abCompare().activeSide());
    const auto& bank = proc.variationBank();
    s.activeVariation = bank.activeIndex();
    for (int i = 0; i < variationSlotCount; ++i)
    {
        s.variationNames[static_cast<std::size_t> (i)] = bank.slot (i).name;
        s.variationEmpty[static_cast<std::size_t> (i)] = bank.isEmpty (i);
    }
    s.modified = proc.presetBrowser().hasUnsavedChanges();
    s.auditioning = proc.presetBrowser().auditionActive();
    s.armed = learn_.isArmed();
    s.learnRevision = learn_.revision();
    s.restoreSequence = proc.restoreSequence();
    s.loadedHash = stdToJuce (proc.presetBrowser().displayName()).hashCode();
    return s;
}

void EditorController::refreshNow() { timerCallback(); }

void EditorController::timerCallback()
{
    if (destroyed_)
        return;

    // project restored while the editor is open (host loaded a session): re-read everything derived from state
    if (const auto seq = proc.restoreSequence(); seq != seenRestoreSequence_ && (seq & 1u) == 0u)
    {
        seenRestoreSequence_ = seq;
        proc.undoHistory().sync (snapshot());
        initialiseBrowserFromProcessor();
        std::string err;
        learn_.load ({ proc.midiMappings() }, &err);
        {
            const auto stored = proc.uiTree().getProperty ("rndSeed").toString();
            std::uint32_t s = 0;
            if (stored.isNotEmpty() && rnd::seedFromHex (stored.toStdString(), s)) { seed_ = s; haveSeed_ = true; }
        }
    }

    if (proc.presetBrowser().auditionActive())
    {
        pendingSync_.store (false); // audition edits are not history and not dirty
        dirtyCheck_.store (false);
    }
    else
    {
        if (pendingSync_.exchange (false, std::memory_order_relaxed))
            proc.undoHistory().sync (snapshot());
        if (dirtyCheck_.exchange (false, std::memory_order_relaxed))
        {
            auto& b = proc.presetBrowser();
            b.notifyEdited (snapshot());
            proc.presetInfo().modified = b.hasUnsavedChanges();
        }
    }

    if (auditionNote_ >= 0 && nowMs() >= auditionNoteOffAt_)
        releaseAuditionNote();

    proc.applyPendingLatency();
    if (learn_.isArmed() || learn_.hasPendingChoice())
        pollLearn();

    if (const auto sig = makeSignature(); ! (sig == lastSignature_))
    {
        lastSignature_ = sig;
        listeners.call ([] (Listener& l) { l.controllerStateChanged(); });
    }
}

// ---- history -------------------------------------------------------------------------------------------------
juce::String EditorController::undoLabel() const { return stdToJuce (proc.undoHistory().undoLabel()); }
juce::String EditorController::redoLabel() const { return stdToJuce (proc.undoHistory().redoLabel()); }

void EditorController::publishTransaction (const juce::String& label, const ParamSnapshot& newState)
{
    proc.undoHistory().recordTransaction (juceToStd (label), newState, nowMs());
    proc.applySoundSnapshot (newState);
    pendingSync_.store (true);
    dirtyCheck_.store (true);
}

void EditorController::undo()
{
    auto state = snapshot();
    const auto label = undoLabel();
    if (! proc.undoHistory().undo (state))
    {
        status (Severity::Info, "Nothing to undo.", 2500);
        return;
    }
    proc.applySoundSnapshot (state);
    dirtyCheck_.store (true);
    status (Severity::Info, "Undid: " + label, 3000);
}

void EditorController::redo()
{
    auto state = snapshot();
    const auto label = redoLabel();
    if (! proc.undoHistory().redo (state))
    {
        status (Severity::Info, "Nothing to redo.", 2500);
        return;
    }
    proc.applySoundSnapshot (state);
    dirtyCheck_.store (true);
    status (Severity::Info, "Redid: " + label, 3000);
}

// ---- common actions ------------------------------------------------------------------------------------------
void EditorController::panic()
{
    proc.panic();
    proc.referenceTone().stop();
    if (onPanicPerformed)
        onPanicPerformed();
    status (Severity::Info, "Panic: all sound stopped.", 2500);
}

void EditorController::resetPatch()
{
    auto& b = proc.presetBrowser();
    if (b.auditionActive())
        cancelAudition();
    const auto cur = snapshot();
    const auto init = rnd::initializedPatch (cur);
    publishTransaction ("Reset", init);
    b.notifyLoaded ("", "Initialized", init);
    proc.presetInfo() = { "Initialized", "", false };
    status (Severity::Success, "Reset to the initialized patch. Undo brings the old sound back.");
}

void EditorController::persistRandomState()
{
    proc.uiTree().setProperty ("rndSeed", juce::String (rnd::seedToHex (seed_)), nullptr);
    proc.uiTree().setProperty ("rndAlgorithm", rnd::algorithmVersion, nullptr);
}

void EditorController::randomize()
{
    seed_ = rnd::seedFromClock();
    haveSeed_ = true;
    reproduceRandomize();
}

void EditorController::reproduceRandomize()
{
    if (! haveSeed_)
    {
        randomize();
        return;
    }
    const auto cur = snapshot();
    const auto res = rnd::randomize (cur, seed_, locks_);
    if (! res.ok)
    {
        status (Severity::Error, "Randomize did nothing: the current sound has a value outside its range. " + stdToJuce (res.error), 8000);
        return;
    }
    publishTransaction ("Randomize", res.snapshot);
    persistRandomState();
    const auto hex = stdToJuce (rnd::seedToHex (seed_));
    juce::Logger::writeToLog ("Snaral randomize: algorithm " + juce::String (rnd::algorithmVersion) + ", seed " + hex + ", locks "
                              + juce::String (static_cast<int> (locks_)) + ", " + juce::String (res.changedCount) + " parameters changed");
    status (Severity::Success, "Randomized " + juce::String (res.changedCount) + " parameters (seed " + hex + "). Undo restores the old sound.");
}

juce::String EditorController::seedHex() const
{
    return haveSeed_ ? stdToJuce (rnd::seedToHex (seed_)) : juce::String ("--------");
}

bool EditorController::setSeedFromText (const juce::String& hex)
{
    std::uint32_t s = 0;
    if (! rnd::seedFromHex (hex.trim().toStdString(), s))
    {
        status (Severity::Warning, "A seed is 1 to 8 hexadecimal digits, for example 1a2b3c4d.", 6000);
        return false;
    }
    seed_ = s;
    haveSeed_ = true;
    persistRandomState();
    return true;
}

void EditorController::setRandomizeLocks (rnd::LockMask m)
{
    locks_ = m & rnd::lockAll;
    prefs_.set ("rndLocks", static_cast<int> (locks_));
}

// ---- A/B ----------------------------------------------------------------------------------------------------
void EditorController::selectAb (AbCompare::Side side)
{
    auto& ab = proc.abCompare();
    const auto cur = snapshot();
    if (! ab.engaged())
        ab.engage (cur);
    if (ab.activeSide() == side)
    {
        status (Severity::Info, juce::String (side == AbCompare::Side::A ? "A" : "B") + " is already active.", 2000);
        return;
    }
    ParamSnapshot out;
    if (! ab.toggle (cur, out))
        return;
    proc.applySoundSnapshot (out);
    proc.undoHistory().sync (out);   // an A/B switch is not an undo step
    pendingSync_.store (true);
    dirtyCheck_.store (true);
    status (Severity::Info, juce::String ("Now comparing ") + (side == AbCompare::Side::A ? "A" : "B") + ".", 3000);
}

void EditorController::copyAToB()
{
    auto& ab = proc.abCompare();
    const auto cur = snapshot();
    if (! ab.engaged())
        ab.engage (cur);
    const bool onA = ab.activeSide() == AbCompare::Side::A;
    ab.copyToOther (cur);
    status (Severity::Success, onA ? "Copied A to B." : "Copied B to A.");
}

void EditorController::copyBToA()
{
    auto& ab = proc.abCompare();
    const auto cur = snapshot();
    if (! ab.engaged())
        ab.engage (cur);
    ParamSnapshot out;
    if (! ab.copyFromOther (cur, out))
        return;
    publishTransaction ("Copy to " + juce::String (ab.activeSide() == AbCompare::Side::A ? "A" : "B"), out);
    status (Severity::Success, "Copied the other side into the active side. Undo is available.");
}

// ---- variation slots ----------------------------------------------------------------------------------------
juce::String EditorController::variationLabel (int slot) const
{
    return stdToJuce (proc.variationBank().slot (slot).name);
}

void EditorController::recallVariation (int slot)
{
    ParamSnapshot out;
    const auto name = variationLabel (slot);
    if (! proc.variationBank().recall (slot, snapshot(), out))
    {
        status (Severity::Info, "Variation " + name + " is empty. Store the current sound first.", 4000);
        return;
    }
    publishTransaction ("Recall Variation " + name, out);
    status (Severity::Success, "Recalled variation " + name + ".");
}

void EditorController::storeVariation (int slot)
{
    if (proc.variationBank().store (slot, snapshot()))
        status (Severity::Success, "Stored the current sound in variation " + variationLabel (slot) + ".");
}

void EditorController::clearVariation (int slot)
{
    if (proc.variationBank().clear (slot))
        status (Severity::Info, "Cleared variation " + variationLabel (slot) + ".");
}

void EditorController::renameVariation (int slot)
{
    if (! showDialog)
        return;
    DialogSpec d;
    d.title = "Rename variation";
    d.message = "Up to " + juce::String (static_cast<int> (maxVariationNameChars)) + " characters. Leave it empty to use the letter.";
    d.fields = { { "Name", variationLabel (slot), false } };
    d.buttons = { "Rename", "Cancel" };
    d.onResult = [this, slot] (int button, const juce::StringArray& values)
    {
        if (button != 0 || values.isEmpty())
            return;
        if (! proc.variationBank().rename (slot, juceToStd (values[0])))
            status (Severity::Warning, "That name is not allowed. Use up to 32 printable characters.", 6000);
    };
    showDialog (std::move (d));
}

// ---- presets ------------------------------------------------------------------------------------------------
juce::String EditorController::presetName() const
{
    const auto n = proc.presetBrowser().displayName();
    return n.empty() ? juce::String ("Init") : stdToJuce (n);
}

bool EditorController::isModified() const { return proc.presetBrowser().hasUnsavedChanges(); }

void EditorController::rescanLibrary()
{
    const auto report = proc.presetLibrary().rescan();
    if (! report.skipped.empty())
        status (Severity::Warning, juce::String (static_cast<int> (report.skipped.size())) + " preset file(s) were skipped: "
                                       + stdToJuce (report.skipped.front().path) + " (" + stdToJuce (report.skipped.front().message) + ")", 9000);
    for (const auto& n : report.notes)
        status (Severity::Warning, stdToJuce (n.path) + ": " + stdToJuce (n.message), 9000);
}

void EditorController::initialiseBrowserFromProcessor()
{
    auto& lib = proc.presetLibrary();
    if (lib.entries().empty())
        rescanLibrary();

    auto& b = proc.presetBrowser();
    const auto info = proc.presetInfo();
    const auto cur = snapshot();
    ParamSnapshot reference = cur;
    bool haveReference = false;
    if (! info.id.empty() && lib.find (info.id) != nullptr)
    {
        const auto r = lib.loadPreset (info.id);
        if (r.ok)
        {
            reference = withSoundParams (cur, r.data.params);
            haveReference = true;
        }
    }
    else if (info.name.empty() || info.name == "Initialized")
    {
        reference = rnd::initializedPatch (cur);
        haveReference = true;
    }
    b.notifyLoaded (info.id, info.name.empty() ? "Init" : info.name, reference);
    if (haveReference)
        b.notifyEdited (cur);
    else
        b.setModified (info.modified);
    syncPresetInfoFromBrowser();
}

void EditorController::syncPresetInfoFromBrowser()
{
    auto& b = proc.presetBrowser();
    auto& info = proc.presetInfo();
    info.id = b.loadedId();
    info.name = b.loadedName() == "Init" ? std::string() : b.loadedName();
    info.modified = b.hasUnsavedChanges();
}

void EditorController::requestLoadPreset (const std::string& id)
{
    auto& b = proc.presetBrowser();
    if (b.auditionActive())
    {
        cancelAudition();
    }
    if (b.needsConfirmationBeforeLoad() && showDialog)
    {
        DialogSpec d;
        d.title = "Unsaved changes";
        d.message = "\"" + presetName() + "\" has changes that are not saved. Loading another preset replaces them. "
                    "You can still undo the load afterwards.";
        d.buttons = { "Load anyway", "Cancel" };
        d.onResult = [this, id] (int button, const juce::StringArray&)
        {
            if (button == 0)
                loadPresetNow (id, false);
        };
        showDialog (std::move (d));
        return;
    }
    loadPresetNow (id, false);
}

void EditorController::loadPresetNow (const std::string& id, bool)
{
    auto& lib = proc.presetLibrary();
    auto& b = proc.presetBrowser();
    const auto* entry = lib.find (id);
    if (entry == nullptr)
    {
        status (Severity::Error, "That preset is no longer in the library. Rescan the library from the browser.", 8000);
        return;
    }
    const auto name = stdToJuce (entry->name);
    const auto r = lib.loadPreset (id);
    if (! r.ok)
    {
        status (Severity::Error, "Could not load \"" + name + "\": " + stdToJuce (r.firstError()) + ". Your current sound was not changed.", 10000);
        return;
    }
    const auto applied = withSoundParams (snapshot(), r.data.params);
    publishTransaction ("Load Preset " + name, applied);
    b.notifyLoaded (id, entry->name, applied);
    b.select (id);
    lib.markUsed (id, juce::Time::currentTimeMillis() / 1000);
    lib.saveBrowserState();
    syncPresetInfoFromBrowser();
    status (Severity::Success, "Loaded preset: " + name);
}

void EditorController::stepPreset (int direction)
{
    auto& b = proc.presetBrowser();
    if (b.resultCount() == 0)
    {
        status (Severity::Info, "No presets match the current filters.", 4000);
        return;
    }
    if (b.selectedId().empty() || ! b.select (b.loadedId()))
    {
        b.select (b.entryAt (direction > 0 ? 0 : b.resultCount() - 1).id);
        requestLoadPreset (b.selectedId());
        return;
    }
    if (direction > 0)
        b.selectNext (true);
    else
        b.selectPrevious (true);
    requestLoadPreset (b.selectedId());
}

void EditorController::toggleFavourite (const std::string& id)
{
    auto& lib = proc.presetLibrary();
    const bool now = ! lib.isFavourite (id);
    const auto r = lib.setFavourite (id, now);
    if (! r.ok)
        status (Severity::Error, "Could not save your favourites: " + stdToJuce (r.error), 8000);
    listeners.call ([] (Listener& l) { l.controllerStateChanged(); });
}

// ---- audition -----------------------------------------------------------------------------------------------
void EditorController::playAuditionNote()
{
    releaseAuditionNote();
    proc.queueUiNote (auditionNoteNumber, 100, true);
    auditionNote_ = auditionNoteNumber;
    auditionNoteOffAt_ = nowMs() + auditionNoteMs;
}

void EditorController::releaseAuditionNote()
{
    if (auditionNote_ >= 0)
        proc.queueUiNote (auditionNote_, 64, false);
    auditionNote_ = -1;
}

void EditorController::beginAudition (const std::string& id)
{
    auto& b = proc.presetBrowser();
    auto& lib = proc.presetLibrary();
    const auto* entry = lib.find (id);
    if (entry == nullptr)
        return;
    const auto r = lib.loadPreset (id);
    if (! r.ok)
    {
        status (Severity::Error, "Cannot audition \"" + stdToJuce (entry->name) + "\": " + stdToJuce (r.firstError()), 8000);
        return;
    }
    if (! b.auditionActive())
    {
        auditionOriginal_ = snapshot();
        b.beginAudition (auditionOriginal_);
    }
    b.setAuditionTarget (id);
    auditionLoadedId_ = id;
    proc.applySoundSnapshot (withSoundParams (snapshot(), r.data.params));   // not a history step, not dirty
    playAuditionNote();
    status (Severity::Info, "Auditioning \"" + stdToJuce (entry->name) + "\". Apply keeps it, Cancel restores your sound.", 6000);
}

void EditorController::confirmAudition()
{
    auto& b = proc.presetBrowser();
    if (! b.auditionActive() || auditionLoadedId_.empty())
        return;
    const auto id = auditionLoadedId_;
    const auto* entry = proc.presetLibrary().find (id);
    const auto applied = snapshot();
    const auto name = entry != nullptr ? entry->name : std::string ("Preset");
    releaseAuditionNote();
    // history: one "Load Preset" step from the sound the user had before the audition
    proc.undoHistory().sync (auditionOriginal_);
    proc.undoHistory().recordTransaction ("Load Preset " + name, applied, nowMs());
    b.confirmAudition (id, name, applied);
    proc.presetLibrary().markUsed (id, juce::Time::currentTimeMillis() / 1000);
    proc.presetLibrary().saveBrowserState();
    syncPresetInfoFromBrowser();
    auditionLoadedId_.clear();
    dirtyCheck_.store (true);
    status (Severity::Success, "Applied preset: " + stdToJuce (name));
}

void EditorController::cancelAudition()
{
    auto& b = proc.presetBrowser();
    if (! b.auditionActive())
        return;
    releaseAuditionNote();
    if (auto restore = b.cancelAudition())
    {
        proc.applySoundSnapshot (restore->snapshot);
        proc.undoHistory().sync (restore->snapshot);
        b.notifyEdited (restore->snapshot);
        b.setModified (restore->modified);
        proc.presetInfo() = { restore->loadedName == "Init" ? std::string() : restore->loadedName, restore->loadedId, restore->modified };
    }
    auditionLoadedId_.clear();
    pendingSync_.store (true);
    status (Severity::Info, "Audition cancelled. Your sound is back.", 3500);
}

// ---- save / import / export ---------------------------------------------------------------------------------
juce::String EditorController::plainSaveError (const SaveResult& r)
{
    juce::String t = stdToJuce (r.message);
    if (! r.issues.empty())
        t << " (" << stdToJuce (describe (r.issues.front())) << ")";
    if (t.isEmpty())
        t = "The preset could not be written.";
    return t;
}

PresetData EditorController::presetDataFromCurrent (const juce::String& name, const juce::String& author, const juce::String& tagsCsv,
                                                    const juce::String& description) const
{
    PresetData d;
    d.name = juceToStd (name.trim());
    d.author = juceToStd (author.trim());
    d.description = juceToStd (description.trim());
    juce::StringArray tags;
    tags.addTokens (tagsCsv, ",", "");
    for (auto t : tags)
    {
        t = t.trim();
        if (t.isNotEmpty())
            d.tags.push_back (juceToStd (t));
    }
    d.params = snapshot();
    return d;
}

void EditorController::saveCurrentAs (const juce::String& name, const juce::String& author, const juce::String& tagsCsv,
                                      const juce::String& description, bool overwrite)
{
    if (name.trim().isEmpty())
    {
        status (Severity::Warning, "Give the preset a name before saving.", 6000);
        return;
    }
    auto data = presetDataFromCurrent (name, author, tagsCsv, description);
    auto& lib = proc.presetLibrary();
    const auto res = lib.saveUserPreset (data, overwrite);
    switch (res.status)
    {
        case SaveStatus::Saved:
        {
            auto& b = proc.presetBrowser();
            const auto cur = snapshot();
            b.notifyLoaded (res.id, data.name, cur);
            b.select (res.id);
            syncPresetInfoFromBrowser();
            prefs_.set ("author", stdToJuce (data.author));
            status (Severity::Success, "Saved preset \"" + stdToJuce (data.name) + "\" to your library.");
            break;
        }
        case SaveStatus::OverwriteRequired:
        {
            if (! showDialog)
                break;
            DialogSpec d;
            d.title = "Replace preset?";
            d.message = "A preset called \"" + stdToJuce (data.name) + "\" already exists in your library. Replacing it cannot be undone.";
            d.buttons = { "Replace", "Cancel" };
            d.onResult = [this, name, author, tagsCsv, description] (int button, const juce::StringArray&)
            {
                if (button == 0)
                    saveCurrentAs (name, author, tagsCsv, description, true);
            };
            showDialog (std::move (d));
            break;
        }
        case SaveStatus::Invalid:
        case SaveStatus::IoError:
            status (Severity::Error, "Not saved: " + plainSaveError (res) + " Your sound is unchanged.", 12000);
            break;
    }
}

void EditorController::savePreset()
{
    const auto& id = proc.presetBrowser().loadedId();
    const auto* entry = id.empty() ? nullptr : proc.presetLibrary().find (id);
    if (entry != nullptr && entry->origin == PresetOrigin::User)
    {
        juce::String tags;
        for (const auto& t : entry->tags)
            tags << (tags.isEmpty() ? "" : ", ") << stdToJuce (t);
        saveCurrentAs (stdToJuce (entry->name), stdToJuce (entry->author), tags, stdToJuce (entry->description), true);
        return;
    }
    savePresetAs();
}

void EditorController::savePresetAs()
{
    if (! showDialog)
        return;
    const auto& id = proc.presetBrowser().loadedId();
    const auto* entry = id.empty() ? nullptr : proc.presetLibrary().find (id);
    juce::String name = presetName(), tags, desc;
    if (name == "Init" || name == "Initialized")
        name = {};
    if (entry != nullptr)
    {
        for (const auto& t : entry->tags)
            tags << (tags.isEmpty() ? "" : ", ") << stdToJuce (t);
        desc = stdToJuce (entry->description);
        if (entry->origin != PresetOrigin::User)
            name << (name.isEmpty() ? "" : " ") << "(copy)";
    }
    DialogSpec d;
    d.title = "Save preset";
    d.message = "Saved to your user library. Factory presets are never changed.";
    d.fields = { { "Name", name, false }, { "Author", prefs_.getString ("author"), false }, { "Tags (comma separated)", tags, false },
                 { "Description", desc, true } };
    d.buttons = { "Save", "Cancel" };
    d.onResult = [this] (int button, const juce::StringArray& v)
    {
        if (button == 0 && v.size() >= 4)
            saveCurrentAs (v[0], v[1], v[2], v[3], false);
    };
    showDialog (std::move (d));
}

void EditorController::importPreset()
{
    const auto start = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    chooser_ = std::make_unique<juce::FileChooser> ("Import a Snaral preset", start, "*.snaralpreset;*.json");
    chooser_->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                           [this] (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();
        if (file == juce::File() || destroyed_)
            return;
        auto& lib = proc.presetLibrary();
        auto path = fileio::pathFromUtf8 (file.getFullPathName().toStdString());
        auto res = lib.importPresetFile (path, false);
        if (res.status == SaveStatus::OverwriteRequired && showDialog)
        {
            DialogSpec d;
            d.title = "Replace preset?";
            d.message = "Your library already has a preset with the same file name. Replace it with the imported file?";
            d.buttons = { "Replace", "Cancel" };
            d.onResult = [this, path] (int button, const juce::StringArray&)
            {
                if (button != 0)
                    return;
                const auto again = proc.presetLibrary().importPresetFile (path, true);
                if (again.ok())
                    status (Severity::Success, "Imported \"" + stdToJuce (again.id) + "\".");
                else
                    status (Severity::Error, "Import failed: " + plainSaveError (again), 12000);
            };
            showDialog (std::move (d));
            return;
        }
        if (res.ok())
        {
            proc.presetBrowser().select (res.id);
            const auto* e = lib.find (res.id);
            status (Severity::Success, "Imported \"" + (e != nullptr ? stdToJuce (e->name) : file.getFileNameWithoutExtension())
                                            + "\". Find it in the preset browser.");
        }
        else
            status (Severity::Error, "Import failed: " + plainSaveError (res) + " Your sound was not changed.", 12000);
    });
}

void EditorController::exportPreset()
{
    const auto name = presetName();
    const auto slug = juce::String (wav::presetSlug (name.toStdString()));
    const auto start = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile (slug + ".snaralpreset");
    chooser_ = std::make_unique<juce::FileChooser> ("Export the current sound as a preset file", start, "*.snaralpreset");
    chooser_->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                               | juce::FileBrowserComponent::warnAboutOverwriting,
                           [this, name] (const juce::FileChooser& fc)
    {
        auto file = fc.getResult();
        if (file == juce::File() || destroyed_)
            return;
        if (! file.hasFileExtension ("snaralpreset"))
            file = file.withFileExtension ("snaralpreset");
        auto data = presetDataFromCurrent (name == "Init" ? juce::String ("Init") : name, prefs_.getString ("author"), {}, {});
        std::string text;
        std::vector<Issue> errors;
        if (! writePreset (data, text, &errors))
        {
            status (Severity::Error, "Could not export: " + (errors.empty() ? juce::String ("the preset is not valid") : stdToJuce (describe (errors.front()))), 10000);
            return;
        }
        const auto io = fileio::writeFileAtomic (fileio::pathFromUtf8 (file.getFullPathName().toStdString()), text);
        if (io.ok)
            status (Severity::Success, "Exported \"" + file.getFileName() + "\".");
        else
            status (Severity::Error, "Could not export: " + stdToJuce (io.error), 12000);
    });
}

// ---- MIDI learn ---------------------------------------------------------------------------------------------
void EditorController::commitMappings()
{
    std::string err;
    if (! proc.setMidiMappings (learn_.mappings(), &err))
    {
        status (Severity::Error, "MIDI mappings not applied: " + stdToJuce (err), 9000);
        std::string e2;
        learn_.load ({ proc.midiMappings() }, &e2);
    }
}

void EditorController::toggleLearn (const juce::String& paramId)
{
    const int idx = findParameterIndex (paramId.toStdString());
    if (idx < 0 || ! midi::isLearnable (idx))
    {
        status (Severity::Info, "This control cannot be mapped to a MIDI controller.", 4000);
        return;
    }
    if (learn_.isArmed() && learn_.armedTarget() == idx)
    {
        cancelLearn();
        status (Severity::Info, "MIDI Learn cancelled.", 2500);
        return;
    }
    if (learn_.arm (idx) != midi::MidiLearnModel::ArmResult::Armed)
        return;
    proc.ccRouter().setLearnActive (true);
    // drop any CC that arrived before this learn started
    CcSeen stale;
    while (proc.ccRouter().seenQueue().pop (stale)) {}
    status (Severity::Info, "Learning " + toJuce (parameterTable[static_cast<std::size_t> (idx)].name) + ". Move a controller. Esc cancels.", 0);
}

void EditorController::cancelLearn()
{
    if (! learn_.isArmed() && ! learn_.hasPendingChoice())
        return;
    learn_.cancel();
    proc.ccRouter().setLearnActive (false);
}

void EditorController::unlink (const juce::String& paramId)
{
    const int idx = findParameterIndex (paramId.toStdString());
    if (idx >= 0 && learn_.remove (idx))
    {
        commitMappings();
        status (Severity::Info, "Removed the MIDI mapping for " + toJuce (parameterTable[static_cast<std::size_t> (idx)].name) + ".");
    }
}

bool EditorController::isLearned (const juce::String& paramId) const
{
    const int idx = findParameterIndex (paramId.toStdString());
    return idx >= 0 && learn_.indexOfTarget (idx) >= 0;
}

juce::String EditorController::learnDetail (const juce::String& paramId) const
{
    const int idx = findParameterIndex (paramId.toStdString());
    const int at = idx >= 0 ? learn_.indexOfTarget (idx) : -1;
    if (at < 0)
        return {};
    const auto& m = learn_.mappings()[static_cast<std::size_t> (at)];
    juce::String t = "CC " + juce::String (m.cc);
    if (m.channel != 0)
        t << " ch " << m.channel;
    return t;
}

bool EditorController::updateMapping (const midi::MidiMapping& m)
{
    std::string err;
    if (! learn_.update (m, &err))
    {
        status (Severity::Warning, "Mapping not changed: " + stdToJuce (err), 8000);
        return false;
    }
    commitMappings();
    return true;
}

void EditorController::removeMapping (int paramIndex)
{
    if (learn_.remove (paramIndex))
        commitMappings();
}

void EditorController::clearMappings()
{
    learn_.clearAll();
    commitMappings();
    status (Severity::Info, "All MIDI mappings removed.");
}

void EditorController::pollLearn()
{
    if (! learn_.isArmed() || learn_.hasPendingChoice())
        return;
    CcSeen seen;
    int guard = 0;
    while (guard++ < 64 && learn_.isArmed() && ! learn_.hasPendingChoice() && proc.ccRouter().seenQueue().pop (seen))
    {
        const auto res = learn_.onCcCaptured (seen.cc, seen.channel);
        using Kind = midi::MidiLearnModel::CaptureKind;
        switch (res.kind)
        {
            case Kind::NotArmed: break;
            case Kind::Reserved:
                status (Severity::Info, "Reserved MIDI message. Move another controller.", 4000);
                break;
            case Kind::Assigned:
                proc.ccRouter().setLearnActive (false);
                commitMappings();
                status (Severity::Success, stdToJuce (res.status));
                break;
            case Kind::NeedsChoice:
                handleConflict (res);
                break;
            case Kind::LimitReached:
                proc.ccRouter().setLearnActive (false);
                status (Severity::Warning, "16 MIDI mappings is the maximum. Remove one in Options, then try again.", 9000);
                break;
        }
    }
}

void EditorController::handleConflict (const midi::MidiLearnModel::CaptureResult& r)
{
    proc.ccRouter().setLearnActive (false);
    auto describeAll = [] (const std::vector<midi::MidiMapping>& v)
    {
        juce::String t;
        for (const auto& m : v)
            t << (t.isEmpty() ? "" : ", ") << stdToJuce (midi::describeMapping (m));
        return t;
    };
    DialogSpec d;
    d.title = "MIDI mapping conflict";
    const int targetIdx = learn_.armedTarget();
    const auto targetName = targetIdx >= 0 ? toJuce (parameterTable[static_cast<std::size_t> (targetIdx)].name) : juce::String();
    const bool keepBoth = r.conflict.keepBothAvailable && ! r.conflict.ccHolders.empty();
    if (r.conflict.ccHolders.empty())
    {
        d.message = targetName + " is already mapped to a controller. Replace that mapping with the one you just moved?";
        d.buttons = { "Replace", "Cancel" };
    }
    else
    {
        d.message = "That controller is already mapped: " + describeAll (r.conflict.ccHolders) + ". Replace removes the old mapping.";
        if (keepBoth)
            d.message << " Keep both gives each its own MIDI channel.";
        else
            d.message << " Keeping both is not possible: " + stdToJuce (r.conflict.keepBothUnavailableReason);
        d.buttons = keepBoth ? std::vector<juce::String> { "Replace", "Keep both", "Cancel" } : std::vector<juce::String> { "Replace", "Cancel" };
    }
    const int existingChannel = r.conflict.suggestedExistingChannel;
    d.onResult = [this, keepBoth, existingChannel] (int button, const juce::StringArray&)
    {
        using Choice = midi::MidiLearnModel::Choice;
        Choice c = Choice::Cancel;
        if (button == 0) c = Choice::Replace;
        else if (button == 1 && keepBoth) c = Choice::KeepBoth;
        const auto msg = learn_.resolve (c, existingChannel);
        proc.ccRouter().setLearnActive (false);
        commitMappings();
        status (c == Choice::Cancel ? Severity::Info : Severity::Success, stdToJuce (msg).isEmpty() ? juce::String ("MIDI Learn finished.") : stdToJuce (msg));
    };
    if (showDialog)
        showDialog (std::move (d));
    else
    {
        learn_.cancel();
    }
}

void EditorController::installHooks (const juce::String& paramId, ContextHooks& h)
{
    const int idx = findParameterIndex (paramId.toStdString());
    h.onFeedback = [this] (Severity s, const juce::String& t) { status (s, t, s == Severity::Error ? 9000 : 6000); };
    if (idx < 0 || ! midi::isLearnable (idx))
        return;
    h.onMidiLearn = [this, paramId] { toggleLearn (paramId); };
    h.onClearMidiMapping = [this, paramId] { unlink (paramId); };
    h.isLearnArmed = [this, idx] { return learn_.isArmed() && learn_.armedTarget() == idx; };
    h.isLearned = [this, idx] { return learn_.indexOfTarget (idx) >= 0; };
}
} // namespace snaral::ui
