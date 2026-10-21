#include "OptionsPanel.h"
#include "HelpContent.h"

namespace snaral::ui
{
const std::vector<float>& OptionsPanel::scaleChoices()
{
    static const std::vector<float> v { 0.75f, 0.9f, 1.0f, 1.1f, 1.25f, 1.5f, 1.75f, 2.0f };
    return v;
}

//==================================================================================================================
class OptionsPanel::Content : public juce::Component
{
public:
    Content (OptionsPanel& o) : owner (o)
    {
        auto& cfg = owner.config;
        auto& model = owner.options;

        themeLabelTitle = "Theme";
        for (std::size_t i = 0; i < ThemeCatalog::all().size(); ++i)
            themeCombo.addItem (ThemeCatalog::all()[i].name, static_cast<int> (i) + 1);
        themeCombo.setTitle ("Theme");
        themeCombo.setDescription ("Choose the colour theme. It is saved for your user account and applies to every panel.");
        themeCombo.setTooltip ("Colour theme");
        themeCombo.onChange = [this] { owner.options.setThemeId (ThemeCatalog::all()[static_cast<std::size_t> (themeCombo.getSelectedId() - 1)].id); repaint(); };
        addAndMakeVisible (themeCombo);

        for (std::size_t i = 0; i < scaleChoices().size(); ++i)
            scaleCombo.addItem (juce::String (juce::roundToInt (scaleChoices()[i] * 100.0f)) + "%", static_cast<int> (i) + 1);
        scaleCombo.setTitle ("UI scale");
        scaleCombo.setDescription ("Scales text and controls together. Larger values make everything bigger.");
        scaleCombo.setTooltip ("Interface size");
        scaleCombo.onChange = [this] { owner.options.setUiScale (scaleChoices()[static_cast<std::size_t> (scaleCombo.getSelectedId() - 1)]); };
        addAndMakeVisible (scaleCombo);

        auto setupToggle = [this] (juce::ToggleButton& b, const juce::String& text, const juce::String& tip, std::function<void (bool)> apply)
        {
            b.setButtonText (text);
            b.setTitle (text);
            b.setTooltip (tip);
            b.setWantsKeyboardFocus (true);
            b.setDescription (tip);
            b.onClick = [&b, apply] { apply (b.getToggleState()); };
            addAndMakeVisible (b);
        };
        setupToggle (tooltips, "Show tooltips", "Show descriptive tooltips when hovering over controls. Accessible names stay available when off.",
                     [this] (bool v) { owner.options.setTooltipsEnabled (v); });
        setupToggle (animations, "Animations", "Turn off for reduced motion: meters and the scope update twice a second and nothing pulses.",
                     [this] (bool v) { owner.options.setAnimationsEnabled (v); });
        setupToggle (scope, "Show signal scope", "Show or hide the output waveform display. Hidden displays do no drawing work.",
                     [this] (bool v) { owner.options.setScopeVisible (v); });
        setupToggle (cpu, "Show DSP CPU gauge", "Show or hide the Snaral DSP CPU gauge.",
                     [this] (bool v) { owner.options.setCpuGaugeVisible (v); });

        tourButton.setTooltip ("Replay the first-run introduction");
        tourButton.onClick = [this] { if (owner.config.onRerunTour) owner.config.onRerunTour(); };
        addChildComponent (tourButton);
        tourButton.setVisible (static_cast<bool> (cfg.onRerunTour));

        helpButton.setTooltip ("Open the help pages");
        helpButton.onClick = [this] { if (owner.config.onOpenHelp) owner.config.onOpenHelp(); };
        addChildComponent (helpButton);
        helpButton.setVisible (static_cast<bool> (cfg.onOpenHelp));

        websiteButton.setTooltip ("Opens the Circuit Drift Labs website in your browser");
        websiteButton.onClick = [this]
        {
            const juce::String url (HelpContent::cdlWebsite);
            if (owner.config.onOpenUrl) owner.config.onOpenUrl (url); else juce::URL (url).launchInDefaultBrowser();
        };
        addAndMakeVisible (websiteButton);

        if (cfg.standalone)
        {
            if (cfg.createDeviceSelector)
            {
                deviceSelector = cfg.createDeviceSelector();
                if (deviceSelector != nullptr)
                {
                    deviceSelector->setTitle ("Audio and MIDI devices");
                    addAndMakeVisible (*deviceSelector);
                }
            }
            toneButton = std::make_unique<juce::TextButton> ("Start 440 Hz reference tone");
            toneButton->setTitle ("440 Hz reference tone");
            toneButton->setDescription ("Plays a 440 Hz sine at minus 12 dBFS so you can test the output path. It only plays while you leave it on.");
            toneButton->setTooltip ("440 Hz sine at -12 dBFS for testing the output path. Never starts by itself.");
            toneButton->onClick = [this] { owner.setReferenceToneRunning (! owner.toneRunning, true); };
            addAndMakeVisible (*toneButton);
        }
        syncFromModel();
        juce::ignoreUnused (model);
    }

    void syncFromModel()
    {
        auto& m = owner.options;
        const auto& themes = ThemeCatalog::all();
        for (std::size_t i = 0; i < themes.size(); ++i)
            if (themes[i].id == m.getThemeId())
                themeCombo.setSelectedId (static_cast<int> (i) + 1, juce::dontSendNotification);
        int best = 3;
        for (std::size_t i = 0; i < scaleChoices().size(); ++i)
            if (std::abs (scaleChoices()[i] - m.getUiScale()) < 0.02f)
                best = static_cast<int> (i) + 1;
        scaleCombo.setSelectedId (best, juce::dontSendNotification);
        tooltips.setToggleState (m.areTooltipsEnabled(), juce::dontSendNotification);
        animations.setToggleState (m.areAnimationsEnabled(), juce::dontSendNotification);
        scope.setToggleState (m.isScopeVisible(), juce::dontSendNotification);
        cpu.setToggleState (m.isCpuGaugeVisible(), juce::dontSendNotification);
        if (toneButton != nullptr)
        {
            toneButton->setButtonText (owner.toneRunning ? "Stop reference tone" : "Start 440 Hz reference tone (-12 dBFS)");
            toneButton->setToggleState (owner.toneRunning, juce::dontSendNotification);
        }
        repaint();
    }

    // layout ---------------------------------------------------------------------------------------------------
    int layout (int width, bool apply)
    {
        const int pad = 16;
        const int labelW = 150;
        const int rowH = 32;
        int y = 10;
        auto place = [&] (juce::Component& c, juce::Rectangle<int> r) { if (apply) c.setBounds (r); };
        auto heading = [&] (const juce::String& text)
        {
            if (apply) headings.add ({ text, y });
            y += 30;
        };
        if (apply) headings.clear(), notes.clear();

        if (apply) headings.add ({ "OPTIONS", -1 });
        y += 30;                                                     // title row

        heading ("APPEARANCE");
        place (themeCombo, { pad + labelW, y, juce::jmin (220, width - pad * 2 - labelW), 28 });
        if (apply) rowLabels.set ("Theme", juce::String (y));
        y += rowH;
        if (apply) notes.add ({ ThemeCatalog::byId (owner.options.getThemeId()).description, y - 2, true });
        y += 22;
        place (scaleCombo, { pad + labelW, y, 120, 28 });
        if (apply) rowLabels.set ("UI scale", juce::String (y));
        y += rowH + 8;

        heading ("BEHAVIOUR");
        place (tooltips, { pad, y, juce::jmin (320, width - 2 * pad), 28 });
        y += rowH;
        place (animations, { pad, y, juce::jmin (320, width - 2 * pad), 28 });
        y += rowH;
        if (apply) notes.add ({ "Off = reduced motion. Meters and the scope update twice a second, nothing pulses.", y - 2, false });
        y += 22 + 8;

        heading ("DISPLAYS");
        place (scope, { pad, y, juce::jmin (320, width - 2 * pad), 28 });
        y += rowH;
        place (cpu, { pad, y, juce::jmin (320, width - 2 * pad), 28 });
        y += rowH + 8;

        if (tourButton.isVisible() || helpButton.isVisible())
        {
            heading ("HELP AND INTRODUCTION");
            int x = pad;
            if (tourButton.isVisible()) { place (tourButton, { x, y, 190, 28 }); x += 198; }
            if (helpButton.isVisible()) place (helpButton, { x, y, 140, 28 });
            y += rowH + 8;
        }

        heading ("AUDIO AND MIDI");
        if (owner.config.standalone)
        {
            if (deviceSelector != nullptr)
            {
                const int h = 250;
                place (*deviceSelector, { pad, y, width - 2 * pad, h });
                y += h + 8;
            }
            else
            {
                if (apply) notes.add ({ "No device selector was supplied by the application.", y, false });
                y += 24;
            }
            if (toneButton != nullptr)
            {
                place (*toneButton, { pad, y, 280, 28 });
                if (apply) notes.add ({ owner.toneRunning ? "Reference tone is playing. Press the button to stop it." : "Reference tone is stopped.", y + 30, false, owner.toneRunning });
                y += 28 + 26 + 8;
            }
        }
        else
        {
            if (apply) notes.add ({ "Audio device, sample rate and buffer size are controlled by your host.", y, false });
            y += 30;
        }

        heading ("ABOUT");
        if (apply) notes.add ({ owner.config.productName + "  " + owner.config.versionText
                                    + (owner.config.formatText.isNotEmpty() ? "  (" + owner.config.formatText + ")" : juce::String()),
                                y, false, false, true });
        y += 26;
        place (websiteButton, { pad, y, 230, 28 });
        y += rowH + 12;
        return y;
    }

    void resized() override { layout (getWidth(), true); }

    void paint (juce::Graphics& g) override
    {
        const auto& t = SnaralLookAndFeel::tokensFor (*this);
        for (const auto& h : headings)
        {
            if (h.y < 0)
            {
                g.setFont (fonts::title());
                g.setColour (t.textPrimary);
                g.drawText ("Options", 16, 6, getWidth() - 32, 24, juce::Justification::centredLeft, false);
                continue;
            }
            g.setFont (fonts::label());
            g.setColour (t.textMuted);
            g.drawText (h.text, 16, h.y + 4, getWidth() - 32, 18, juce::Justification::centredLeft, false);
            g.setColour (t.border);
            g.drawHorizontalLine (h.y + 24, 16.0f, static_cast<float> (getWidth() - 16));
        }
        for (int i = 0; i < rowLabels.size(); ++i)
        {
            g.setFont (fonts::body());
            g.setColour (t.textPrimary);
            g.drawText (rowLabels.getAllKeys()[i], 16, rowLabels.getAllValues()[i].getIntValue(), 140, 28, juce::Justification::centredLeft, false);
        }
        for (const auto& n : notes)
        {
            int x = 16;
            if (n.warn)
            {
                SnaralLookAndFeel::drawSeverityIcon (g, Severity::Warning, juce::Rectangle<float> (16.0f, static_cast<float> (n.y) + 2.0f, 16.0f, 16.0f), t.warning);
                x = 38;
            }
            g.setFont (n.bold ? fonts::heading() : fonts::small());
            g.setColour (n.bold ? t.textPrimary : (n.warn ? t.warning : t.textMuted));
            g.drawFittedText (n.text, juce::Rectangle<int> (n.indentToControls ? 16 + 150 : x, n.y, getWidth() - x - 16, 20),
                              juce::Justification::centredLeft, 1, 0.9f);
        }

        // theme preview swatches next to the theme combo
        const auto& th = ThemeCatalog::byId (owner.options.getThemeId());
        const juce::Colour swatches[] = { th.background, th.panel, th.accent, th.secondary, th.textPrimary, th.success, th.warning, th.error };
        const int sx = themeCombo.getRight() + 12;
        for (int i = 0; i < 8; ++i)
        {
            const auto r = juce::Rectangle<float> (static_cast<float> (sx + i * 18), static_cast<float> (themeCombo.getY() + 6), 14.0f, 16.0f);
            if (r.getRight() > static_cast<float> (getWidth() - 8))
                break;
            g.setColour (swatches[i]);
            g.fillRoundedRectangle (r, 3.0f);
            g.setColour (t.outline);
            g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);
        }
    }

    struct Heading { juce::String text; int y; };
    struct Note { juce::String text; int y; bool indentToControls; bool warn = false; bool bold = false; };

    OptionsPanel& owner;
    juce::String themeLabelTitle;
    juce::ComboBox themeCombo, scaleCombo;
    juce::ToggleButton tooltips, animations, scope, cpu;
    juce::TextButton tourButton { "Run introduction again" }, helpButton { "Open help" }, websiteButton { "Circuit Drift Labs website" };
    std::unique_ptr<juce::Component> deviceSelector;
    std::unique_ptr<juce::TextButton> toneButton;
    juce::Array<Heading> headings;
    juce::Array<Note> notes;
    juce::StringPairArray rowLabels;
};

//==================================================================================================================
OptionsPanel::OptionsPanel (OptionsModel& m, Config c) : options (m), config (std::move (c))
{
    content = std::make_unique<Content> (*this);
    viewport.setViewedComponent (content.get(), false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (12);
    viewport.setTitle ("Options");
    addAndMakeVisible (viewport);

    if (config.onClose)
    {
        closeButton = std::make_unique<juce::TextButton> ("Close");
        closeButton->onClick = [this] { if (config.onClose) config.onClose(); };
        addAndMakeVisible (*closeButton);
    }
    options.addListener (this);
    setTitle ("Options");
    setSize (560, 440);
}

OptionsPanel::~OptionsPanel()
{
    options.removeListener (this);
    viewport.setViewedComponent (nullptr, false);
    if (toneRunning && config.onReferenceTone)
        config.onReferenceTone (false);                  // never leave a test tone running behind a closed panel
}

void OptionsPanel::optionChanged (const juce::Identifier&)
{
    content->syncFromModel();
    content->resized();
}

void OptionsPanel::setReferenceToneRunning (bool running, bool notify)
{
    if (! config.standalone || toneRunning == running)
        return;
    toneRunning = running;
    if (notify && config.onReferenceTone)
        config.onReferenceTone (running);
    content->syncFromModel();
    content->resized();
}

int OptionsPanel::getContentHeight() const { return content->layout (juce::jmax (1, content->getWidth()), false); }

void OptionsPanel::resized()
{
    auto r = getLocalBounds();
    if (closeButton != nullptr)
    {
        auto f = r.removeFromBottom (44).reduced (16, 8);
        closeButton->setBounds (f.removeFromRight (90));
    }
    viewport.setBounds (r);
    const int w = juce::jmax (1, r.getWidth() - viewport.getScrollBarThickness());
    content->setSize (w, juce::jmax (r.getHeight(), content->layout (w, false)));
    content->resized();
}

void OptionsPanel::paint (juce::Graphics& g)
{
    g.fillAll (SnaralLookAndFeel::tokensFor (*this).panel);
}

juce::ComboBox& OptionsPanel::getThemeCombo() noexcept          { return content->themeCombo; }
juce::ComboBox& OptionsPanel::getScaleCombo() noexcept          { return content->scaleCombo; }
juce::ToggleButton& OptionsPanel::getTooltipsToggle() noexcept  { return content->tooltips; }
juce::ToggleButton& OptionsPanel::getAnimationsToggle() noexcept { return content->animations; }
juce::ToggleButton& OptionsPanel::getScopeToggle() noexcept     { return content->scope; }
juce::ToggleButton& OptionsPanel::getCpuToggle() noexcept       { return content->cpu; }
juce::Button* OptionsPanel::getReferenceToneButton() noexcept   { return content->toneButton.get(); }
} // namespace snaral::ui
