#pragma once

// Shared small types and layout constants for the Snaral UI kit.
// Headers in Source/UI deliberately include JUCE module headers directly (never JuceHeader.h) so the
// kit can be compiled into headless tests.

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>
#include <string_view>

namespace snaral::ui
{
/// Logical-pixel constants taken from the CDL baseline (AUDIO_APP_BASELINE 2.2 / 2.3) and ui-layout.md.
namespace metrics
{
inline constexpr int headerHeight = 32;
inline constexpr int knobSmall = 36;
inline constexpr int knobNormal = 48;
inline constexpr int knobLarge = 64;
inline constexpr int buttonHeight = 28;
inline constexpr int cornerRadius = 4;
inline constexpr int minHitTarget = 32;          ///< interactive targets are never smaller than this (logical px)
inline constexpr int defaultWidth = 900;
inline constexpr int defaultHeight = 560;
inline constexpr int minWidth = 720;
inline constexpr int minHeight = 480;
inline constexpr int maxWidth = 1440;
inline constexpr int maxHeight = 900;
inline constexpr float labelPx = 11.0f;         ///< labels are ~11 px uppercase
inline constexpr float valuePx = 13.5f;         ///< numeric readouts 13-14 px
inline constexpr float bodyPx = 13.0f;
inline constexpr float headingPx = 13.0f;
inline constexpr float versionPx = 9.0f;        ///< the only text allowed below 11 px (baseline 2.2)
inline constexpr float minTextPx = 11.0f;       ///< readable floor for everything except the version text
inline constexpr float minUiScale = 0.75f;
inline constexpr float maxUiScale = 2.0f;
} // namespace metrics

/// Status severities. Always rendered with an icon shape AND text, never colour alone.
enum class Severity { Info, Success, Warning, Error };

inline juce::String severityWord (Severity s)
{
    switch (s)
    {
        case Severity::Success: return "Success";
        case Severity::Warning: return "Warning";
        case Severity::Error:   return "Error";
        case Severity::Info:    break;
    }
    return "Info";
}

/// Converts a string_view from the (JUCE-free) parameter table to a juce::String.
inline juce::String toJuce (std::string_view s)
{
    return juce::String::fromUTF8 (s.data(), static_cast<int> (s.size()));
}

/// Implemented by content components that can report the height they need for a given width
/// (used by SectionPanel and the Advanced tab scroll containers).
class PreferredHeightProvider
{
public:
    virtual ~PreferredHeightProvider() = default;
    virtual int getPreferredHeight (int width) const = 0;
};

/// Text width helper (Font::getStringWidth is gone in JUCE 9).
float textWidth (const juce::Font& font, const juce::String& text);
} // namespace snaral::ui
