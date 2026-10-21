#pragma once

#include "Theme.h"
#include "UiSources.h"

namespace snaral::ui
{
/// Exposes a read-only text value (current meter reading etc.) to screen readers.
inline std::unique_ptr<juce::AccessibilityHandler> makeReadoutAccessibilityHandler (juce::Component& c,
                                                                                    juce::AccessibilityRole role,
                                                                                    std::function<juce::String()> text)
{
    struct TextValue final : public juce::AccessibilityTextValueInterface
    {
        explicit TextValue (std::function<juce::String()> f) : fn (std::move (f)) {}
        bool isReadOnly() const override { return true; }
        juce::String getCurrentValueAsString() const override { return fn(); }
        void setValueAsString (const juce::String&) override {}
        std::function<juce::String()> fn;
    };
    return std::make_unique<juce::AccessibilityHandler> (c, role, juce::AccessibilityActions {},
                                                         juce::AccessibilityHandler::Interfaces { std::make_unique<TextValue> (std::move (text)) });
}

/// Base for timer-driven read-only displays. The timer runs ONLY while the component is showing (and enabled via
/// setDisplayEnabled), so hidden scopes/meters cost nothing. With animations off (reduced motion) the refresh drops to
/// 2 Hz: values stay honest and current, but nothing moves smoothly.
class LiveDisplay : public juce::Component,
                    public juce::SettableTooltipClient,
                    protected juce::Timer
{
public:
    ~LiveDisplay() override;

    /// Options "show signal display / CPU gauge": false stops drawing work and hides the component.
    void setDisplayEnabled (bool);
    bool isDisplayEnabled() const noexcept { return displayEnabled; }

    bool isRefreshing() const noexcept { return isTimerRunning(); }
    int getRefreshRateHz() const noexcept;

    /// One refresh step immediately (used by tests / snapshots, which have no running message loop).
    void refreshNow (double dtSeconds = 1.0 / 30.0);

    void visibilityChanged() override;
    void parentHierarchyChanged() override;
    void lookAndFeelChanged() override;

protected:
    LiveDisplay() = default;
    virtual int baseRateHz() const noexcept = 0;
    virtual void refresh (double dtSeconds) = 0;
    void updateTimer();
    void timerCallback() final;

private:
    bool displayEnabled = true;
    double lastTickMs = 0.0;
};

//==================================================================================================================
/// Oscilloscope of recent post-limiter output (cdl-signal-visualization.md). Header row: tap buttons OUT / SUB / MID
/// and MONO. The trace is triggered on a rising zero crossing for stability. With no source or no data it says so.
/// The caption reminds the user it is a display aid and not a quality statement.
class ScopeView : public LiveDisplay
{
public:
    ScopeView();
    ~ScopeView() override;

    static constexpr int headerHeight = 28;

    void setSource (ScopeSource*);
    ScopeSource* getSource() const noexcept { return source; }

    void setTap (ScopeTap);
    ScopeTap getTap() const noexcept { return tap; }
    void setMono (bool);
    bool isMono() const noexcept { return mono; }
    bool hasData() const noexcept { return haveData; }
    /// Seconds of signal shown per frame.
    void setWindowSeconds (double s) { windowSeconds = juce::jlimit (0.005, 0.2, s); }

    void paint (juce::Graphics&) override;
    void resized() override;

protected:
    int baseRateHz() const noexcept override { return 30; }
    void refresh (double dt) override;
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

private:
    void updateButtons();
    juce::String caption() const;

    ScopeSource* source = nullptr;
    ScopeTap tap = ScopeTap::Output;
    bool mono = false;
    bool haveData = false;
    double windowSeconds = 0.04;
    std::vector<float> rawL, rawR, shownL, shownR;
    juce::TextButton outButton { "OUT" }, subButton { "SUB" }, midButton { "MID" }, monoButton { "MONO" };
    juce::TextButton cycleButton { "OUT" };   ///< narrow layouts: one button that steps through the available taps
};

//==================================================================================================================
/// Pure peak / RMS ballistics (unit-tested): bar = peak with fast release, held peak marker holds 1.5 s then falls at
/// 20 dB/s (baseline 2.3), RMS follows with the same release.
struct MeterBallistics
{
    static constexpr float floorDb = -90.0f;
    static constexpr double holdSeconds = 1.5;
    static constexpr float holdDecayDbPerSecond = 20.0f;
    static constexpr float barDecayDbPerSecond = 40.0f;

    float peakDb = floorDb;
    float rmsDb = floorDb;
    float holdDb = floorDb;
    double sinceHold = 0.0;

    static float toDb (float linear) noexcept
    {
        return linear > 1.0e-5f ? juce::jmax (floorDb, 20.0f * std::log10 (linear)) : floorDb;
    }

    void reset() noexcept { *this = MeterBallistics {}; }
    void update (double dtSeconds, float newPeakLinear, float newRmsLinear) noexcept;
};

/// Stereo peak/RMS meter (vertical or horizontal) with dB scale, held-peak numeric readout and a CLIP flag that appears
/// only when a FRESH reading reached 0 dBFS (never from stale data) and then latches for 2 s or until clicked.
class LevelMeter : public LiveDisplay
{
public:
    enum class Orientation { Vertical, Horizontal };

    explicit LevelMeter (Orientation = Orientation::Vertical);
    ~LevelMeter() override;

    void setSource (MeterSource*);
    MeterSource* getSource() const noexcept { return source; }

    float getHeldPeakDb() const noexcept { return juce::jmax (balL.holdDb, balR.holdDb); }
    float getBarDb (int channel) const noexcept { return channel == 0 ? balL.peakDb : balR.peakDb; }
    bool isClipping() const noexcept { return clipLatchMs > 0.0; }
    bool hasValidReading() const noexcept { return valid; }
    juce::String getReadoutText() const;
    void clearClip() { clipLatchMs = 0.0; repaint(); }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

protected:
    int baseRateHz() const noexcept override { return 30; }
    void refresh (double dt) override;
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

private:
    void drawBar (juce::Graphics&, const ThemeTokens&, juce::Rectangle<float>, const MeterBallistics&) const;

    Orientation orientation;
    MeterSource* source = nullptr;
    MeterBallistics balL, balR;
    bool valid = false;
    double clipLatchMs = 0.0;
};

//==================================================================================================================
/// "DSP CPU" gauge: Snaral callback time / block deadline for one audio core, smoothed over 1 s, with an overrun
/// indicator that fires only on a measured over-deadline callback. Not a host-wide figure (stated in the tooltip).
class CpuGauge : public LiveDisplay
{
public:
    CpuGauge();
    ~CpuGauge() override;

    static constexpr double smoothingSeconds = 1.0;
    static constexpr double overrunShowSeconds = 3.0;

    void setSource (CpuSource*);
    CpuSource* getSource() const noexcept { return source; }

    float getSmoothedLoad() const noexcept { return smoothed; }
    bool isOverrunShown() const noexcept { return overrunRemaining > 0.0; }
    bool hasValidReading() const noexcept { return valid; }
    juce::String getReadoutText() const;

    void paint (juce::Graphics&) override;

protected:
    int baseRateHz() const noexcept override { return 10; }
    void refresh (double dt) override;
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

private:
    CpuSource* source = nullptr;
    float smoothed = 0.0f;
    bool valid = false;
    double overrunRemaining = 0.0;
};
} // namespace snaral::ui
