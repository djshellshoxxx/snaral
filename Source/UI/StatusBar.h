#pragma once

#include "Theme.h"

namespace snaral::ui
{
/// Footer strip: transient message (icon + text), persistent status chips (e.g. "Host tempo unavailable"), and the
/// version string at the right in muted 9 px monospace (baseline 2.2). Every status has a distinct icon shape and a
/// severity word, so colour is never the only cue. Messages are also announced to screen readers.
class StatusBar : public juce::Component,
                  private juce::Timer
{
public:
    StatusBar();
    ~StatusBar() override;

    static constexpr int preferredHeight = 26;

    /// Shows a message; ttlMs <= 0 keeps it until replaced or cleared.
    void post (Severity, const juce::String& text, int ttlMs = 5000);
    void clearMessage();
    const juce::String& getMessage() const noexcept { return message; }
    Severity getMessageSeverity() const noexcept { return messageSeverity; }

    /// Persistent chips identified by key (set again to update, clear to remove).
    void setChip (const juce::String& key, Severity, const juce::String& text);
    void clearChip (const juce::String& key);
    bool hasChip (const juce::String& key) const;
    int getNumChips() const noexcept { return static_cast<int> (chips.size()); }

    void setVersionText (const juce::String&);
    const juce::String& getVersionText() const noexcept { return version; }

    void paint (juce::Graphics&) override;

private:
    struct Chip
    {
        juce::String key;
        Severity severity;
        juce::String text;
    };

    void timerCallback() override;

    juce::String message, version;
    Severity messageSeverity = Severity::Info;
    juce::int64 expiresAtMs = 0;
    std::vector<Chip> chips;
};

/// Transient notifications stacked at the bottom-right of its bounds (add it over the editor, same size). Clicking a
/// toast dismisses it. Success/Info expire after ~4 s, warnings/errors stay a little longer; with animations off there is
/// no fade at all. Mouse events outside the toasts pass through to the editor beneath.
class ToastHost : public juce::Component,
                  private juce::Timer
{
public:
    ToastHost();
    ~ToastHost() override;

    static constexpr int maxVisible = 3;

    void show (Severity, const juce::String& text, int ttlMs = 0);
    int getNumToasts() const noexcept { return static_cast<int> (toasts.size()); }
    juce::String getToastText (int index) const;
    void dismissAll();

    void paint (juce::Graphics&) override;
    bool hitTest (int x, int y) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    struct Toast
    {
        Severity severity;
        juce::String text;
        juce::int64 expiresAtMs;
        juce::int64 shownAtMs;
    };

    juce::Rectangle<int> boundsOf (int index) const;
    void timerCallback() override;

    std::vector<Toast> toasts;
};
} // namespace snaral::ui
