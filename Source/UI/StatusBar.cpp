#include "StatusBar.h"

namespace snaral::ui
{
namespace
{
void announce (Severity s, const juce::String& text)
{
    juce::AccessibilityHandler::postAnnouncement (severityWord (s) + ": " + text,
                                                  s == Severity::Error ? juce::AccessibilityHandler::AnnouncementPriority::high
                                                                       : juce::AccessibilityHandler::AnnouncementPriority::medium);
}
} // namespace

//==================================================================================================================
StatusBar::StatusBar()
{
    setTitle ("Status bar");
    setDescription ("Shows status messages, host tempo state and the plug-in version.");
    setAccessible (true);
    setSize (400, preferredHeight);
}

StatusBar::~StatusBar() = default;

void StatusBar::post (Severity s, const juce::String& text, int ttlMs)
{
    message = text;
    messageSeverity = s;
    expiresAtMs = ttlMs > 0 ? juce::Time::getMillisecondCounter() + static_cast<juce::int64> (ttlMs) : 0;
    setTitle ("Status: " + severityWord (s) + ": " + text);
    if (expiresAtMs != 0)
        startTimer (250);
    else
        stopTimer();
    announce (s, text);
    repaint();
}

void StatusBar::clearMessage()
{
    message = {};
    expiresAtMs = 0;
    stopTimer();
    setTitle ("Status bar");
    repaint();
}

void StatusBar::timerCallback()
{
    if (expiresAtMs != 0 && static_cast<juce::int64> (juce::Time::getMillisecondCounter()) >= expiresAtMs)
        clearMessage();
}

void StatusBar::setChip (const juce::String& key, Severity s, const juce::String& text)
{
    for (auto& c : chips)
        if (c.key == key)
        {
            c.severity = s;
            c.text = text;
            repaint();
            return;
        }
    chips.push_back ({ key, s, text });
    repaint();
}

void StatusBar::clearChip (const juce::String& key)
{
    chips.erase (std::remove_if (chips.begin(), chips.end(), [&] (const Chip& c) { return c.key == key; }), chips.end());
    repaint();
}

bool StatusBar::hasChip (const juce::String& key) const
{
    for (const auto& c : chips)
        if (c.key == key)
            return true;
    return false;
}

void StatusBar::setVersionText (const juce::String& v)
{
    version = v;
    repaint();
}

void StatusBar::paint (juce::Graphics& g)
{
    const auto& t = SnaralLookAndFeel::tokensFor (*this);
    auto r = getLocalBounds();
    g.setColour (t.border);
    g.drawHorizontalLine (0, 0.0f, static_cast<float> (getWidth()));

    // version, bottom-right
    const auto verFont = fonts::version();
    const int verW = version.isEmpty() ? 0 : static_cast<int> (std::ceil (textWidth (verFont, version))) + 12;
    if (verW > 0)
    {
        g.setFont (verFont);
        g.setColour (t.textMuted);
        g.drawText (version, r.removeFromRight (verW).withTrimmedRight (8), juce::Justification::centredRight, false);
    }

    // chips (right-aligned, left of the version)
    const auto chipFont = fonts::small();
    for (auto it = chips.rbegin(); it != chips.rend(); ++it)
    {
        const auto label = severityWord (it->severity) + ": " + it->text;
        const int w = static_cast<int> (std::ceil (textWidth (chipFont, label))) + 30;
        if (w > r.getWidth() / 2)
            continue;
        auto chip = r.removeFromRight (w).reduced (2, 3);
        const auto colour = SnaralLookAndFeel::severityColour (t, it->severity);
        g.setColour (t.panelRaised);
        g.fillRoundedRectangle (chip.toFloat(), 4.0f);
        g.setColour (colour);
        g.drawRoundedRectangle (chip.toFloat().reduced (0.5f), 4.0f, 1.0f);
        SnaralLookAndFeel::drawSeverityIcon (g, it->severity, chip.removeFromLeft (20).toFloat().reduced (3.0f), colour);
        g.setFont (chipFont);
        g.setColour (t.textPrimary);
        g.drawFittedText (label, chip.reduced (0, 0), juce::Justification::centredLeft, 1, 0.85f);
    }

    // message (left)
    auto left = r.withTrimmedLeft (10);
    if (message.isNotEmpty())
    {
        const auto colour = SnaralLookAndFeel::severityColour (t, messageSeverity);
        SnaralLookAndFeel::drawSeverityIcon (g, messageSeverity, left.removeFromLeft (18).toFloat().reduced (1.0f), colour);
        left.removeFromLeft (4);
        g.setFont (fonts::small());
        g.setColour (t.textPrimary);
        g.drawFittedText (severityWord (messageSeverity) + ": " + message, left, juce::Justification::centredLeft, 1, 0.85f);
    }
}

//==================================================================================================================
ToastHost::ToastHost()
{
    setInterceptsMouseClicks (true, false);
    setTitle ("Notifications");
    setAccessible (false);
}

ToastHost::~ToastHost() = default;

void ToastHost::show (Severity s, const juce::String& text, int ttlMs)
{
    if (ttlMs <= 0)
        ttlMs = (s == Severity::Error || s == Severity::Warning) ? 7000 : 4000;
    const auto now = static_cast<juce::int64> (juce::Time::getMillisecondCounter());
    toasts.push_back ({ s, text, now + ttlMs, now });
    while (static_cast<int> (toasts.size()) > maxVisible)
        toasts.erase (toasts.begin());
    announce (s, text);
    startTimer (250);
    repaint();
}

juce::String ToastHost::getToastText (int i) const
{
    return juce::isPositiveAndBelow (i, getNumToasts()) ? toasts[static_cast<std::size_t> (i)].text : juce::String();
}

void ToastHost::dismissAll()
{
    toasts.clear();
    stopTimer();
    repaint();
}

void ToastHost::timerCallback()
{
    const auto now = static_cast<juce::int64> (juce::Time::getMillisecondCounter());
    const auto before = toasts.size();
    toasts.erase (std::remove_if (toasts.begin(), toasts.end(), [now] (const Toast& t) { return now >= t.expiresAtMs; }), toasts.end());
    if (toasts.empty())
        stopTimer();
    if (before != toasts.size())
        repaint();
}

juce::Rectangle<int> ToastHost::boundsOf (int index) const
{
    const int w = juce::jmin (380, getWidth() - 24);
    const auto font = fonts::body().withHeight (12.5f);
    int y = getHeight() - 12;
    juce::Rectangle<int> result;
    for (int i = static_cast<int> (toasts.size()) - 1; i >= 0; --i)
    {
        juce::AttributedString s;
        s.append (toasts[static_cast<std::size_t> (i)].text, font, juce::Colours::white);
        juce::TextLayout tl;
        tl.createLayout (s, static_cast<float> (w - 70));
        const int h = juce::jmax (52, static_cast<int> (std::ceil (tl.getHeight())) + 38);
        const juce::Rectangle<int> b (getWidth() - w - 12, y - h, w, h);
        if (i == index)
            result = b;
        y -= h + 8;
    }
    return result;
}

bool ToastHost::hitTest (int x, int y)
{
    for (int i = 0; i < getNumToasts(); ++i)
        if (boundsOf (i).contains (x, y))
            return true;
    return false;
}

void ToastHost::mouseDown (const juce::MouseEvent& e)
{
    for (int i = 0; i < getNumToasts(); ++i)
        if (boundsOf (i).contains (e.getPosition()))
        {
            toasts.erase (toasts.begin() + i);
            if (toasts.empty())
                stopTimer();
            repaint();
            return;
        }
}

void ToastHost::paint (juce::Graphics& g)
{
    const auto& t = SnaralLookAndFeel::tokensFor (*this);
    const auto font = fonts::body().withHeight (12.5f);
    for (int i = 0; i < getNumToasts(); ++i)
    {
        const auto& toast = toasts[static_cast<std::size_t> (i)];
        auto b = boundsOf (i).toFloat();
        const auto colour = SnaralLookAndFeel::severityColour (t, toast.severity);
        SnaralLookAndFeel::drawPanelShadow (g, t, b, 6.0f);
        g.setColour (t.panelRaised);
        g.fillRoundedRectangle (b, 6.0f);
        g.setColour (colour);
        g.drawRoundedRectangle (b.reduced (0.5f), 6.0f, 1.5f);

        auto inner = b.reduced (12.0f, 8.0f);
        SnaralLookAndFeel::drawSeverityIcon (g, toast.severity, inner.removeFromLeft (20.0f).withHeight (20.0f), colour);
        inner.removeFromLeft (8.0f);
        auto closeArea = inner.removeFromRight (18.0f);
        g.setColour (t.textMuted);                                             // "x" close affordance
        const auto cx = closeArea.getCentreX(), cy = closeArea.getY() + 8.0f;
        g.drawLine (cx - 4.0f, cy - 4.0f, cx + 4.0f, cy + 4.0f, 1.6f);
        g.drawLine (cx - 4.0f, cy + 4.0f, cx + 4.0f, cy - 4.0f, 1.6f);

        g.setFont (font.boldened());
        g.setColour (colour);
        auto head = inner.removeFromTop (14.0f);
        g.drawText (severityWord (toast.severity), head.toNearestInt(), juce::Justification::topLeft, false);
        g.setFont (font);
        g.setColour (t.textPrimary);
        g.drawFittedText (toast.text, inner.toNearestInt(), juce::Justification::topLeft, 4, 1.0f);
    }
}
} // namespace snaral::ui
