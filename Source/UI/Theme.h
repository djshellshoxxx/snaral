#pragma once

#include "UiCommon.h"
#include "UiOptions.h"

#include <array>
#include <vector>

namespace snaral::ui
{
// =================================================================================================================
// Tokens
// =================================================================================================================

/// Semantic colour tokens of one theme. Widgets never use raw colours; they read these from the LookAndFeel of the
/// component tree (SnaralLookAndFeel::tokensFor).
///
/// State is never conveyed by colour alone: every widget pairs these colours with a shape, icon, text or position cue.
struct ThemeTokens
{
    juce::String id;                ///< stable lower-case id persisted in OptionsModel ("cdl", "midnight", ...)
    juce::String name;              ///< display name
    juce::String description;       ///< one-line description for the Options panel
    bool officialCdlPalette = false;///< true only for the owner-supplied CDL palette (baseline 2.1)
    bool dark = true;

    juce::Colour background;        ///< main window
    juce::Colour panel;             ///< raised sections, header
    juce::Colour panelRaised;       ///< controls sitting on a panel (buttons, combo boxes, knob bodies)
    juce::Colour border;            ///< decorative edges and separators
    juce::Colour outline;           ///< control boundaries (slightly stronger than border)
    juce::Colour accent;            ///< primary accent: active values, selection, primary actions
    juce::Colour accentText;        ///< accent variant that is >= 4.5:1 on the accent tint (text on "on" buttons)
    juce::Colour onAccent;          ///< text/icons drawn on a solid accent fill
    juce::Colour secondary;         ///< secondary accent: modulation, links, learn indicators
    juce::Colour textPrimary;
    juce::Colour textMuted;
    juce::Colour success;
    juce::Colour warning;
    juce::Colour error;
    juce::Colour focusRing;         ///< keyboard focus indicator (always drawn with a 2 px+ stroke)
    juce::Colour shadow;            ///< baseline: rgba(0,0,0,0.55), 8 px blur, 2 px down

    float borderThickness = 1.0f;   ///< high-contrast themes thicken edges

    /// The restrained accent tint used for "on" buttons, composited over `base`.
    juce::Colour accentTint (juce::Colour base) const noexcept { return base.interpolatedWith (accent, 0.16f); }

    /// Meter colour for a position 0..1 along the teal -> orange -> yellow -> red ramp.
    juce::Colour meterColour (float position01) const noexcept;
};

/// The built-in themes. Index 0 is the official CDL default.
struct ThemeCatalog
{
    static const std::vector<ThemeTokens>& all();
    static const ThemeTokens& cdlDefault();
    /// Unknown ids fall back to the CDL default.
    static const ThemeTokens& byId (const juce::String& id);
    static juce::StringArray ids();
};

// ---- contrast ---------------------------------------------------------------------------------------------------
/// WCAG 2.x relative luminance of an opaque colour (alpha is ignored).
double relativeLuminance (juce::Colour c) noexcept;
/// WCAG 2.x contrast ratio between two colours (alpha composited over `over` first when a colour is translucent).
double contrastRatio (juce::Colour a, juce::Colour b) noexcept;

/// One checked pair of tokens (used by the unit test and by the Options panel's theme preview).
struct ContrastRequirement
{
    const char* description;
    std::function<juce::Colour (const ThemeTokens&)> foreground;
    std::function<juce::Colour (const ThemeTokens&)> background;
    double minimumRatio;            ///< 4.5 for text, 3.0 for meaningful graphics (WCAG 1.4.3 / 1.4.11)
};
/// The list of token pairs every theme must satisfy.
const std::vector<ContrastRequirement>& contrastRequirements();

// =================================================================================================================
// Fonts
// =================================================================================================================

/// Font roles. Families are resolved once from what is installed (no fonts are bundled or licensed here):
///   sans: Inter -> Space Grotesk -> JUCE default sans-serif
///   mono: JetBrains Mono -> IBM Plex Mono -> JUCE default monospace
/// Weights: labels 500 (Medium), headings 600 (SemiBold) when the family offers them, otherwise Regular/Bold.
namespace fonts
{
const juce::String& sansFamily();
const juce::String& monoFamily();
/// True when the primary CDL typeface (Inter) is installed; false means the documented fallback is in use.
bool usingPrimaryCdlSans();
/// Test hook: re-run family detection (e.g. after changing the available font list).
void resetFamilyCache();

juce::Font label();      ///< 11 px, Medium, +0.08 em tracking; call sites upper-case the text
juce::Font heading();    ///< 13 px, SemiBold
juce::Font title();      ///< 16 px, SemiBold (product name / panel titles)
juce::Font body();       ///< 13 px, Regular
juce::Font small();      ///< 11 px, Regular (hints, captions)
juce::Font value();      ///< 13.5 px monospace Medium (numeric readouts)
juce::Font version();    ///< 9 px monospace (version text, the only sub-11 px text)
} // namespace fonts

// =================================================================================================================
// LookAndFeel
// =================================================================================================================

/// Snapshot of a rotary control's paint state (shared by MacroKnob and generic rotary sliders).
struct KnobPaint
{
    float position01 = 0.0f;
    bool bipolar = false;           ///< arc starts at 12 o'clock instead of the minimum
    bool enabled = true;
    bool focused = false;
    bool hot = false;
    bool dragging = false;
    bool invalid = false;           ///< red outline + "!" while an invalid value is typed
};

class SnaralLookAndFeel : public juce::LookAndFeel_V4
{
public:
    SnaralLookAndFeel();
    ~SnaralLookAndFeel() override = default;

    void setTokens (const ThemeTokens& newTokens);
    const ThemeTokens& tokens() const noexcept { return themeTokens; }

    void setAnimationsEnabled (bool shouldAnimate) noexcept { animations = shouldAnimate; }
    bool animationsEnabled() const noexcept { return animations; }

    /// Tokens of the closest SnaralLookAndFeel in `c`'s hierarchy, or the CDL default when none is installed.
    static const ThemeTokens& tokensFor (const juce::Component& c);
    /// Animation preference of the closest SnaralLookAndFeel (true when none is installed).
    static bool animationsFor (const juce::Component& c);

    // ---- drawing helpers reused by the widgets ----------------------------------------------------------------
    /// Focus indicator: 2 px ring (3 px in high-contrast themes) drawn just outside `bounds`.
    static void drawFocusRing (juce::Graphics&, const ThemeTokens&, juce::Rectangle<float> bounds, float cornerRadius);
    /// Flat dark body, inner border, 270 degree value arc 4 px outside the body, accent pointer and centre dot.
    static void drawKnob (juce::Graphics&, const ThemeTokens&, juce::Rectangle<float> bodyBounds, const KnobPaint&);
    /// Severity glyph (circle-i, check, triangle-!, octagon-x). Shapes differ so they work without colour.
    static void drawSeverityIcon (juce::Graphics&, Severity, juce::Rectangle<float> area, juce::Colour colour);
    static juce::Colour severityColour (const ThemeTokens&, Severity);
    /// The 2 px burnt-orange diagonal notch at the window's top-left (CDL signature mark until a logo asset exists).
    static void drawSignatureNotch (juce::Graphics&, const ThemeTokens&);
    /// Drop shadow per baseline (8 px blur, 2 px down).
    static void drawPanelShadow (juce::Graphics&, const ThemeTokens&, juce::Rectangle<float> panelBounds, float cornerRadius);

    // ---- LookAndFeel_V4 overrides ----------------------------------------------------------------------------
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPosProportional,
                           float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos, float minSliderPos,
                           float maxSliderPos, juce::Slider::SliderStyle, juce::Slider&) override;
    int getSliderThumbRadius (juce::Slider&) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;

    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown, int buttonX, int buttonY,
                       int buttonW, int buttonH, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;

    juce::Font getPopupMenuFont() override;
    juce::Font getSliderPopupFont (juce::Slider&) override;

    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int width, int height, bool isScrollbarVertical,
                        int thumbStartPosition, int thumbSize, bool isMouseOver, bool isMouseDown) override;
    int getDefaultScrollbarWidth() override { return 12; }

    juce::Rectangle<int> getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos,
                                           juce::Rectangle<int> parentArea) override;
    void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;

private:
    void applyColourIds();

    ThemeTokens themeTokens;
    bool animations = true;
};

// =================================================================================================================
// Tooltips
// =================================================================================================================

/// TooltipWindow that honours the global "tooltips on/off" option. Own one per editor (parent = the editor) and
/// call setTipsEnabled() from ThemeManager::Listener::preferencesChanged. Accessible names/descriptions are
/// separate from tooltips and stay available when tips are off.
class SnaralTooltipWindow : public juce::TooltipWindow
{
public:
    explicit SnaralTooltipWindow (juce::Component* parent, int delayMs = 600)
        : juce::TooltipWindow (parent, delayMs) {}

    void setTipsEnabled (bool shouldShow)
    {
        tipsEnabled = shouldShow;
        if (! shouldShow)
            hideTip();
    }
    bool areTipsEnabled() const noexcept { return tipsEnabled; }

    juce::String getTipFor (juce::Component& c) override
    {
        return tipsEnabled ? juce::TooltipWindow::getTipFor (c) : juce::String();
    }

private:
    bool tipsEnabled = true;
};

// =================================================================================================================
// ThemeManager
// =================================================================================================================

/// Owns the SnaralLookAndFeel and keeps it in sync with an OptionsModel (theme, animations, UI scale).
///
/// Typical use in the editor:
///     themeManager.applyTo (*this);                 // sets the LookAndFeel on the editor and every child
///     themeManager.addListener (this);              // to react to preferencesChanged() (tooltips, scale, ...)
/// ThemeManager never installs itself as the process-wide default LookAndFeel (a plug-in must not change the host's).
class ThemeManager : private OptionsModel::Listener
{
public:
    struct Listener
    {
        virtual ~Listener() = default;
        virtual void themeChanged() {}          ///< theme tokens changed (components are repainted automatically)
        virtual void preferencesChanged() {}    ///< tooltips / animations / scale / visibility toggles changed
    };

    explicit ThemeManager (OptionsModel& options);
    ~ThemeManager() override;

    SnaralLookAndFeel& getLookAndFeel() noexcept { return laf; }
    const ThemeTokens& getTokens() const noexcept { return laf.tokens(); }
    OptionsModel& getOptions() noexcept { return options; }

    /// Installs the LookAndFeel on `root` and remembers it so theme changes repaint it.
    void applyTo (juce::Component& root);
    void detach (juce::Component& root);

    /// Applies the UI scale option to a top-level component: AudioProcessorEditor::setScaleFactor when available,
    /// otherwise an AffineTransform. Hit targets and text scale together, so they never shrink below the
    /// metrics:: minimums at scale >= 1 (the scale option is clamped to 0.75..2.0 and 0.75 is only reachable
    /// explicitly).
    static void applyUiScale (juce::Component& root, float scale);

    bool animationsEnabled() const { return options.areAnimationsEnabled(); }
    bool tooltipsEnabled() const { return options.areTooltipsEnabled(); }
    float uiScale() const { return options.getUiScale(); }

    void addListener (Listener* l) { listeners.add (l); }
    void removeListener (Listener* l) { listeners.remove (l); }

    /// Re-reads the options (call after OptionsModel::setState from project restore).
    void refresh();

private:
    void optionChanged (const juce::Identifier&) override;
    void syncFromOptions (bool notifyTheme);

    OptionsModel& options;
    SnaralLookAndFeel laf;
    juce::ListenerList<Listener> listeners;
    std::vector<juce::Component::SafePointer<juce::Component>> roots;
};
} // namespace snaral::ui
