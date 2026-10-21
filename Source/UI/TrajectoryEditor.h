#pragma once

#include "Theme.h"
#include "Dsp/Trajectory.h"

#include <array>

namespace snaral::ui
{
/// The eight editable trajectory parameters. Point 1 (x = 0) and point 5 (x = 1) are fixed in time.
enum class TrajParam { X2, X3, X4, Y1, Y2, Y3, Y4, Y5 };
inline constexpr int numTrajParams = 8;
const char* trajParamId (TrajParam);                  // "traj_x2" ... "traj_y5"

/// Timing information shown next to the canvas (traj_time_mode / traj_length_ms / traj_length_div).
struct TrajectoryTiming
{
    bool syncMode = false;
    double lengthMs = 600.0;
    juce::String divisionName = "1/2";
    double tempoBpm = 0.0;           ///< host tempo when known (0 = unknown); only used to show an approximate ms figure
    double divisionBeats = 2.0;
};

/// Pure geometry rules (unit-tested; the widget and the tests use the same functions).
struct TrajectoryGeometry
{
    /// Minimum horizontal gap between neighbouring points as a fraction of the trajectory length: dragging can never
    /// cross a neighbour or create a zero / negative segment duration.
    /// Equals the DSP sanitising gap (trajMinGap = 0.02, expansion-snarl-trajectory.md), so the drawn points can never
    /// differ from the points the engine plays.
    static constexpr double minGap = 0.02;
    static_assert (static_cast<float> (minGap) == trajMinGap, "UI and DSP trajectory gaps must match");

    /// `x` holds the five point times; index 0 and 4 are fixed (0 and 1). Returns the allowed x for `index`.
    static double clampX (const std::array<double, 5>& x, int index, double proposed) noexcept;
    static double clampY (double proposed) noexcept { return proposed < 0.0 ? 0.0 : (proposed > 1.0 ? 1.0 : proposed); }
    static double snap (double v, double step) noexcept;
    /// Dense, ordered, finite check of a full point set.
    static bool isValid (const std::array<double, 5>& x, const std::array<double, 5>& y) noexcept;
};

/// Curve preview callback so the DSP evaluator (Source/Dsp/Trajectory.h) can be reused for the drawn curve: returns
/// the normalised destination depth (0..1) at normalised time t01 for the given points and curve amount (traj_curve / 100).
using TrajectoryCurveEvaluator = std::function<float (const std::array<float, 5>& x, const std::array<float, 5>& y,
                                                      float curve01, float t01)>;

/// Default evaluator: the DSP curve itself (snaral::TrajectoryCurve, Source/Dsp/Trajectory.h), so the drawn curve is exactly
/// the engine's destination curve for the same points and curve amount (the points are sanitised the same way).
float defaultTrajectoryCurve (const std::array<float, 5>& x, const std::array<float, 5>& y, float curve01, float t01);

/// Abstraction of the parameter store behind the editor, so it can be unit tested without a processor.
class TrajectoryBinding
{
public:
    virtual ~TrajectoryBinding() = default;
    virtual double getValue (TrajParam) const = 0;
    virtual double getDefault (TrajParam) const = 0;
    virtual void beginGesture (TrajParam) = 0;
    virtual void setValue (TrajParam, double) = 0;      ///< only between begin/end (host automation gesture)
    virtual void endGesture (TrajParam) = 0;
    virtual TrajectoryTiming getTiming() const = 0;
    virtual float getCurveAmount01() const = 0;
    virtual bool isEnabled() const { return true; }
    /// The editor sets this; the binding calls it when any watched parameter changed (message thread).
    std::function<void()> onChanged;
};

/// APVTS-backed binding using juce::ParameterAttachment (so begin/set/end map to host change gestures).
class ApvtsTrajectoryBinding : public TrajectoryBinding
{
public:
    explicit ApvtsTrajectoryBinding (juce::AudioProcessorValueTreeState&);
    ~ApvtsTrajectoryBinding() override;

    double getValue (TrajParam) const override;
    double getDefault (TrajParam) const override;
    void beginGesture (TrajParam) override;
    void setValue (TrajParam, double) override;
    void endGesture (TrajParam) override;
    TrajectoryTiming getTiming() const override;
    float getCurveAmount01() const override;
    bool isEnabled() const override;

    /// Optional host tempo source for the approximate-ms hint in sync mode.
    std::function<double()> tempoProvider;

private:
    juce::AudioProcessorValueTreeState& state;
    std::array<std::unique_ptr<juce::ParameterAttachment>, numTrajParams> pointAttachments;
    std::vector<std::unique_ptr<juce::ParameterAttachment>> watchers;   // timing / curve / enabled (read-only use)
    std::array<float, numTrajParams> cached {};
};

//==================================================================================================================
/// The Snarl Trajectory widget: a canvas with five draggable points (body > peak > return), live curve preview and a
/// readout of the selected point and trajectory length (ms or musical division).
///
/// Mouse: drag a point (x and y; end points move vertically only); double-click a point to reset it; right-click for
///        Reset point / Reset trajectory.
/// Keyboard (canvas is focusable): Page Up / Page Down, [ and ] or Home / End choose the point; arrows nudge it
///        (1 %, Shift 5 %, Ctrl 0.1 %, or one grid step with snapping on); Delete resets it; Shift+F10 opens the menu.
/// Every drag is ONE host automation gesture; every key nudge is one complete gesture.
class TrajectoryEditor : public juce::Component,
                         public juce::SettableTooltipClient
{
public:
    explicit TrajectoryEditor (std::unique_ptr<TrajectoryBinding> binding);
    ~TrajectoryEditor() override;

    static constexpr int numPoints = 5;
    static constexpr int readoutHeight = 28;

    void setCurveEvaluator (TrajectoryCurveEvaluator);
    void setSnapToGrid (bool);
    bool isSnapToGrid() const noexcept { return snapEnabled; }
    void setSnapStep (double step) { snapStep = juce::jlimit (0.005, 0.25, step); }
    TrajectoryBinding& getBinding() noexcept { return *binding; }

    int getSelectedPoint() const noexcept { return selected; }
    void selectPoint (int index);

    /// Point positions as currently stored (x, y in 0..1).
    std::array<double, 5> getX() const;
    std::array<double, 5> getY() const;

    /// Moves a point as one complete gesture (what a key nudge does); returns false for a disallowed move.
    bool movePointTo (int index, double newX, double newY);
    void resetPoint (int index);
    void resetAll();
    juce::String getReadoutText() const;
    juce::String getTimeLabel (double x01) const;

    /// Geometry helpers (also used by the tests): point position in component coordinates and the plot rectangle.
    juce::Rectangle<float> getPlotArea() const;
    juce::Point<float> pointToScreen (int index) const;
    int hitTestPoint (juce::Point<float> p) const;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void focusGained (FocusChangeType) override;
    void focusLost (FocusChangeType) override;

protected:
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

private:
    TrajParam xParam (int point) const;                 // only valid for 1..3
    TrajParam yParam (int point) const;
    void beginPointGesture (int point);
    void endPointGesture (int point);
    void applyPoint (int point, double x, double y);
    void updateAccessibleText();
    void showMenu (juce::Point<int> screenPos);
    void nudge (double dx, double dy);

    std::unique_ptr<TrajectoryBinding> binding;
    TrajectoryCurveEvaluator evaluator;
    juce::ToggleButton snapButton { "SNAP" };
    int selected = 1;
    int dragging = -1;
    bool snapEnabled = false;
    double snapStep = 0.05;
    juce::Point<float> dragOffset;
};
} // namespace snaral::ui
