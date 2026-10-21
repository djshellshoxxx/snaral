#pragma once

#include "UiCommon.h"

namespace snaral::ui
{
/// The shared parameter context menu (AUDIO_APP_BASELINE 2.3, cdl-common-actions.md): reset to default, copy value,
/// validated paste, numerical entry, MIDI learn, unlink MIDI mapping. Unavailable actions are omitted, not greyed.
enum class ContextAction
{
    Reset,
    CopyValue,
    PasteValue,
    EnterValue,
    MidiLearn,
    ClearMidiMapping,
    RandomizeThis       ///< not part of cdl-common-actions.md; only shown if the editor supplies onRandomizeThis
};

juce::String contextActionLabel (ContextAction);

/// Callbacks the editor assembly supplies to connect the menu to services owned elsewhere (MIDI learn, randomize,
/// toasts). Every member is optional; an empty callback hides the matching menu entry.
struct ContextHooks
{
    std::function<void()> onMidiLearn;               ///< arm / disarm learn for this control
    std::function<void()> onClearMidiMapping;        ///< unlink the learned mapping
    std::function<void()> onRandomizeThis;           ///< randomize only this control (optional extra)
    std::function<bool()> isLearnArmed;              ///< shows a tick next to "MIDI learn" while armed
    std::function<bool()> isLearned;                 ///< if set, "Unlink MIDI mapping" is shown only while this is true
    std::function<void (Severity, const juce::String&)> onFeedback; ///< invalid paste / input messages (toast or status bar)
};

/// What a given widget supports; `perform` is invoked with the chosen action.
struct ContextMenuSpec
{
    bool canReset = true;
    bool canCopyPaste = true;
    bool canEnterValue = true;
    const ContextHooks* hooks = nullptr;
    std::function<void (ContextAction)> perform;
};

/// The actions that would be listed, in menu order (used by the menu builder and by tests).
std::vector<ContextAction> availableContextActions (const ContextMenuSpec&);

/// Builds the popup (item ids = ContextAction + 1).
juce::PopupMenu buildContextMenu (const ContextMenuSpec&);

/// Shows the menu asynchronously next to `target` and calls spec.perform with the choice. The spec is copied.
void showContextMenu (juce::Component& target, const ContextMenuSpec&, juce::Point<int> screenPosition = {});

/// Clipboard access that tests can replace (the real clipboard is not available headless).
struct Clipboard
{
    static juce::String read();
    static void write (const juce::String&);
    static void setProvider (std::function<juce::String()> reader, std::function<void (const juce::String&)> writer);
    static void resetProvider();
};

/// Convenience: forwards to hooks->onFeedback when set.
inline void sendFeedback (const ContextHooks& h, Severity s, const juce::String& text)
{
    if (h.onFeedback)
        h.onFeedback (s, text);
}
} // namespace snaral::ui
