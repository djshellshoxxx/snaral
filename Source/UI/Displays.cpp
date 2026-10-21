#include "Displays.h"

#include <cmath>

namespace snaral::ui
{
//==================================================================================================================
// LiveDisplay
//==================================================================================================================
LiveDisplay::~LiveDisplay()
{
    stopTimer();
}

int LiveDisplay::getRefreshRateHz() const noexcept
{
    return SnaralLookAndFeel::animationsFor (*this) ? baseRateHz() : 2;
}

void LiveDisplay::setDisplayEnabled (bool e)
{
    displayEnabled = e;
    setVisible (e);
    updateTimer();
}

void LiveDisplay::updateTimer()
{
    if (displayEnabled && isShowing())
    {
        const int hz = getRefreshRateHz();
        if (! isTimerRunning() || getTimerInterval() != 1000 / hz)
        {
            lastTickMs = 0.0;
            startTimerHz (hz);
        }
    }
    else
        stopTimer();
}

void LiveDisplay::visibilityChanged()        { updateTimer(); }
void LiveDisplay::parentHierarchyChanged()   { updateTimer(); }
void LiveDisplay::lookAndFeelChanged()       { updateTimer(); repaint(); }

void LiveDisplay::refreshNow (double dt)     { refresh (dt); }

void LiveDisplay::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    double dt = lastTickMs > 0.0 ? (now - lastTickMs) / 1000.0 : 1.0 / static_cast<double> (getRefreshRateHz());
    lastTickMs = now;
    refresh (juce::jlimit (0.0, 0.5, dt));
}

//==================================================================================================================
// ScopeView
//==================================================================================================================
ScopeView::ScopeView()
{
    setTitle ("Signal scope");
    setDescription ("Live waveform of recent output. A display aid only; it does not indicate sound quality.");
    setTooltip ("Recent output waveform (post-limiter), refreshed about 30 times per second. Display only.");

    struct Spec { juce::TextButton* b; const char* tip; };
    for (auto s : { Spec { &outButton, "Show the post-limiter stereo output" },
                    Spec { &subButton, "Show only the sub layer (diagnosis)" },
                    Spec { &midButton, "Show only the mid layer (diagnosis)" } })
    {
        s.b->setClickingTogglesState (true);
        s.b->setRadioGroupId (0x5c0e);
        s.b->setTooltip (s.tip);
        s.b->setTitle (juce::String (s.b->getButtonText()) + " view");
        s.b->setWantsKeyboardFocus (true);
        addAndMakeVisible (*s.b);
    }
    outButton.setToggleState (true, juce::dontSendNotification);
    outButton.onClick = [this] { setTap (ScopeTap::Output); };
    subButton.onClick = [this] { setTap (ScopeTap::Sub); };
    midButton.onClick = [this] { setTap (ScopeTap::Mid); };

    cycleButton.setTooltip ("Switch scope view: output, sub layer, mid layer");
    cycleButton.setTitle ("Scope view");
    cycleButton.setWantsKeyboardFocus (true);
    cycleButton.onClick = [this]
    {
        auto next = [] (ScopeTap t) { return t == ScopeTap::Output ? ScopeTap::Sub : (t == ScopeTap::Sub ? ScopeTap::Mid : ScopeTap::Output); };
        auto t = next (tap);
        for (int i = 0; i < 3 && ! (t == ScopeTap::Output || (source != nullptr && source->hasTap (t))); ++i)
            t = next (t);
        setTap (t);
    };
    addChildComponent (cycleButton);

    monoButton.setClickingTogglesState (true);
    monoButton.setTooltip ("Sum left and right to one trace");
    monoButton.setTitle ("Mono sum");
    monoButton.setWantsKeyboardFocus (true);
    monoButton.onClick = [this] { setMono (monoButton.getToggleState()); };
    addAndMakeVisible (monoButton);
    setSize (320, headerHeight + 100);
}

ScopeView::~ScopeView() = default;

void ScopeView::setSource (ScopeSource* s)
{
    source = s;
    haveData = false;
    updateButtons();
    repaint();
}

void ScopeView::setTap (ScopeTap t)
{
    tap = t;
    outButton.setToggleState (t == ScopeTap::Output, juce::dontSendNotification);
    subButton.setToggleState (t == ScopeTap::Sub, juce::dontSendNotification);
    midButton.setToggleState (t == ScopeTap::Mid, juce::dontSendNotification);
    cycleButton.setButtonText (t == ScopeTap::Output ? "OUT" : (t == ScopeTap::Sub ? "SUB" : "MID"));
    haveData = false;
    repaint();
}

void ScopeView::setMono (bool m)
{
    mono = m;
    monoButton.setToggleState (m, juce::dontSendNotification);
    repaint();
}

void ScopeView::updateButtons()
{
    subButton.setEnabled (source != nullptr && source->hasTap (ScopeTap::Sub));
    midButton.setEnabled (source != nullptr && source->hasTap (ScopeTap::Mid));
    if (! subButton.isEnabled() && tap == ScopeTap::Sub) setTap (ScopeTap::Output);
    if (! midButton.isEnabled() && tap == ScopeTap::Mid) setTap (ScopeTap::Output);
}

juce::String ScopeView::caption() const
{
    switch (tap)
    {
        case ScopeTap::Output: return "Output, post-limiter";
        case ScopeTap::Sub:    return "Sub layer";
        case ScopeTap::Mid:    return "Mid layer";
    }
    return {};
}

void ScopeView::resized()
{
    auto h = getLocalBounds().removeFromTop (headerHeight);
    const int w = h.getWidth();
    constexpr int fullWidth = 3 * 56 + 64 + 3 * 4;
    const bool full = w >= fullWidth;
    const bool compact = ! full && w >= 100;         // one cycling tap button instead of OUT / SUB / MID / MONO

    for (auto* b : { &outButton, &subButton, &midButton, &monoButton })
        b->setVisible (full);
    cycleButton.setVisible (compact);

    if (full)
    {
        monoButton.setBounds (h.removeFromRight (64));
        h.removeFromRight (4);
        midButton.setBounds (h.removeFromRight (56));
        h.removeFromRight (4);
        subButton.setBounds (h.removeFromRight (56));
        h.removeFromRight (4);
        outButton.setBounds (h.removeFromRight (56));
    }
    else if (compact)
    {
        cycleButton.setBounds (h.removeFromRight (juce::jmin (64, w)));
    }
}

void ScopeView::refresh (double)
{
    if (source == nullptr)
    {
        if (haveData) { haveData = false; repaint(); }
        return;
    }
    updateButtons();

    const double sr = juce::jmax (8000.0, source->getSampleRate());
    const int window = juce::jlimit (128, 8192, static_cast<int> (sr * windowSeconds));
    const int want = window * 2;
    rawL.assign (static_cast<std::size_t> (want), 0.0f);
    rawR.assign (static_cast<std::size_t> (want), 0.0f);
    const int nL = source->pullLatest (tap, 0, rawL.data(), want);
    const int nR = source->pullLatest (tap, 1, rawR.data(), want);
    const int n = juce::jmin (nL, nR);
    if (n < window)
    {
        haveData = false;
        repaint();
        return;
    }

    // rising zero-crossing trigger within the older half; fall back to the newest window
    const float* l = rawL.data();
    const float* r = rawR.data();
    int start = n - window;
    for (int i = 1; i < n - window; ++i)
    {
        const float a = 0.5f * (l[i - 1] + r[i - 1]), b = 0.5f * (l[i] + r[i]);
        if (a < 0.0f && b >= 0.0f)
        {
            start = i;
            break;
        }
    }
    shownL.assign (l + start, l + start + window);
    shownR.assign (r + start, r + start + window);
    haveData = true;
    repaint();
}

void ScopeView::paint (juce::Graphics& g)
{
    const auto& t = SnaralLookAndFeel::tokensFor (*this);
    auto area = getLocalBounds();
    auto head = area.removeFromTop (headerHeight);

    // caption
    const int captionW = (cycleButton.isVisible() ? cycleButton.getX() : (outButton.isVisible() ? outButton.getX() : getWidth())) - 4;
    if (captionW > 70)
    {
        g.setFont (fonts::label());
        g.setColour (t.textMuted);
        auto text = caption().toUpperCase();
        if (textWidth (fonts::label(), text) > static_cast<float> (captionW - 2))
            text = text.upToFirstOccurrenceOf (",", false, false).trim();     // "OUTPUT, POST-LIMITER" -> "OUTPUT"
        g.drawFittedText (text, head.withWidth (captionW).withTrimmedLeft (2), juce::Justification::centredLeft, 1, 0.8f);
    }

    const auto trace = area.toFloat().reduced (0.5f);
    g.setColour (t.background);
    g.fillRoundedRectangle (trace, 4.0f);
    g.setColour (t.border);
    g.drawRoundedRectangle (trace, 4.0f, t.borderThickness);

    const auto inner = trace.reduced (6.0f, 8.0f);
    for (const float v : { -0.5f, 0.0f, 0.5f })
    {
        const float y = inner.getCentreY() - v * inner.getHeight() * 0.5f;
        g.setColour (v == 0.0f ? t.border : t.border.withAlpha (0.5f));
        g.drawHorizontalLine (juce::roundToInt (y), inner.getX(), inner.getRight());
    }

    if (source == nullptr || ! haveData || shownL.empty())
    {
        g.setFont (fonts::body());
        g.setColour (t.textMuted);
        g.drawFittedText (source == nullptr ? "No signal source" : "No signal data", trace.toNearestInt(),
                          juce::Justification::centred, 1, 1.0f);
        return;
    }

    auto makePath = [&] (const std::vector<float>& a, const std::vector<float>* b)
    {
        juce::Path p;
        const int w = juce::jmax (2, static_cast<int> (inner.getWidth()));
        const auto n = static_cast<int> (a.size());
        bool first = true;
        for (int x = 0; x < w; ++x)
        {
            const int i0 = x * n / w;
            const int i1 = juce::jmax (i0 + 1, (x + 1) * n / w);
            float lo = 1.0e9f, hi = -1.0e9f;
            for (int i = i0; i < i1 && i < n; ++i)
            {
                const float v = b != nullptr ? 0.5f * (a[static_cast<std::size_t> (i)] + (*b)[static_cast<std::size_t> (i)])
                                             : a[static_cast<std::size_t> (i)];
                lo = juce::jmin (lo, v);
                hi = juce::jmax (hi, v);
            }
            const float px = inner.getX() + static_cast<float> (x);
            auto yOf = [&] (float v) { return inner.getCentreY() - juce::jlimit (-1.0f, 1.0f, v) * inner.getHeight() * 0.5f; };
            if (first) { p.startNewSubPath (px, yOf (hi)); first = false; }
            else        p.lineTo (px, yOf (hi));
            if (lo < hi)
                p.lineTo (px, yOf (lo));
        }
        return p;
    };

    g.saveState();
    g.reduceClipRegion (trace.toNearestInt());
    if (mono)
    {
        g.setColour (t.accent);
        g.strokePath (makePath (shownL, &shownR), juce::PathStrokeType (1.6f));
    }
    else
    {
        g.setColour (t.secondary);
        const float dashes[] = { 4.0f, 3.0f };
        juce::Path dashed;
        juce::PathStrokeType (1.2f).createDashedStroke (dashed, makePath (shownR, nullptr), dashes, 2);
        g.fillPath (dashed);
        g.setColour (t.accent);
        g.strokePath (makePath (shownL, nullptr), juce::PathStrokeType (1.6f));
    }
    g.restoreState();

    // legend (shape + text, not only colour)
    g.setFont (fonts::small());
    auto legend = trace.reduced (8.0f, 4.0f).removeFromBottom (14.0f);
    if (mono)
    {
        g.setColour (t.accent);
        g.drawLine (legend.getX(), legend.getCentreY(), legend.getX() + 14.0f, legend.getCentreY(), 2.0f);
        g.setColour (t.textMuted);
        g.drawText ("L+R", legend.withTrimmedLeft (18.0f).toNearestInt(), juce::Justification::centredLeft, false);
    }
    else
    {
        g.setColour (t.accent);
        g.drawLine (legend.getX(), legend.getCentreY(), legend.getX() + 14.0f, legend.getCentreY(), 2.0f);
        g.setColour (t.textMuted);
        g.drawText ("L", legend.withTrimmedLeft (18.0f).withWidth (22.0f).toNearestInt(), juce::Justification::centredLeft, false);
        g.setColour (t.secondary);
        const float x2 = legend.getX() + 44.0f;
        g.drawLine (x2, legend.getCentreY(), x2 + 5.0f, legend.getCentreY(), 1.6f);
        g.drawLine (x2 + 9.0f, legend.getCentreY(), x2 + 14.0f, legend.getCentreY(), 1.6f);
        g.setColour (t.textMuted);
        g.drawText ("R", legend.withTrimmedLeft (x2 - legend.getX() + 18.0f).withWidth (22.0f).toNearestInt(), juce::Justification::centredLeft, false);
    }
}

std::unique_ptr<juce::AccessibilityHandler> ScopeView::createAccessibilityHandler()
{
    return makeReadoutAccessibilityHandler (*this, juce::AccessibilityRole::group, [this]
    {
        if (source == nullptr) return juce::String ("No signal source");
        if (! haveData)        return juce::String ("No signal data");
        float peak = 0.0f;
        for (const float v : shownL) peak = juce::jmax (peak, std::abs (v));
        for (const float v : shownR) peak = juce::jmax (peak, std::abs (v));
        return caption() + ", window peak " + juce::String (MeterBallistics::toDb (peak), 1) + " dB full scale";
    });
}

//==================================================================================================================
// Meter
//==================================================================================================================
void MeterBallistics::update (double dt, float newPeakLinear, float newRmsLinear) noexcept
{
    const float pk = toDb (newPeakLinear);
    const float rm = toDb (newRmsLinear);
    const float fall = barDecayDbPerSecond * static_cast<float> (dt);
    peakDb = juce::jmax (pk, peakDb - fall);
    rmsDb = juce::jmax (rm, rmsDb - fall);

    if (pk >= holdDb)
    {
        holdDb = pk;
        sinceHold = 0.0;
    }
    else
    {
        sinceHold += dt;
        if (sinceHold > holdSeconds)
            holdDb = juce::jmax (peakDb, holdDb - holdDecayDbPerSecond * static_cast<float> (dt));
    }
}

LevelMeter::LevelMeter (Orientation o) : orientation (o)
{
    setTitle ("Output level meter");
    setDescription ("Peak and RMS output level in dB full scale. Click to clear the clip indicator.");
    setTooltip ("Output level in dBFS: bars show RMS with the recent peak behind, the tick holds the peak for 1.5 s. "
                "CLIP appears only when the output reached 0 dBFS.");
    setSize (orientation == Orientation::Vertical ? 72 : 220, orientation == Orientation::Vertical ? 150 : 44);
}

LevelMeter::~LevelMeter() = default;

void LevelMeter::setSource (MeterSource* s)
{
    source = s;
    valid = false;
    balL.reset();
    balR.reset();
    clipLatchMs = 0.0;
    repaint();
}

void LevelMeter::refresh (double dt)
{
    if (source == nullptr)
    {
        if (valid) { valid = false; repaint(); }
        return;
    }
    const auto m = source->readAndReset();
    valid = m.valid;
    if (! m.valid)
    {
        balL.reset();
        balR.reset();
        clipLatchMs = 0.0;           // never keep a clip flag alive without fresh data
        repaint();
        return;
    }
    balL.update (dt, m.peak[0], m.rms[0]);
    balR.update (dt, m.peak[1], m.rms[1]);
    if (m.peak[0] >= 0.99999f || m.peak[1] >= 0.99999f)
        clipLatchMs = 2000.0;
    else
        clipLatchMs = juce::jmax (0.0, clipLatchMs - dt * 1000.0);
    repaint();
}

juce::String LevelMeter::getReadoutText() const
{
    if (! valid)
        return "No level data";
    return "Peak " + juce::String (getHeldPeakDb(), 1) + " dBFS, RMS "
           + juce::String (juce::jmax (balL.rmsDb, balR.rmsDb), 1) + " dBFS" + (isClipping() ? ", clipping" : "");
}

std::unique_ptr<juce::AccessibilityHandler> LevelMeter::createAccessibilityHandler()
{
    return makeReadoutAccessibilityHandler (*this, juce::AccessibilityRole::progressBar, [this] { return getReadoutText(); });
}

void LevelMeter::mouseDown (const juce::MouseEvent&)
{
    clearClip();
}

void LevelMeter::drawBar (juce::Graphics& g, const ThemeTokens& t, juce::Rectangle<float> bar, const MeterBallistics& b) const
{
    const bool vertical = orientation == Orientation::Vertical;
    const float length = vertical ? bar.getHeight() : bar.getWidth();
    constexpr float segment = 4.0f, gap = 1.0f;
    const int n = juce::jmax (1, static_cast<int> (length / (segment + gap)));
    auto posOf = [] (float db) { return juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f); };
    const float rmsPos = valid ? posOf (b.rmsDb) : 0.0f;
    const float pkPos = valid ? posOf (b.peakDb) : 0.0f;

    for (int i = 0; i < n; ++i)
    {
        const float p0 = static_cast<float> (i) / static_cast<float> (n);
        const float p1 = static_cast<float> (i + 1) / static_cast<float> (n);
        juce::Rectangle<float> seg;
        if (vertical)
            seg = { bar.getX(), bar.getBottom() - p1 * length + gap, bar.getWidth(), (p1 - p0) * length - gap };
        else
            seg = { bar.getX() + p0 * length, bar.getY(), (p1 - p0) * length - gap, bar.getHeight() };

        const auto colour = t.meterColour (0.5f * (p0 + p1));
        if (p1 <= rmsPos + 0.0001f)
            g.setColour (colour);
        else if (p1 <= pkPos + 0.0001f)
            g.setColour (colour.withAlpha (0.38f));
        else
            g.setColour (t.outline.withAlpha (0.35f));
        g.fillRoundedRectangle (seg, 1.0f);
    }

    if (valid && b.holdDb > MeterBallistics::floorDb + 1.0f)             // held-peak tick
    {
        const float hp = posOf (b.holdDb);
        g.setColour (t.textPrimary);
        if (vertical)
            g.fillRect (bar.getX() - 2.0f, bar.getBottom() - hp * length - 1.0f, bar.getWidth() + 4.0f, 2.0f);
        else
            g.fillRect (bar.getX() + hp * length - 1.0f, bar.getY() - 2.0f, 2.0f, bar.getHeight() + 4.0f);
    }
}

void LevelMeter::paint (juce::Graphics& g)
{
    const auto& t = SnaralLookAndFeel::tokensFor (*this);
    auto area = getLocalBounds().toFloat();
    const bool vertical = orientation == Orientation::Vertical;

    // CLIP chip: text + icon, only from fresh data
    auto chipRow = vertical ? area.removeFromTop (18.0f) : area.removeFromLeft (52.0f);
    if (isClipping())
    {
        auto chip = (vertical ? chipRow.withSizeKeepingCentre (chipRow.getWidth() - 4.0f, 16.0f)
                              : chipRow.withSizeKeepingCentre (48.0f, 20.0f));
        g.setColour (t.error);
        g.fillRoundedRectangle (chip, 3.0f);
        g.setColour (t.background);
        SnaralLookAndFeel::drawSeverityIcon (g, Severity::Error, chip.removeFromLeft (16.0f).reduced (2.0f), t.background);
        g.setFont (fonts::label());
        g.drawText ("CLIP", chip.toNearestInt(), juce::Justification::centred, false);
    }

    const auto valueFont = fonts::value().withHeight (11.5f);
    juce::String pk = valid ? juce::String (getHeldPeakDb(), 1) : juce::String ("--");
    juce::String rm = valid ? juce::String (juce::jmax (balL.rmsDb, balR.rmsDb), 1) : juce::String ("--");
    if (valid && getHeldPeakDb() <= MeterBallistics::floorDb + 1.0f) pk = "-inf";
    if (valid && juce::jmax (balL.rmsDb, balR.rmsDb) <= MeterBallistics::floorDb + 1.0f) rm = "-inf";

    if (vertical)
    {
        auto readout = area.removeFromBottom (32.0f);
        g.setFont (valueFont);
        g.setColour (t.textPrimary);
        g.drawText ("PK " + pk, readout.removeFromTop (16.0f).toNearestInt(), juce::Justification::centred, false);
        g.setColour (t.textMuted);
        g.drawText ("RMS " + rm, readout.toNearestInt(), juce::Justification::centred, false);

        area.removeFromTop (2.0f);
        area.removeFromBottom (2.0f);
        const float barW = 14.0f;
        const float x0 = area.getX() + 2.0f;
        drawBar (g, t, { x0, area.getY(), barW, area.getHeight() }, balL);
        drawBar (g, t, { x0 + barW + 4.0f, area.getY(), barW, area.getHeight() }, balR);

        // scale labels (dBFS)
        g.setFont (fonts::small());
        g.setColour (t.textMuted);
        const float lx = x0 + 2.0f * barW + 10.0f;
        for (const int db : { 0, -12, -24, -36, -48 })
        {
            const float y = area.getBottom() - (static_cast<float> (db) + 60.0f) / 60.0f * area.getHeight();
            g.drawText (juce::String (db), juce::Rectangle<float> (lx, y - 7.0f, area.getRight() - lx, 14.0f).toNearestInt(),
                        juce::Justification::centredLeft, false);
        }
    }
    else
    {
        auto readout = area.removeFromRight (74.0f);
        g.setFont (valueFont);
        g.setColour (t.textPrimary);
        g.drawText ("PK " + pk, readout.removeFromTop (readout.getHeight() * 0.5f).toNearestInt(), juce::Justification::centredLeft, false);
        g.setColour (t.textMuted);
        g.drawText ("RMS " + rm, readout.toNearestInt(), juce::Justification::centredLeft, false);

        area.reduce (4.0f, 4.0f);
        const float barH = juce::jmax (6.0f, (area.getHeight() - 14.0f) * 0.5f - 2.0f);
        drawBar (g, t, { area.getX(), area.getY(), area.getWidth(), barH }, balL);
        drawBar (g, t, { area.getX(), area.getY() + barH + 3.0f, area.getWidth(), barH }, balR);
    }
}

//==================================================================================================================
// CpuGauge
//==================================================================================================================
CpuGauge::CpuGauge()
{
    setTitle ("DSP CPU");
    setDescription ("Snaral DSP processing time as a percentage of the audio block deadline on one core.");
    setTooltip ("DSP CPU: time Snaral spends per audio block divided by the time available, smoothed over 1 s. "
                "It is not the host's total CPU use.");
    setSize (150, 38);
}

CpuGauge::~CpuGauge() = default;

void CpuGauge::setSource (CpuSource* s)
{
    source = s;
    valid = false;
    smoothed = 0.0f;
    overrunRemaining = 0.0;
    repaint();
}

void CpuGauge::refresh (double dt)
{
    if (source == nullptr)
    {
        if (valid) { valid = false; repaint(); }
        return;
    }
    const auto r = source->read();
    if (! r.valid)
    {
        valid = false;
        smoothed = 0.0f;
        repaint();
        return;
    }
    if (! valid)
        smoothed = r.load;
    else
        smoothed += (r.load - smoothed) * static_cast<float> (1.0 - std::exp (-dt / smoothingSeconds));
    valid = true;
    if (r.overrunSinceLastRead)
        overrunRemaining = overrunShowSeconds;
    else
        overrunRemaining = juce::jmax (0.0, overrunRemaining - dt);
    repaint();
}

juce::String CpuGauge::getReadoutText() const
{
    if (! valid)
        return "DSP CPU unavailable";
    return "DSP CPU " + (smoothed > 1.0f ? juce::String (">100") : juce::String (juce::roundToInt (smoothed * 100.0f))) + " percent"
           + (isOverrunShown() ? ", overrun detected" : "");
}

std::unique_ptr<juce::AccessibilityHandler> CpuGauge::createAccessibilityHandler()
{
    return makeReadoutAccessibilityHandler (*this, juce::AccessibilityRole::progressBar, [this] { return getReadoutText(); });
}

void CpuGauge::paint (juce::Graphics& g)
{
    const auto& t = SnaralLookAndFeel::tokensFor (*this);
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    auto top = r.removeFromTop (18.0f);

    g.setFont (fonts::label());
    g.setColour (t.textPrimary);
    g.drawText ("DSP CPU", top.toNearestInt(), juce::Justification::centredLeft, false);

    g.setFont (fonts::value());
    if (isOverrunShown())
    {
        SnaralLookAndFeel::drawSeverityIcon (g, Severity::Warning, juce::Rectangle<float> (top.getRight() - 78.0f, top.getY() + 1.0f, 16.0f, 16.0f), t.warning);
        g.setColour (t.warning);
        g.setFont (fonts::label());
        g.drawText ("OVERRUN", top.withTrimmedLeft (top.getWidth() - 58.0f).toNearestInt(), juce::Justification::centredRight, false);
    }
    else
    {
        g.setColour (valid ? t.textPrimary : t.textMuted);
        g.drawText (valid ? (smoothed > 1.0f ? juce::String (">100%") : juce::String (juce::roundToInt (smoothed * 100.0f)) + "%")
                          : juce::String ("--"),
                    top.toNearestInt(), juce::Justification::centredRight, false);
    }

    auto track = r.removeFromTop (10.0f).withTrimmedTop (2.0f);
    g.setColour (t.outline.withAlpha (0.6f));
    g.fillRoundedRectangle (track, 3.0f);
    if (valid)
    {
        const float load = juce::jlimit (0.0f, 1.0f, smoothed);
        g.setColour (t.meterColour (load));
        g.fillRoundedRectangle (track.withWidth (juce::jmax (4.0f, track.getWidth() * load)), 3.0f);
    }
    g.setColour (isOverrunShown() ? t.warning : t.border);
    g.drawRoundedRectangle (track.reduced (0.5f), 3.0f, isOverrunShown() ? 1.5f : 1.0f);
}
} // namespace snaral::ui
