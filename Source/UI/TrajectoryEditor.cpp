#include "TrajectoryEditor.h"
#include "Displays.h"
#include "ParamFormat.h"

#include <cmath>

namespace snaral::ui
{
const char* trajParamId (TrajParam p)
{
    static const char* ids[] = { "traj_x2", "traj_x3", "traj_x4", "traj_y1", "traj_y2", "traj_y3", "traj_y4", "traj_y5" };
    return ids[static_cast<int> (p)];
}

// =================================================================================================================
// Geometry
// =================================================================================================================
double TrajectoryGeometry::clampX (const std::array<double, 5>& x, int index, double proposed) noexcept
{
    if (index <= 0) return 0.0;
    if (index >= 4) return 1.0;
    const double lo = x[static_cast<std::size_t> (index - 1)] + minGap;
    const double hi = x[static_cast<std::size_t> (index + 1)] - minGap;
    if (hi < lo)                       // neighbours already closer than 2 gaps: stay between them
        return 0.5 * (lo + hi);
    return proposed < lo ? lo : (proposed > hi ? hi : proposed);
}

double TrajectoryGeometry::snap (double v, double step) noexcept
{
    return step > 0.0 ? std::round (v / step) * step : v;
}

bool TrajectoryGeometry::isValid (const std::array<double, 5>& x, const std::array<double, 5>& y) noexcept
{
    for (std::size_t i = 0; i < 5; ++i)
    {
        if (! std::isfinite (x[i]) || ! std::isfinite (y[i]) || y[i] < 0.0 || y[i] > 1.0 || x[i] < 0.0 || x[i] > 1.0)
            return false;
        if (i > 0 && x[i] - x[i - 1] < minGap * 0.999)
            return false;
    }
    return x[0] == 0.0 && x[4] == 1.0;
}

float defaultTrajectoryCurve (const std::array<float, 5>& x, const std::array<float, 5>& y, float curve01, float t01)
{
    TrajectoryCurve curve;
    curve.set (x, y, curve01);
    return curve.eval (t01);
}

// =================================================================================================================
// ApvtsTrajectoryBinding
// =================================================================================================================
ApvtsTrajectoryBinding::ApvtsTrajectoryBinding (juce::AudioProcessorValueTreeState& s) : state (s)
{
    for (int i = 0; i < numTrajParams; ++i)
    {
        auto* p = state.getParameter (trajParamId (static_cast<TrajParam> (i)));
        jassert (p != nullptr);
        if (p == nullptr)
            continue;
        cached[static_cast<std::size_t> (i)] = p->convertFrom0to1 (p->getValue());
        pointAttachments[static_cast<std::size_t> (i)] = std::make_unique<juce::ParameterAttachment> (
            *p, [this, i] (float v)
            {
                cached[static_cast<std::size_t> (i)] = v;
                if (onChanged)
                    onChanged();
            });
        pointAttachments[static_cast<std::size_t> (i)]->sendInitialUpdate();
    }

    for (const char* id : { "traj_time_mode", "traj_length_ms", "traj_length_div", "traj_curve", "traj_enabled" })
        if (auto* p = state.getParameter (id))
            watchers.push_back (std::make_unique<juce::ParameterAttachment> (*p, [this] (float) { if (onChanged) onChanged(); }));
}

ApvtsTrajectoryBinding::~ApvtsTrajectoryBinding()
{
    watchers.clear();
    for (auto& a : pointAttachments)
        a.reset();
}

double ApvtsTrajectoryBinding::getValue (TrajParam p) const
{
    return static_cast<double> (cached[static_cast<std::size_t> (p)]);
}

double ApvtsTrajectoryBinding::getDefault (TrajParam p) const
{
    return static_cast<double> (findParamDef (trajParamId (p))->defaultValue);
}

void ApvtsTrajectoryBinding::beginGesture (TrajParam p)
{
    if (auto& a = pointAttachments[static_cast<std::size_t> (p)])
        a->beginGesture();
}

void ApvtsTrajectoryBinding::setValue (TrajParam p, double v)
{
    cached[static_cast<std::size_t> (p)] = static_cast<float> (v);
    if (auto& a = pointAttachments[static_cast<std::size_t> (p)])
        a->setValueAsPartOfGesture (static_cast<float> (v));
}

void ApvtsTrajectoryBinding::endGesture (TrajParam p)
{
    if (auto& a = pointAttachments[static_cast<std::size_t> (p)])
        a->endGesture();
}

TrajectoryTiming ApvtsTrajectoryBinding::getTiming() const
{
    auto raw = [this] (const char* id, float fallback)
    {
        if (auto* v = state.getRawParameterValue (id))
            return v->load();
        return fallback;
    };
    TrajectoryTiming t;
    t.syncMode = raw ("traj_time_mode", 0.0f) >= 0.5f;
    t.lengthMs = static_cast<double> (raw ("traj_length_ms", 600.0f));
    const int div = juce::jlimit (0, 5, static_cast<int> (raw ("traj_length_div", 3.0f) + 0.5f));
    t.divisionBeats = trajBeatsPerDivision[div];
    if (const auto* d = findParamDef ("traj_length_div"))
        t.divisionName = prettyChoiceName (d->choices[static_cast<std::size_t> (div)]);
    t.tempoBpm = tempoProvider ? tempoProvider() : 0.0;
    return t;
}

float ApvtsTrajectoryBinding::getCurveAmount01() const
{
    if (auto* v = state.getRawParameterValue ("traj_curve"))
        return v->load() * 0.01f;
    return 0.5f;
}

bool ApvtsTrajectoryBinding::isEnabled() const
{
    if (auto* v = state.getRawParameterValue ("traj_enabled"))
        return v->load() >= 0.5f;
    return true;
}

// =================================================================================================================
// TrajectoryEditor
// =================================================================================================================
TrajectoryEditor::TrajectoryEditor (std::unique_ptr<TrajectoryBinding> b)
    : binding (std::move (b)), evaluator (defaultTrajectoryCurve)
{
    binding->onChanged = [this] { updateAccessibleText(); repaint(); };

    snapButton.setClickingTogglesState (true);
    snapButton.setTitle ("Snap to grid");
    snapButton.setTooltip ("Snap dragged points to a 5 % grid (hold Alt while dragging to bypass)");
    snapButton.setWantsKeyboardFocus (true);
    snapButton.onClick = [this] { snapEnabled = snapButton.getToggleState(); };
    addAndMakeVisible (snapButton);

    setWantsKeyboardFocus (true);
    setMouseClickGrabsKeyboardFocus (true);
    setTitle ("Snarl trajectory editor");
    setTooltip ("Snarl Trajectory: five points shape the filter / source movement after each note. Drag a point, or use the "
                "arrow keys; Page Up / Page Down choose the point.");
    setSize (480, 260);
    updateAccessibleText();
}

TrajectoryEditor::~TrajectoryEditor()
{
    binding->onChanged = nullptr;
}

void TrajectoryEditor::setCurveEvaluator (TrajectoryCurveEvaluator e)
{
    evaluator = e ? std::move (e) : TrajectoryCurveEvaluator (defaultTrajectoryCurve);
    repaint();
}

void TrajectoryEditor::setSnapToGrid (bool s)
{
    snapEnabled = s;
    snapButton.setToggleState (s, juce::dontSendNotification);
}

void TrajectoryEditor::selectPoint (int i)
{
    selected = juce::jlimit (0, numPoints - 1, i);
    updateAccessibleText();
    repaint();
}

TrajParam TrajectoryEditor::xParam (int point) const { return static_cast<TrajParam> (point - 1); }          // 1..3 -> X2..X4
TrajParam TrajectoryEditor::yParam (int point) const { return static_cast<TrajParam> (static_cast<int> (TrajParam::Y1) + point); }

std::array<double, 5> TrajectoryEditor::getX() const
{
    return { 0.0, binding->getValue (TrajParam::X2), binding->getValue (TrajParam::X3), binding->getValue (TrajParam::X4), 1.0 };
}

std::array<double, 5> TrajectoryEditor::getY() const
{
    return { binding->getValue (TrajParam::Y1), binding->getValue (TrajParam::Y2), binding->getValue (TrajParam::Y3),
             binding->getValue (TrajParam::Y4), binding->getValue (TrajParam::Y5) };
}

juce::Rectangle<float> TrajectoryEditor::getPlotArea() const
{
    auto r = getLocalBounds().toFloat();
    r.removeFromTop (static_cast<float> (readoutHeight) + 6.0f);
    return r.withTrimmedLeft (44.0f).withTrimmedRight (16.0f).withTrimmedTop (10.0f).withTrimmedBottom (26.0f);
}

juce::Point<float> TrajectoryEditor::pointToScreen (int i) const
{
    const auto plot = getPlotArea();
    const auto x = getX(), y = getY();
    return { plot.getX() + static_cast<float> (x[static_cast<std::size_t> (i)]) * plot.getWidth(),
             plot.getBottom() - static_cast<float> (y[static_cast<std::size_t> (i)]) * plot.getHeight() };
}

int TrajectoryEditor::hitTestPoint (juce::Point<float> p) const
{
    int best = -1;
    float bestD = 16.0f;                                     // 32 px diameter hit target
    for (int i = 0; i < numPoints; ++i)
    {
        const float d = pointToScreen (i).getDistanceFrom (p);
        if (d <= bestD)
        {
            bestD = d;
            best = i;
        }
    }
    return best;
}

juce::String TrajectoryEditor::getTimeLabel (double x01) const
{
    const auto t = binding->getTiming();
    if (! t.syncMode)
        return juce::String (juce::roundToInt (x01 * t.lengthMs)) + " ms";
    if (x01 <= 0.0001) return "0";
    if (x01 >= 0.9999) return t.divisionName;
    return juce::String (juce::roundToInt (x01 * 100.0)) + "%";
}

juce::String TrajectoryEditor::getReadoutText() const
{
    const auto t = binding->getTiming();
    const auto x = getX(), y = getY();
    const auto i = static_cast<std::size_t> (selected);
    juce::String s;
    s << "Point " << (selected + 1) << " of 5: time " << juce::roundToInt (x[i] * 100.0) << "%";
    if (! t.syncMode)
        s << " (" << juce::roundToInt (x[i] * t.lengthMs) << " ms)";
    s << ", depth " << juce::roundToInt (y[i] * 100.0) << "%.  ";
    if (t.syncMode)
    {
        s << "Length " << t.divisionName;
        if (t.tempoBpm > 1.0)
            s << " (about " << juce::roundToInt (t.divisionBeats * 60000.0 / t.tempoBpm) << " ms at " << juce::roundToInt (t.tempoBpm) << " BPM)";
        else
            s << " (tempo synced)";
    }
    else
        s << "Length " << juce::roundToInt (t.lengthMs) << " ms";
    return s;
}

void TrajectoryEditor::updateAccessibleText()
{
    setDescription (getReadoutText() + " Arrow keys move the selected point. Page Up and Page Down choose another point. "
                    "Delete resets the point.");
    if (auto* h = getAccessibilityHandler())
        h->notifyAccessibilityEvent (juce::AccessibilityEvent::valueChanged);
}

std::unique_ptr<juce::AccessibilityHandler> TrajectoryEditor::createAccessibilityHandler()
{
    return makeReadoutAccessibilityHandler (*this, juce::AccessibilityRole::group, [this] { return getReadoutText(); });
}

void TrajectoryEditor::resized()
{
    snapButton.setBounds (getWidth() - 76, 0, 76, readoutHeight);
}

// ---- value changes ----------------------------------------------------------------------------------------------
void TrajectoryEditor::beginPointGesture (int p)
{
    if (p >= 1 && p <= 3)
        binding->beginGesture (xParam (p));
    binding->beginGesture (yParam (p));
}

void TrajectoryEditor::endPointGesture (int p)
{
    if (p >= 1 && p <= 3)
        binding->endGesture (xParam (p));
    binding->endGesture (yParam (p));
}

void TrajectoryEditor::applyPoint (int p, double x, double y)
{
    if (p >= 1 && p <= 3)
        binding->setValue (xParam (p), TrajectoryGeometry::clampX (getX(), p, x));
    binding->setValue (yParam (p), TrajectoryGeometry::clampY (y));
    updateAccessibleText();
    repaint();
}

bool TrajectoryEditor::movePointTo (int p, double x, double y)
{
    if (p < 0 || p >= numPoints || ! std::isfinite (x) || ! std::isfinite (y))
        return false;
    const auto cx = getX(), cy = getY();
    const double nx = (p >= 1 && p <= 3) ? TrajectoryGeometry::clampX (cx, p, x) : cx[static_cast<std::size_t> (p)];
    const double ny = TrajectoryGeometry::clampY (y);
    if (std::abs (nx - cx[static_cast<std::size_t> (p)]) < 1e-9 && std::abs (ny - cy[static_cast<std::size_t> (p)]) < 1e-9)
        return false;
    beginPointGesture (p);
    applyPoint (p, nx, ny);
    endPointGesture (p);
    return true;
}

void TrajectoryEditor::resetPoint (int p)
{
    if (p < 0 || p >= numPoints)
        return;
    const double dx = (p >= 1 && p <= 3) ? binding->getDefault (xParam (p)) : (p == 0 ? 0.0 : 1.0);
    movePointTo (p, dx, binding->getDefault (yParam (p)));
}

void TrajectoryEditor::resetAll()
{
    for (int p = 0; p < numPoints; ++p)
        resetPoint (p);
}

void TrajectoryEditor::nudge (double dx, double dy)
{
    const auto x = getX(), y = getY();
    const auto i = static_cast<std::size_t> (selected);
    double nx = x[i] + dx, ny = y[i] + dy;
    if (snapEnabled)
    {
        if (dx != 0.0) nx = TrajectoryGeometry::snap (nx, snapStep);
        if (dy != 0.0) ny = TrajectoryGeometry::snap (ny, snapStep);
    }
    movePointTo (selected, nx, ny);
}

// ---- mouse ------------------------------------------------------------------------------------------------------
void TrajectoryEditor::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    const int hit = hitTestPoint (e.position);
    if (hit >= 0)
        selectPoint (hit);

    if (e.mods.isPopupMenu())
    {
        showMenu (e.getScreenPosition());
        return;
    }
    if (hit < 0)
        return;

    dragging = hit;
    dragOffset = pointToScreen (hit) - e.position;
    beginPointGesture (hit);
}

void TrajectoryEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging < 0)
        return;
    const auto plot = getPlotArea();
    const auto p = e.position + dragOffset;
    double x = (p.x - plot.getX()) / plot.getWidth();
    double y = 1.0 - (p.y - plot.getY()) / plot.getHeight();
    if (snapEnabled && ! e.mods.isAltDown())
    {
        x = TrajectoryGeometry::snap (x, snapStep);
        y = TrajectoryGeometry::snap (y, snapStep);
    }
    applyPoint (dragging, x, y);
}

void TrajectoryEditor::mouseUp (const juce::MouseEvent&)
{
    if (dragging >= 0)
    {
        endPointGesture (dragging);
        dragging = -1;
    }
}

void TrajectoryEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int hit = hitTestPoint (e.position);
    if (hit >= 0)
        resetPoint (hit);
}

void TrajectoryEditor::showMenu (juce::Point<int> screenPos)
{
    juce::PopupMenu m;
    m.addItem (1, "Reset point " + juce::String (selected + 1));
    m.addItem (2, "Reset trajectory");
    m.addSeparator();
    m.addItem (3, "Snap to grid", true, snapEnabled);
    auto options = juce::PopupMenu::Options().withTargetComponent (this);
    if (screenPos != juce::Point<int>())
        options = options.withTargetScreenArea (juce::Rectangle<int> (screenPos, screenPos).expanded (1));
    juce::Component::SafePointer<TrajectoryEditor> safe (this);
    m.showMenuAsync (options, [safe] (int r)
    {
        if (safe == nullptr) return;
        if (r == 1) safe->resetPoint (safe->selected);
        if (r == 2) safe->resetAll();
        if (r == 3) safe->setSnapToGrid (! safe->snapEnabled);
    });
}

bool TrajectoryEditor::keyPressed (const juce::KeyPress& k)
{
    const auto code = k.getKeyCode();
    const auto mods = k.getModifiers();

    if (code == juce::KeyPress::pageUpKey || k.getTextCharacter() == ']' || code == juce::KeyPress::endKey)
    {
        selectPoint (code == juce::KeyPress::endKey ? numPoints - 1 : selected + 1);
        return true;
    }
    if (code == juce::KeyPress::pageDownKey || k.getTextCharacter() == '[' || code == juce::KeyPress::homeKey)
    {
        selectPoint (code == juce::KeyPress::homeKey ? 0 : selected - 1);
        return true;
    }
    if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey)
    {
        resetPoint (selected);
        return true;
    }
    if (code == juce::KeyPress::F10Key && mods.isShiftDown())
    {
        showMenu ({});
        return true;
    }

    double step = 0.01;
    if (mods.isShiftDown())                                          step = 0.05;
    if (mods.isCtrlDown() || mods.isAltDown() || mods.isCommandDown()) step = 0.001;
    if (code == juce::KeyPress::leftKey)  { nudge (-step, 0.0); return true; }
    if (code == juce::KeyPress::rightKey) { nudge (step, 0.0);  return true; }
    if (code == juce::KeyPress::upKey)    { nudge (0.0, step);  return true; }
    if (code == juce::KeyPress::downKey)  { nudge (0.0, -step); return true; }
    return false;
}

void TrajectoryEditor::focusGained (FocusChangeType) { repaint(); }
void TrajectoryEditor::focusLost (FocusChangeType)   { repaint(); }

// ---- painting ---------------------------------------------------------------------------------------------------
void TrajectoryEditor::paint (juce::Graphics& g)
{
    const auto& t = SnaralLookAndFeel::tokensFor (*this);
    const auto plot = getPlotArea();
    const auto canvas = getLocalBounds().toFloat().withTrimmedTop (static_cast<float> (readoutHeight) + 2.0f).reduced (0.5f);

    g.setColour (t.background);
    g.fillRoundedRectangle (canvas, 6.0f);
    g.setColour (t.border);
    g.drawRoundedRectangle (canvas, 6.0f, t.borderThickness);

    // readout (selected point + length)
    g.setFont (fonts::body().withHeight (12.0f));
    g.setColour (t.textPrimary);
    g.drawFittedText (getReadoutText(), getLocalBounds().withHeight (readoutHeight).withTrimmedRight (82), juce::Justification::centredLeft, 2, 0.85f);

    // grid + axis labels
    g.setFont (fonts::small());
    for (int i = 0; i <= 4; ++i)
    {
        const float f = static_cast<float> (i) / 4.0f;
        const float x = plot.getX() + f * plot.getWidth();
        g.setColour (i == 0 || i == 4 ? t.outline : t.border);
        g.drawVerticalLine (juce::roundToInt (x), plot.getY(), plot.getBottom());
        g.setColour (t.textMuted);
        const int w = 62;
        auto labelArea = juce::Rectangle<int> (juce::roundToInt (x) - w / 2, juce::roundToInt (plot.getBottom()) + 4, w, 16);
        labelArea = labelArea.withX (juce::jlimit (0, getWidth() - w, labelArea.getX()));
        g.drawText (getTimeLabel (f), labelArea, i == 0 ? juce::Justification::centredLeft : (i == 4 ? juce::Justification::centredRight : juce::Justification::centred), false);
    }
    for (int i = 0; i <= 2; ++i)
    {
        const float f = static_cast<float> (i) / 2.0f;
        const float y = plot.getBottom() - f * plot.getHeight();
        g.setColour (i == 0 || i == 2 ? t.outline : t.border);
        g.drawHorizontalLine (juce::roundToInt (y), plot.getX(), plot.getRight());
        g.setColour (t.textMuted);
        g.drawText (juce::String (juce::roundToInt (f * 100.0f)) + "%", juce::Rectangle<int> (0, juce::roundToInt (y) - 8, 40, 16),
                    juce::Justification::centredRight, false);
    }

    // curve
    const auto x = getX(), y = getY();
    std::array<float, 5> fx, fy;
    for (std::size_t i = 0; i < 5; ++i)
    {
        fx[i] = static_cast<float> (x[i]);
        fy[i] = static_cast<float> (y[i]);
    }
    const float curve = binding->getCurveAmount01();
    juce::Path line, fill;
    const int steps = juce::jmax (16, static_cast<int> (plot.getWidth() / 2.0f));
    for (int i = 0; i <= steps; ++i)
    {
        const float tt = static_cast<float> (i) / static_cast<float> (steps);
        float v = evaluator(fx, fy, curve, tt);
        if (! std::isfinite (v)) v = 0.0f;
        const auto p = juce::Point<float> (plot.getX() + tt * plot.getWidth(),
                                           plot.getBottom() - juce::jlimit (0.0f, 1.0f, v) * plot.getHeight());
        if (i == 0) { line.startNewSubPath (p); fill.startNewSubPath (plot.getX(), plot.getBottom()); fill.lineTo (p); }
        else        { line.lineTo (p); fill.lineTo (p); }
    }
    fill.lineTo (plot.getRight(), plot.getBottom());
    fill.closeSubPath();
    const bool on = binding->isEnabled();
    g.setColour (t.accent.withAlpha (on ? 0.14f : 0.06f));
    g.fillPath (fill);
    g.setColour (on ? t.accent : t.textMuted);
    if (on)
        g.strokePath (line, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    else
    {
        const float dashes[] = { 6.0f, 4.0f };
        juce::Path dashed;
        juce::PathStrokeType (1.6f).createDashedStroke (dashed, line, dashes, 2);
        g.fillPath (dashed);
        g.setFont (fonts::label());
        g.drawText ("TRAJECTORY OFF - NOT APPLIED", plot.toNearestInt().removeFromTop (16), juce::Justification::centredRight, false);
    }

    // points
    g.setFont (fonts::small());
    for (int i = 0; i < numPoints; ++i)
    {
        const auto c = pointToScreen (i);
        const bool sel = i == selected;
        const float r = sel ? 8.0f : 6.0f;
        const auto box = juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c);
        g.setColour (t.panelRaised);
        if (i == 0 || i == 4) g.fillRect (box); else g.fillEllipse (box);
        g.setColour (sel ? t.textPrimary : t.accent);
        if (i == 0 || i == 4) g.drawRect (box, sel ? 2.5f : 2.0f); else g.drawEllipse (box, sel ? 2.5f : 2.0f);
        if (sel)
        {
            g.setColour (t.accent);
            g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre (c));
        }
        g.setColour (t.textPrimary);
        const bool nearTop = c.y - plot.getY() < 22.0f;
        g.drawText (juce::String (i + 1), juce::Rectangle<float> (c.x - 8.0f, nearTop ? c.y + 10.0f : c.y - 26.0f, 16.0f, 16.0f).toNearestInt(),
                    juce::Justification::centred, false);
    }

    if (hasKeyboardFocus (false))
        SnaralLookAndFeel::drawFocusRing (g, t, canvas.reduced (1.0f), 6.0f);
}
} // namespace snaral::ui
