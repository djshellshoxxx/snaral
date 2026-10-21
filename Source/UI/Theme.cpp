#include "Theme.h"

#include <cmath>

namespace snaral::ui
{
namespace
{
juce::Colour hex (juce::uint32 rgb) { return juce::Colour (0xff000000u | rgb); }

constexpr float knobStartAngle = juce::MathConstants<float>::pi * 1.25f;   // -135 deg from 12 o'clock
constexpr float knobEndAngle   = juce::MathConstants<float>::pi * 2.75f;   // +135 deg
} // namespace

float textWidth (const juce::Font& font, const juce::String& text)
{
    if (text.isEmpty())
        return 0.0f;
    juce::GlyphArrangement ga;
    ga.addLineOfText (font, text, 0.0f, 0.0f);
    return ga.getBoundingBox (0, -1, true).getWidth();
}

// =================================================================================================================
// Tokens
// =================================================================================================================
juce::Colour ThemeTokens::meterColour (float p) const noexcept
{
    p = juce::jlimit (0.0f, 1.0f, p);
    struct Stop { float at; juce::Colour c; };
    const Stop stops[] = { { 0.0f, secondary }, { 0.5f, accent }, { 0.8f, warning }, { 1.0f, error } };
    for (int i = 0; i < 3; ++i)
        if (p <= stops[i + 1].at)
            return stops[i].c.interpolatedWith (stops[i + 1].c, (p - stops[i].at) / (stops[i + 1].at - stops[i].at));
    return error;
}

static std::vector<ThemeTokens> buildThemes()
{
    std::vector<ThemeTokens> t;

    {   // Official CDL palette, values copied verbatim from standards/AUDIO_APP_BASELINE.md section 2.1.
        ThemeTokens c;
        c.id = "cdl"; c.name = "CDL"; c.description = "Circuit Drift Labs default (dark, burnt-orange accent)";
        c.officialCdlPalette = true; c.dark = true;
        c.background = hex (0x0E1116); c.panel = hex (0x171B22); c.border = hex (0x2A303A);
        c.accent = hex (0xE8532A); c.secondary = hex (0x4FB6C4);
        c.textPrimary = hex (0xE6E8EC); c.textMuted = hex (0x8A929E);
        c.success = hex (0x7BC96F); c.warning = hex (0xF2C14E);
        c.shadow = juce::Colour::fromFloatRGBA (0.0f, 0.0f, 0.0f, 0.55f);
        // Derived (not part of the official palette): states the baseline does not name.
        c.panelRaised = hex (0x1F252E); c.outline = hex (0x3A4250);
        c.accentText = hex (0xF57F58); c.onAccent = hex (0x0E1116);
        c.error = hex (0xFF6B6B); c.focusRing = hex (0x4FB6C4);
        t.push_back (c);
    }
    {
        ThemeTokens c;
        c.id = "midnight"; c.name = "Midnight"; c.description = "Darker variant: true-black background, amber-orange accent";
        c.dark = true;
        c.background = hex (0x000000); c.panel = hex (0x0A0C10); c.panelRaised = hex (0x141821);
        c.border = hex (0x262B36); c.outline = hex (0x3A4150);
        c.accent = hex (0xF0782F); c.accentText = hex (0xF89A5E); c.onAccent = hex (0x000000);
        c.secondary = hex (0x6FC8D6); c.textPrimary = hex (0xEDEFF3); c.textMuted = hex (0x98A0AD);
        c.success = hex (0x86D67A); c.warning = hex (0xF5CB5C); c.error = hex (0xFF7070);
        c.focusRing = hex (0x6FC8D6); c.shadow = juce::Colour::fromFloatRGBA (0.0f, 0.0f, 0.0f, 0.7f);
        t.push_back (c);
    }
    {
        ThemeTokens c;
        c.id = "highcontrast"; c.name = "High Contrast"; c.description = "Maximum contrast: black, white edges, yellow accent";
        c.dark = true;
        c.background = hex (0x000000); c.panel = hex (0x0A0A0A); c.panelRaised = hex (0x1A1A1A);
        c.border = hex (0xFFFFFF); c.outline = hex (0xFFFFFF);
        c.accent = hex (0xFFD400); c.accentText = hex (0xFFE04D); c.onAccent = hex (0x000000);
        c.secondary = hex (0x00E5FF); c.textPrimary = hex (0xFFFFFF); c.textMuted = hex (0xD0D0D0);
        c.success = hex (0x3DFF6B); c.warning = hex (0xFFB000); c.error = hex (0xFF6B6B);
        c.focusRing = hex (0xFFFFFF); c.shadow = juce::Colours::transparentBlack;
        c.borderThickness = 2.0f;
        t.push_back (c);
    }
    {
        ThemeTokens c;
        c.id = "tealdrift"; c.name = "Teal Drift"; c.description = "Derived variant: deep teal-slate with a teal accent";
        c.dark = true;
        c.background = hex (0x0B1315); c.panel = hex (0x122025); c.panelRaised = hex (0x1A2D33);
        c.border = hex (0x24383F); c.outline = hex (0x35505A);
        c.accent = hex (0x4FB6C4); c.accentText = hex (0x7FD3DF); c.onAccent = hex (0x0B1315);
        c.secondary = hex (0xF2A365); c.textPrimary = hex (0xE4EEF0); c.textMuted = hex (0x8FA7AD);
        c.success = hex (0x7BC96F); c.warning = hex (0xF2C14E); c.error = hex (0xFF7B7B);
        c.focusRing = hex (0xF2A365); c.shadow = juce::Colour::fromFloatRGBA (0.0f, 0.0f, 0.0f, 0.55f);
        t.push_back (c);
    }
    {
        ThemeTokens c;
        c.id = "daylight"; c.name = "Daylight"; c.description = "Derived light variant for bright rooms";
        c.dark = false;
        c.background = hex (0xEEF0F3); c.panel = hex (0xFFFFFF); c.panelRaised = hex (0xE4E7EC);
        c.border = hex (0xC9CED6); c.outline = hex (0x8A93A0);
        c.accent = hex (0xB83A0E); c.accentText = hex (0x9A2D08); c.onAccent = hex (0xFFFFFF);
        c.secondary = hex (0x0F6A78); c.textPrimary = hex (0x14181F); c.textMuted = hex (0x505967);
        c.success = hex (0x1E7B2E); c.warning = hex (0x8A5A00); c.error = hex (0xB3261E);
        c.focusRing = hex (0x0F6A78); c.shadow = juce::Colour::fromFloatRGBA (0.0f, 0.0f, 0.0f, 0.2f);
        t.push_back (c);
    }
    return t;
}

const std::vector<ThemeTokens>& ThemeCatalog::all()
{
    static const std::vector<ThemeTokens> themes = buildThemes();
    return themes;
}

const ThemeTokens& ThemeCatalog::cdlDefault() { return all().front(); }

const ThemeTokens& ThemeCatalog::byId (const juce::String& id)
{
    for (const auto& t : all())
        if (t.id == id)
            return t;
    return cdlDefault();
}

juce::StringArray ThemeCatalog::ids()
{
    juce::StringArray a;
    for (const auto& t : all())
        a.add (t.id);
    return a;
}

// ---- contrast ---------------------------------------------------------------------------------------------------
double relativeLuminance (juce::Colour c) noexcept
{
    auto lin = [] (float v)
    {
        const double d = static_cast<double> (v);
        return d <= 0.03928 ? d / 12.92 : std::pow ((d + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * lin (c.getFloatRed()) + 0.7152 * lin (c.getFloatGreen()) + 0.0722 * lin (c.getFloatBlue());
}

double contrastRatio (juce::Colour a, juce::Colour b) noexcept
{
    if (a.getAlpha() < 255)
        a = b.overlaidWith (a);
    const double la = relativeLuminance (a), lb = relativeLuminance (b);
    const double hi = std::max (la, lb), lo = std::min (la, lb);
    return (hi + 0.05) / (lo + 0.05);
}

const std::vector<ContrastRequirement>& contrastRequirements()
{
    using T = ThemeTokens;
    static const std::vector<ContrastRequirement> reqs = {
        // ---- text (WCAG 1.4.3, 4.5:1) ----
        { "text on background",        [] (const T& t) { return t.textPrimary; }, [] (const T& t) { return t.background; }, 4.5 },
        { "text on panel",             [] (const T& t) { return t.textPrimary; }, [] (const T& t) { return t.panel; }, 4.5 },
        { "text on raised control",    [] (const T& t) { return t.textPrimary; }, [] (const T& t) { return t.panelRaised; }, 4.5 },
        { "muted text on background",  [] (const T& t) { return t.textMuted; }, [] (const T& t) { return t.background; }, 4.5 },
        { "muted text on panel",       [] (const T& t) { return t.textMuted; }, [] (const T& t) { return t.panel; }, 4.5 },
        { "muted text on raised",      [] (const T& t) { return t.textMuted; }, [] (const T& t) { return t.panelRaised; }, 4.5 },
        { "accent text on background", [] (const T& t) { return t.accent; }, [] (const T& t) { return t.background; }, 4.5 },
        { "accent text on panel",      [] (const T& t) { return t.accent; }, [] (const T& t) { return t.panel; }, 4.5 },
        { "accentText on accent tint (panel)",  [] (const T& t) { return t.accentText; }, [] (const T& t) { return t.accentTint (t.panel); }, 4.5 },
        { "accentText on accent tint (raised)", [] (const T& t) { return t.accentText; }, [] (const T& t) { return t.accentTint (t.panelRaised); }, 4.5 },
        { "text on accent tint",       [] (const T& t) { return t.textPrimary; }, [] (const T& t) { return t.accentTint (t.panelRaised); }, 4.5 },
        { "onAccent on solid accent",  [] (const T& t) { return t.onAccent; }, [] (const T& t) { return t.accent; }, 4.5 },
        { "secondary text on background", [] (const T& t) { return t.secondary; }, [] (const T& t) { return t.background; }, 4.5 },
        { "secondary text on panel",   [] (const T& t) { return t.secondary; }, [] (const T& t) { return t.panel; }, 4.5 },
        { "success text on background", [] (const T& t) { return t.success; }, [] (const T& t) { return t.background; }, 4.5 },
        { "success text on panel",     [] (const T& t) { return t.success; }, [] (const T& t) { return t.panel; }, 4.5 },
        { "warning text on background", [] (const T& t) { return t.warning; }, [] (const T& t) { return t.background; }, 4.5 },
        { "warning text on panel",     [] (const T& t) { return t.warning; }, [] (const T& t) { return t.panel; }, 4.5 },
        { "error text on background",  [] (const T& t) { return t.error; }, [] (const T& t) { return t.background; }, 4.5 },
        { "error text on panel",       [] (const T& t) { return t.error; }, [] (const T& t) { return t.panel; }, 4.5 },
        // ---- meaningful graphics (WCAG 1.4.11, 3:1) ----
        { "focus ring on background",  [] (const T& t) { return t.focusRing; }, [] (const T& t) { return t.background; }, 3.0 },
        { "focus ring on panel",       [] (const T& t) { return t.focusRing; }, [] (const T& t) { return t.panel; }, 3.0 },
        { "knob pointer (accent) on raised body", [] (const T& t) { return t.accent; }, [] (const T& t) { return t.panelRaised; }, 3.0 },
        { "success icon on raised",    [] (const T& t) { return t.success; }, [] (const T& t) { return t.panelRaised; }, 3.0 },
        { "warning icon on raised",    [] (const T& t) { return t.warning; }, [] (const T& t) { return t.panelRaised; }, 3.0 },
        { "error icon on raised",      [] (const T& t) { return t.error; }, [] (const T& t) { return t.panelRaised; }, 3.0 },
        { "secondary graphics on raised", [] (const T& t) { return t.secondary; }, [] (const T& t) { return t.panelRaised; }, 3.0 },
    };
    return reqs;
}

// =================================================================================================================
// Fonts
// =================================================================================================================
namespace
{
struct Families
{
    juce::String sans, mono;
    bool primary = false;
    bool resolved = false;
};

Families& families()
{
    static Families f;
    return f;
}

void resolveFamilies()
{
    auto& f = families();
    if (f.resolved)
        return;
    const auto names = juce::Font::findAllTypefaceNames();
    auto pick = [&names] (std::initializer_list<const char*> candidates, const juce::String& fallback, bool* first = nullptr)
    {
        int i = 0;
        for (auto* c : candidates)
        {
            if (names.contains (c, true))
            {
                if (first != nullptr)
                    *first = (i == 0);
                return juce::String (c);
            }
            ++i;
        }
        if (first != nullptr)
            *first = false;
        return fallback;
    };
    bool primary = false;
    f.sans = pick ({ "Inter", "Space Grotesk" }, juce::Font::getDefaultSansSerifFontName(), &primary);
    f.primary = primary;
    f.mono = pick ({ "JetBrains Mono", "IBM Plex Mono" }, juce::Font::getDefaultMonospacedFontName());
    f.resolved = true;
}

juce::Font makeFont (const juce::String& family, std::initializer_list<const char*> styles, bool boldFallback,
                     float height, float kerning = 0.0f)
{
    juce::String style;
    const auto available = juce::Font::findAllTypefaceStyles (family);
    for (auto* s : styles)
        if (available.contains (s, true))
        {
            style = s;
            break;
        }

    auto opts = juce::FontOptions().withName (family).withHeight (height);
    if (style.isNotEmpty())
        opts = opts.withStyle (style);
    else if (boldFallback)
        opts = opts.withStyleFlags (juce::Font::bold);

    juce::Font f (opts);
    if (kerning != 0.0f)
        f.setExtraKerningFactor (kerning);
    return f;
}
} // namespace

namespace fonts
{
const juce::String& sansFamily() { resolveFamilies(); return families().sans; }
const juce::String& monoFamily() { resolveFamilies(); return families().mono; }
bool usingPrimaryCdlSans() { resolveFamilies(); return families().primary; }
void resetFamilyCache() { families().resolved = false; }

juce::Font label()   { return makeFont (sansFamily(), { "Medium" }, false, metrics::labelPx, 0.08f); }
juce::Font heading() { return makeFont (sansFamily(), { "SemiBold", "Semibold", "Bold" }, true, metrics::headingPx); }
juce::Font title()   { return makeFont (sansFamily(), { "SemiBold", "Semibold", "Bold" }, true, 16.0f); }
juce::Font body()    { return makeFont (sansFamily(), { "Regular", "Book" }, false, metrics::bodyPx); }
juce::Font small()   { return makeFont (sansFamily(), { "Regular", "Book" }, false, metrics::minTextPx); }
juce::Font value()   { return makeFont (monoFamily(), { "Medium" }, false, metrics::valuePx); }
juce::Font version() { return makeFont (monoFamily(), { "Regular", "Book" }, false, metrics::versionPx); }
} // namespace fonts

// =================================================================================================================
// LookAndFeel
// =================================================================================================================
SnaralLookAndFeel::SnaralLookAndFeel()
{
    setTokens (ThemeCatalog::cdlDefault());
    setDefaultSansSerifTypefaceName (fonts::sansFamily());
}

void SnaralLookAndFeel::setTokens (const ThemeTokens& t)
{
    themeTokens = t;
    applyColourIds();
}

const ThemeTokens& SnaralLookAndFeel::tokensFor (const juce::Component& c)
{
    if (auto* laf = dynamic_cast<SnaralLookAndFeel*> (&c.getLookAndFeel()))
        return laf->tokens();
    return ThemeCatalog::cdlDefault();
}

bool SnaralLookAndFeel::animationsFor (const juce::Component& c)
{
    if (auto* laf = dynamic_cast<SnaralLookAndFeel*> (&c.getLookAndFeel()))
        return laf->animationsEnabled();
    return true;
}

void SnaralLookAndFeel::applyColourIds()
{
    const auto& t = themeTokens;
    const auto none = juce::Colours::transparentBlack;
    const auto tint = t.accentTint (t.panelRaised);

    setColour (juce::ResizableWindow::backgroundColourId, t.background);
    setColour (juce::DocumentWindow::textColourId, t.textPrimary);

    setColour (juce::Label::textColourId, t.textPrimary);
    setColour (juce::Label::backgroundColourId, none);
    setColour (juce::Label::outlineColourId, none);
    setColour (juce::Label::backgroundWhenEditingColourId, t.panelRaised);
    setColour (juce::Label::textWhenEditingColourId, t.textPrimary);
    setColour (juce::Label::outlineWhenEditingColourId, t.focusRing);

    setColour (juce::TextEditor::backgroundColourId, t.panelRaised);
    setColour (juce::TextEditor::textColourId, t.textPrimary);
    setColour (juce::TextEditor::highlightColourId, t.accent.withAlpha (0.45f));
    setColour (juce::TextEditor::highlightedTextColourId, t.textPrimary);
    setColour (juce::TextEditor::outlineColourId, t.outline);
    setColour (juce::TextEditor::focusedOutlineColourId, t.focusRing);
    setColour (juce::TextEditor::shadowColourId, none);
    setColour (juce::CaretComponent::caretColourId, t.textPrimary);

    setColour (juce::TextButton::buttonColourId, t.panelRaised);
    setColour (juce::TextButton::buttonOnColourId, tint);
    setColour (juce::TextButton::textColourOffId, t.textPrimary);
    setColour (juce::TextButton::textColourOnId, t.accentText);

    setColour (juce::ToggleButton::textColourId, t.textPrimary);
    setColour (juce::ToggleButton::tickColourId, t.accentText);
    setColour (juce::ToggleButton::tickDisabledColourId, t.textMuted);

    setColour (juce::ComboBox::backgroundColourId, t.panelRaised);
    setColour (juce::ComboBox::textColourId, t.textPrimary);
    setColour (juce::ComboBox::outlineColourId, t.outline);
    setColour (juce::ComboBox::buttonColourId, t.panelRaised);
    setColour (juce::ComboBox::arrowColourId, t.textPrimary);
    setColour (juce::ComboBox::focusedOutlineColourId, t.focusRing);

    setColour (juce::PopupMenu::backgroundColourId, t.panel);
    setColour (juce::PopupMenu::textColourId, t.textPrimary);
    setColour (juce::PopupMenu::headerTextColourId, t.textMuted);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, t.accent);
    setColour (juce::PopupMenu::highlightedTextColourId, t.onAccent);

    setColour (juce::Slider::backgroundColourId, t.outline);
    setColour (juce::Slider::trackColourId, t.accent);
    setColour (juce::Slider::thumbColourId, t.panelRaised);
    setColour (juce::Slider::rotarySliderFillColourId, t.accent);
    setColour (juce::Slider::rotarySliderOutlineColourId, t.outline);
    setColour (juce::Slider::textBoxTextColourId, t.textPrimary);
    setColour (juce::Slider::textBoxBackgroundColourId, t.panelRaised);
    setColour (juce::Slider::textBoxOutlineColourId, t.outline);
    setColour (juce::Slider::textBoxHighlightColourId, t.accent.withAlpha (0.45f));

    setColour (juce::ScrollBar::thumbColourId, t.textMuted);
    setColour (juce::ScrollBar::trackColourId, none);
    setColour (juce::ScrollBar::backgroundColourId, none);

    setColour (juce::TooltipWindow::backgroundColourId, t.panelRaised);
    setColour (juce::TooltipWindow::textColourId, t.textPrimary);
    setColour (juce::TooltipWindow::outlineColourId, t.outline);

    setColour (juce::ListBox::backgroundColourId, t.panel);
    setColour (juce::ListBox::outlineColourId, t.border);
    setColour (juce::ListBox::textColourId, t.textPrimary);

    setColour (juce::HyperlinkButton::textColourId, t.secondary);
    setColour (juce::AlertWindow::backgroundColourId, t.panel);
    setColour (juce::AlertWindow::textColourId, t.textPrimary);
    setColour (juce::AlertWindow::outlineColourId, t.outline);
}

// ---- helpers ----------------------------------------------------------------------------------------------------
void SnaralLookAndFeel::drawFocusRing (juce::Graphics& g, const ThemeTokens& t, juce::Rectangle<float> b, float radius)
{
    const float w = t.borderThickness > 1.0f ? 3.0f : 2.0f;
    g.setColour (t.focusRing);
    g.drawRoundedRectangle (b.expanded (w * 0.5f + 1.0f), radius + w * 0.5f, w);
}

juce::Colour SnaralLookAndFeel::severityColour (const ThemeTokens& t, Severity s)
{
    switch (s)
    {
        case Severity::Success: return t.success;
        case Severity::Warning: return t.warning;
        case Severity::Error:   return t.error;
        case Severity::Info:    break;
    }
    return t.secondary;
}

void SnaralLookAndFeel::drawSeverityIcon (juce::Graphics& g, Severity s, juce::Rectangle<float> area, juce::Colour colour)
{
    const auto sq = area.withSizeKeepingCentre (juce::jmin (area.getWidth(), area.getHeight()),
                                                juce::jmin (area.getWidth(), area.getHeight()));
    const auto b = sq.reduced (1.0f);
    const float cx = b.getCentreX(), cy = b.getCentreY(), u = b.getWidth() / 16.0f;
    g.setColour (colour);
    const juce::PathStrokeType stroke (juce::jmax (1.4f, 1.6f * u), juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

    juce::Path shape, glyph;
    switch (s)
    {
        case Severity::Info:
            shape.addEllipse (b);
            glyph.addEllipse (cx - 1.0f * u, cy - 4.6f * u, 2.0f * u, 2.0f * u);
            glyph.addRectangle (cx - 0.9f * u, cy - 1.4f * u, 1.8f * u, 5.6f * u);
            g.strokePath (shape, stroke);
            g.fillPath (glyph);
            break;
        case Severity::Success:
            shape.addEllipse (b);
            glyph.startNewSubPath (cx - 3.6f * u, cy + 0.2f * u);
            glyph.lineTo (cx - 1.0f * u, cy + 2.9f * u);
            glyph.lineTo (cx + 3.8f * u, cy - 2.8f * u);
            g.strokePath (shape, stroke);
            g.strokePath (glyph, stroke);
            break;
        case Severity::Warning:
            shape.startNewSubPath (cx, b.getY());
            shape.lineTo (b.getRight(), b.getBottom());
            shape.lineTo (b.getX(), b.getBottom());
            shape.closeSubPath();
            g.strokePath (shape, stroke);
            glyph.addRectangle (cx - 0.9f * u, cy - 3.2f * u, 1.8f * u, 4.6f * u);
            glyph.addEllipse (cx - 1.0f * u, cy + 2.6f * u, 2.0f * u, 2.0f * u);
            g.fillPath (glyph);
            break;
        case Severity::Error:
        {
            const float k = b.getWidth() * 0.29f;
            shape.startNewSubPath (b.getX() + k, b.getY());
            shape.lineTo (b.getRight() - k, b.getY());
            shape.lineTo (b.getRight(), b.getY() + k);
            shape.lineTo (b.getRight(), b.getBottom() - k);
            shape.lineTo (b.getRight() - k, b.getBottom());
            shape.lineTo (b.getX() + k, b.getBottom());
            shape.lineTo (b.getX(), b.getBottom() - k);
            shape.lineTo (b.getX(), b.getY() + k);
            shape.closeSubPath();
            g.strokePath (shape, stroke);
            glyph.startNewSubPath (cx - 3.0f * u, cy - 3.0f * u);
            glyph.lineTo (cx + 3.0f * u, cy + 3.0f * u);
            glyph.startNewSubPath (cx + 3.0f * u, cy - 3.0f * u);
            glyph.lineTo (cx - 3.0f * u, cy + 3.0f * u);
            g.strokePath (glyph, stroke);
            break;
        }
    }
}

void SnaralLookAndFeel::drawSignatureNotch (juce::Graphics& g, const ThemeTokens& t)
{
    g.setColour (t.accent);
    g.drawLine (0.0f, 12.0f, 12.0f, 0.0f, 2.0f);
}

void SnaralLookAndFeel::drawPanelShadow (juce::Graphics& g, const ThemeTokens& t, juce::Rectangle<float> r, float radius)
{
    if (t.shadow.isTransparent())
        return;
    juce::Path p;
    p.addRoundedRectangle (r, radius);
    juce::DropShadow (t.shadow, 8, { 0, 2 }).drawForPath (g, p);
}

void SnaralLookAndFeel::drawKnob (juce::Graphics& g, const ThemeTokens& t, juce::Rectangle<float> bodyBounds, const KnobPaint& k)
{
    const auto c = bodyBounds.getCentre();
    const float r = bodyBounds.getWidth() * 0.5f;
    const float arcR = r + 4.0f + 1.5f;                       // 4 px gap between the body and the 3 px arc
    const float alpha = k.enabled ? 1.0f : 0.4f;
    const float pos = juce::jlimit (0.0f, 1.0f, k.position01);
    const float angle = knobStartAngle + pos * (knobEndAngle - knobStartAngle);
    const float arcWidth = k.dragging ? 4.0f : 3.0f;

    // track
    juce::Path track;
    track.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, knobStartAngle, knobEndAngle, true);
    g.setColour (t.outline.withMultipliedAlpha (alpha));
    g.strokePath (track, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // value arc
    {
        const float from = k.bipolar ? (knobStartAngle + knobEndAngle) * 0.5f : knobStartAngle;
        const float a0 = juce::jmin (from, angle), a1 = juce::jmax (from, angle);
        if (a1 - a0 > 0.002f)
        {
            juce::Path val;
            val.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, a0, a1, true);
            g.setColour (t.accent.withMultipliedAlpha (alpha));
            g.strokePath (val, juce::PathStrokeType (arcWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }

    // body
    g.setColour ((t.dark ? t.background : t.panelRaised).withMultipliedAlpha (alpha));
    g.fillEllipse (bodyBounds);
    g.setColour ((k.hot ? t.textMuted : t.outline).withMultipliedAlpha (alpha));
    g.drawEllipse (bodyBounds.reduced (0.5f), t.borderThickness);

    // pointer + centre dot
    juce::Path pointer;
    pointer.startNewSubPath (c.x + std::sin (angle) * r * 0.30f, c.y - std::cos (angle) * r * 0.30f);
    pointer.lineTo (c.x + std::sin (angle) * r * 0.86f, c.y - std::cos (angle) * r * 0.86f);
    g.setColour (t.accent.withMultipliedAlpha (alpha));
    g.strokePath (pointer, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (t.textMuted.withMultipliedAlpha (alpha));
    g.fillEllipse (c.x - 2.0f, c.y - 2.0f, 4.0f, 4.0f);

    if (k.invalid)
    {
        g.setColour (t.error);
        g.drawEllipse (bodyBounds.expanded (2.0f), 2.0f);
    }
    if (k.focused)
        drawFocusRing (g, t, juce::Rectangle<float> (c.x - arcR - 1.5f, c.y - arcR - 1.5f, (arcR + 1.5f) * 2.0f, (arcR + 1.5f) * 2.0f)
                                 .expanded (1.0f), arcR + 2.0f);
}

// ---- LookAndFeel_V4 overrides -----------------------------------------------------------------------------------
void SnaralLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider& s)
{
    const float arcMargin = 4.0f + 3.0f + 2.0f;
    const float d = juce::jmax (8.0f, static_cast<float> (juce::jmin (w, h)) - 2.0f * arcMargin);
    const auto body = juce::Rectangle<float> (d, d).withCentre ({ x + w * 0.5f, y + h * 0.5f });
    KnobPaint k;
    k.position01 = pos;
    k.enabled = s.isEnabled();
    k.focused = s.hasKeyboardFocus (false);
    k.hot = s.isMouseOverOrDragging();
    k.dragging = s.isMouseButtonDown();
    drawKnob (g, themeTokens, body, k);
}

int SnaralLookAndFeel::getSliderThumbRadius (juce::Slider&) { return 12; }

juce::Label* SnaralLookAndFeel::createSliderTextBox (juce::Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    l->setFont (fonts::value());
    l->setMinimumHorizontalScale (0.8f);
    l->setColour (juce::Label::textColourId, themeTokens.textPrimary);
    l->setColour (juce::Label::backgroundColourId, themeTokens.panelRaised);
    l->setColour (juce::Label::outlineColourId, themeTokens.outline);
    return l;
}

void SnaralLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float sliderPos, float minPos,
                                          float maxPos, juce::Slider::SliderStyle style, juce::Slider& s)
{
    if (style != juce::Slider::LinearHorizontal && style != juce::Slider::LinearVertical)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, sliderPos, minPos, maxPos, style, s);
        return;
    }

    const auto& t = themeTokens;
    const bool horizontal = style == juce::Slider::LinearHorizontal;
    const float alpha = s.isEnabled() ? 1.0f : 0.4f;
    const auto area = juce::Rectangle<float> (static_cast<float> (x), static_cast<float> (y),
                                              static_cast<float> (w), static_cast<float> (h));
    const auto centre = area.getCentre();

    juce::Rectangle<float> track = horizontal ? juce::Rectangle<float> (minPos, centre.y - 2.0f, maxPos - minPos, 4.0f)
                                               : juce::Rectangle<float> (centre.x - 2.0f, maxPos, 4.0f, minPos - maxPos);
    g.setColour (t.outline.withMultipliedAlpha (alpha));
    g.fillRoundedRectangle (track, 2.0f);

    juce::Rectangle<float> fill = horizontal ? track.withRight (sliderPos) : track.withTop (sliderPos);
    g.setColour (t.accent.withMultipliedAlpha (alpha));
    g.fillRoundedRectangle (fill, 2.0f);

    const auto thumb = horizontal ? juce::Rectangle<float> (16.0f, 24.0f).withCentre ({ sliderPos, centre.y })
                                  : juce::Rectangle<float> (24.0f, 16.0f).withCentre ({ centre.x, sliderPos });
    g.setColour (t.panelRaised.withMultipliedAlpha (alpha));
    g.fillRoundedRectangle (thumb, 3.0f);
    g.setColour ((s.isMouseOverOrDragging() ? t.textMuted : t.outline).withMultipliedAlpha (alpha));
    g.drawRoundedRectangle (thumb.reduced (0.5f), 3.0f, juce::jmax (1.0f, t.borderThickness));
    g.setColour (t.accent.withMultipliedAlpha (alpha));                       // grip line: position cue beyond colour
    if (horizontal)
        g.fillRect (thumb.getCentreX() - 1.0f, thumb.getY() + 5.0f, 2.0f, thumb.getHeight() - 10.0f);
    else
        g.fillRect (thumb.getX() + 5.0f, thumb.getCentreY() - 1.0f, thumb.getWidth() - 10.0f, 2.0f);

    if (s.hasKeyboardFocus (false))
        drawFocusRing (g, t, thumb, 3.0f);
}

void SnaralLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool highlighted, bool down)
{
    const auto& t = themeTokens;
    const auto bounds = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = b.getToggleState();
    const float radius = static_cast<float> (metrics::cornerRadius);

    auto fill = on ? t.accentTint (t.panelRaised) : t.panelRaised;
    if (down)            fill = fill.interpolatedWith (t.background, 0.35f);
    else if (highlighted) fill = fill.interpolatedWith (t.textPrimary, 0.08f);
    if (! b.isEnabled())  fill = fill.withMultipliedAlpha (0.5f);

    g.setColour (fill);
    g.fillRoundedRectangle (bounds, radius);
    g.setColour (on ? t.accent : (highlighted ? t.textMuted : t.outline));
    g.drawRoundedRectangle (bounds, radius, on ? juce::jmax (1.5f, t.borderThickness) : t.borderThickness);

    if (b.hasKeyboardFocus (true))
        drawFocusRing (g, t, bounds, radius);
}

juce::Font SnaralLookAndFeel::getTextButtonFont (juce::TextButton&, int)
{
    return fonts::heading().withHeight (12.5f);
}

void SnaralLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    const auto& t = themeTokens;
    const bool on = b.getToggleState();
    const auto font = getTextButtonFont (b, b.getHeight());
    g.setFont (font);
    auto colour = on ? t.accentText : t.textPrimary;
    if (! b.isEnabled())
        colour = t.textMuted.withMultipliedAlpha (0.8f);
    g.setColour (colour);

    auto area = b.getLocalBounds().reduced (6, 1);
    if (b.getClickingTogglesState())          // toggle buttons carry a check mark when on (shape cue)
    {
        auto box = area.removeFromLeft (14).toFloat().withSizeKeepingCentre (12.0f, 12.0f);
        g.drawRoundedRectangle (box, 2.0f, 1.2f);
        if (on)
        {
            juce::Path tick;
            tick.startNewSubPath (box.getX() + 2.5f, box.getCentreY());
            tick.lineTo (box.getX() + 5.0f, box.getBottom() - 3.0f);
            tick.lineTo (box.getRight() - 2.0f, box.getY() + 3.0f);
            g.strokePath (tick, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        area.removeFromLeft (4);
    }
    g.drawFittedText (b.getButtonText(), area, juce::Justification::centred, 1, 0.85f);
}

void SnaralLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool down)
{
    const auto& t = themeTokens;
    const bool on = b.getToggleState();
    drawButtonBackground (g, b, t.panelRaised, highlighted, down);

    auto area = b.getLocalBounds().reduced (8, 1);
    auto box = area.removeFromLeft (16).toFloat().withSizeKeepingCentre (14.0f, 14.0f);
    const auto ink = b.isEnabled() ? (on ? t.accentText : t.textPrimary) : t.textMuted;
    g.setColour (ink);
    g.drawRoundedRectangle (box, 3.0f, 1.4f);
    if (on)
    {
        juce::Path tick;
        tick.startNewSubPath (box.getX() + 3.0f, box.getCentreY() + 0.5f);
        tick.lineTo (box.getX() + 5.8f, box.getBottom() - 3.5f);
        tick.lineTo (box.getRight() - 2.5f, box.getY() + 3.5f);
        g.strokePath (tick, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    area.removeFromLeft (6);
    g.setFont (fonts::body().withHeight (12.5f));
    g.setColour (b.isEnabled() ? (on ? t.accentText : t.textPrimary) : t.textMuted);
    g.drawFittedText (b.getButtonText(), area, juce::Justification::centredLeft, 1, 0.85f);
}

void SnaralLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto& t = themeTokens;
    const auto bounds = juce::Rectangle<float> (0.0f, 0.0f, static_cast<float> (w), static_cast<float> (h)).reduced (0.5f);
    const float radius = static_cast<float> (metrics::cornerRadius);
    g.setColour (box.isEnabled() ? t.panelRaised : t.panelRaised.withMultipliedAlpha (0.5f));
    g.fillRoundedRectangle (bounds, radius);
    g.setColour (box.isMouseOver (true) ? t.textMuted : t.outline);
    g.drawRoundedRectangle (bounds, radius, t.borderThickness);

    const auto arrow = juce::Rectangle<float> (static_cast<float> (w) - 22.0f, 0.0f, 14.0f, static_cast<float> (h));
    juce::Path chevron;
    chevron.startNewSubPath (arrow.getX() + 2.0f, arrow.getCentreY() - 2.0f);
    chevron.lineTo (arrow.getCentreX(), arrow.getCentreY() + 3.0f);
    chevron.lineTo (arrow.getRight() - 2.0f, arrow.getCentreY() - 2.0f);
    g.setColour (box.isEnabled() ? t.textPrimary : t.textMuted);
    g.strokePath (chevron, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (box.hasKeyboardFocus (true))
        drawFocusRing (g, t, bounds, radius);
}

juce::Font SnaralLookAndFeel::getComboBoxFont (juce::ComboBox&) { return fonts::body().withHeight (12.5f); }

void SnaralLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (8, 1, box.getWidth() - 32, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}

juce::Font SnaralLookAndFeel::getPopupMenuFont() { return fonts::body(); }
juce::Font SnaralLookAndFeel::getSliderPopupFont (juce::Slider&) { return fonts::value(); }

void SnaralLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    g.fillAll (themeTokens.panel);
    g.setColour (themeTokens.outline);
    g.drawRect (0, 0, w, h, 1);
}

void SnaralLookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int w, int h, bool vertical,
                                       int thumbStart, int thumbSize, bool over, bool down)
{
    const float thickness = (over || down) ? 8.0f : 6.0f;
    juce::Rectangle<float> thumb = vertical
        ? juce::Rectangle<float> (static_cast<float> (x) + (static_cast<float> (w) - thickness) * 0.5f,
                                  static_cast<float> (y + thumbStart), thickness, static_cast<float> (thumbSize))
        : juce::Rectangle<float> (static_cast<float> (x + thumbStart),
                                  static_cast<float> (y) + (static_cast<float> (h) - thickness) * 0.5f,
                                  static_cast<float> (thumbSize), thickness);
    g.setColour (themeTokens.textMuted.withAlpha (down ? 0.95f : (over ? 0.8f : 0.55f)));
    g.fillRoundedRectangle (thumb, thickness * 0.5f);
}

namespace
{
juce::TextLayout layoutTip (const juce::String& text, juce::Colour colour)
{
    juce::AttributedString s;
    s.setJustification (juce::Justification::centredLeft);
    s.append (text, fonts::body().withHeight (12.5f), colour);
    juce::TextLayout tl;
    tl.createLayoutWithBalancedLineLengths (s, 320.0f);
    return tl;
}
} // namespace

juce::Rectangle<int> SnaralLookAndFeel::getTooltipBounds (const juce::String& tipText, juce::Point<int> pos,
                                                          juce::Rectangle<int> parentArea)
{
    const auto tl = layoutTip (tipText, juce::Colours::white);
    const int w = static_cast<int> (std::ceil (tl.getWidth())) + 18;
    const int h = static_cast<int> (std::ceil (tl.getHeight())) + 12;
    return juce::Rectangle<int> (pos.x > parentArea.getCentreX() ? pos.x - (w + 12) : pos.x + 24,
                                 pos.y > parentArea.getCentreY() ? pos.y - (h + 6) : pos.y + 6, w, h)
        .constrainedWithin (parentArea);
}

void SnaralLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h)
{
    const auto r = juce::Rectangle<float> (0.0f, 0.0f, static_cast<float> (w), static_cast<float> (h));
    g.setColour (themeTokens.panelRaised);
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (themeTokens.outline);
    g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);
    layoutTip (text, themeTokens.textPrimary).draw (g, r.reduced (9.0f, 6.0f));
}

// =================================================================================================================
// ThemeManager
// =================================================================================================================
ThemeManager::ThemeManager (OptionsModel& o) : options (o)
{
    syncFromOptions (false);
    options.addListener (this);
}

ThemeManager::~ThemeManager()
{
    options.removeListener (this);
    for (auto& r : roots)
        if (r != nullptr && &r->getLookAndFeel() == &laf)
            r->setLookAndFeel (nullptr);
}

void ThemeManager::syncFromOptions (bool notifyTheme)
{
    const auto& wanted = ThemeCatalog::byId (options.getThemeId());
    const bool themeChanged = laf.tokens().id != wanted.id;
    if (themeChanged)
        laf.setTokens (wanted);
    const bool animationsChanged = laf.animationsEnabled() != options.areAnimationsEnabled();
    laf.setAnimationsEnabled (options.areAnimationsEnabled());

    if (themeChanged || animationsChanged)
    {
        for (auto& r : roots)
            if (r != nullptr)
                r->sendLookAndFeelChange();
    }
    if (themeChanged)
    {
        if (notifyTheme)
            listeners.call ([] (Listener& l) { l.themeChanged(); });
    }
}

void ThemeManager::optionChanged (const juce::Identifier&)
{
    syncFromOptions (true);
    listeners.call ([] (Listener& l) { l.preferencesChanged(); });
}

void ThemeManager::refresh()
{
    syncFromOptions (true);
    listeners.call ([] (Listener& l) { l.preferencesChanged(); });
}

void ThemeManager::applyTo (juce::Component& root)
{
    root.setLookAndFeel (&laf);
    for (auto& r : roots)
        if (r.getComponent() == &root)
            return;
    roots.emplace_back (&root);
}

void ThemeManager::detach (juce::Component& root)
{
    if (&root.getLookAndFeel() == &laf)
        root.setLookAndFeel (nullptr);
    roots.erase (std::remove_if (roots.begin(), roots.end(),
                                 [&root] (const auto& r) { return r.getComponent() == &root || r.getComponent() == nullptr; }),
                 roots.end());
}

void ThemeManager::applyUiScale (juce::Component& root, float scale)
{
    scale = juce::jlimit (metrics::minUiScale, metrics::maxUiScale, scale);
    if (auto* editor = dynamic_cast<juce::AudioProcessorEditor*> (&root))
        editor->setScaleFactor (scale);
    else
        root.setTransform (juce::AffineTransform::scale (scale));
}
} // namespace snaral::ui
