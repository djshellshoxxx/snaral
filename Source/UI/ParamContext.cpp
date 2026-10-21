#include "ParamContext.h"

namespace snaral::ui
{
juce::String contextActionLabel (ContextAction a)
{
    switch (a)
    {
        case ContextAction::Reset:            return "Reset to default";
        case ContextAction::CopyValue:        return "Copy value";
        case ContextAction::PasteValue:       return "Paste value";
        case ContextAction::EnterValue:       return "Enter value...";
        case ContextAction::MidiLearn:        return "MIDI learn";
        case ContextAction::ClearMidiMapping: return "Unlink MIDI mapping";
        case ContextAction::RandomizeThis:    return "Randomize this control";
    }
    return {};
}

std::vector<ContextAction> availableContextActions (const ContextMenuSpec& s)
{
    std::vector<ContextAction> out;
    if (s.canReset)       out.push_back (ContextAction::Reset);
    if (s.canCopyPaste)
    {
        out.push_back (ContextAction::CopyValue);
        out.push_back (ContextAction::PasteValue);
    }
    if (s.canEnterValue)  out.push_back (ContextAction::EnterValue);
    if (s.hooks != nullptr)
    {
        if (s.hooks->onMidiLearn)
            out.push_back (ContextAction::MidiLearn);
        if (s.hooks->onClearMidiMapping && (! s.hooks->isLearned || s.hooks->isLearned()))
            out.push_back (ContextAction::ClearMidiMapping);
        if (s.hooks->onRandomizeThis)
            out.push_back (ContextAction::RandomizeThis);
    }
    return out;
}

juce::PopupMenu buildContextMenu (const ContextMenuSpec& spec)
{
    juce::PopupMenu menu;
    ContextAction previousGroupLast = ContextAction::Reset;
    bool first = true;
    auto group = [] (ContextAction a)
    {
        switch (a)
        {
            case ContextAction::Reset:
            case ContextAction::CopyValue:
            case ContextAction::PasteValue:
            case ContextAction::EnterValue:        return 0;
            case ContextAction::MidiLearn:
            case ContextAction::ClearMidiMapping:  return 1;
            case ContextAction::RandomizeThis:     return 2;
        }
        return 0;
    };

    for (const auto a : availableContextActions (spec))
    {
        if (! first && group (a) != group (previousGroupLast))
            menu.addSeparator();
        const bool ticked = a == ContextAction::MidiLearn && spec.hooks != nullptr && spec.hooks->isLearnArmed
                            && spec.hooks->isLearnArmed();
        menu.addItem (static_cast<int> (a) + 1, contextActionLabel (a), true, ticked);
        previousGroupLast = a;
        first = false;
    }
    return menu;
}

void showContextMenu (juce::Component& target, const ContextMenuSpec& spec, juce::Point<int> screenPosition)
{
    auto menu = buildContextMenu (spec);
    auto options = juce::PopupMenu::Options().withTargetComponent (&target).withMinimumWidth (180);
    if (screenPosition != juce::Point<int>())
        options = options.withTargetScreenArea (juce::Rectangle<int> (screenPosition, screenPosition).expanded (1));

    juce::Component::SafePointer<juce::Component> safe (&target);
    auto perform = spec.perform;
    menu.showMenuAsync (options, [safe, perform] (int result)
    {
        if (result > 0 && safe != nullptr && perform)
            perform (static_cast<ContextAction> (result - 1));
    });
}

namespace
{
std::function<juce::String()>& readerFn()
{
    static std::function<juce::String()> f = [] { return juce::SystemClipboard::getTextFromClipboard(); };
    return f;
}
std::function<void (const juce::String&)>& writerFn()
{
    static std::function<void (const juce::String&)> f = [] (const juce::String& s) { juce::SystemClipboard::copyTextToClipboard (s); };
    return f;
}
} // namespace

juce::String Clipboard::read() { return readerFn()(); }
void Clipboard::write (const juce::String& s) { writerFn()(s); }

void Clipboard::setProvider (std::function<juce::String()> r, std::function<void (const juce::String&)> w)
{
    readerFn() = std::move (r);
    writerFn() = std::move (w);
}

void Clipboard::resetProvider()
{
    readerFn() = [] { return juce::SystemClipboard::getTextFromClipboard(); };
    writerFn() = [] (const juce::String& s) { juce::SystemClipboard::copyTextToClipboard (s); };
}
} // namespace snaral::ui
