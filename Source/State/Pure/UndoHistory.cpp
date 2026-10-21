#include "UndoHistory.h"

#include <algorithm>

namespace snaral
{
namespace
{
const std::string emptyLabel;
}

UndoHistory::UndoHistory (const Config& config) : config_ (config)
{
    if (config_.capacity == 0)
        config_.capacity = 1;
    if (config_.coalesceWindowMs < 0)
        config_.coalesceWindowMs = 0;
}

bool UndoHistory::isTracked (P::Index param) noexcept
{
    return param >= 0 && param < P::count && parameterTable[static_cast<std::size_t> (param)].inPreset();
}

bool UndoHistory::canUndo() const noexcept
{
    if (! undoStack_.empty())
        return true;
    return std::any_of (gestures_.begin(), gestures_.end(), [] (const OpenGesture& g) { return g.changed; });
}

const std::string& UndoHistory::undoLabel() const noexcept
{
    return undoStack_.empty() ? emptyLabel : undoStack_.back().label;
}

const std::string& UndoHistory::redoLabel() const noexcept
{
    return redoStack_.empty() ? emptyLabel : redoStack_.back().label;
}

void UndoHistory::reset (const ParamSnapshot& baseline)
{
    undoStack_.clear();
    redoStack_.clear();
    gestures_.clear();
    tracked_ = sanitised (baseline);
    ++revision_;
}

void UndoHistory::sync (const ParamSnapshot& current)
{
    const auto clean = sanitised (current);
    for (int i = 0; i < parameterCount; ++i)
    {
        const auto param = static_cast<P::Index> (i);
        if (gestureOpen (param))
            continue;
        tracked_[param] = clean[param];
    }
}

bool UndoHistory::gestureOpen (P::Index param) const noexcept
{
    return std::any_of (gestures_.begin(), gestures_.end(), [param] (const OpenGesture& g) { return g.param == param; });
}

void UndoHistory::sealLast() noexcept
{
    if (! undoStack_.empty())
        undoStack_.back().sealed = true;
}

void UndoHistory::pushStep (Step&& step)
{
    undoStack_.push_back (std::move (step));
    while (undoStack_.size() > config_.capacity)
        undoStack_.pop_front();
    redoStack_.clear();
    ++revision_;
}

void UndoHistory::beginGesture (P::Index param, TimeMs now)
{
    if (! isTracked (param) || gestureOpen (param))
        return;
    sealLast();
    gestures_.push_back ({ param, tracked_[param], tracked_[param], false, now });
}

void UndoHistory::commitGesture (std::size_t index, TimeMs now)
{
    const OpenGesture g = gestures_[index];
    gestures_.erase (gestures_.begin() + static_cast<std::ptrdiff_t> (index));
    if (! g.changed || g.before == g.after)
        return;
    Step step;
    step.label = "Edit " + std::string (parameterTable[static_cast<std::size_t> (g.param)].name);
    step.changes.push_back ({ g.param, g.before, g.after });
    step.time = now;
    step.sealed = true;
    pushStep (std::move (step));
}

void UndoHistory::commitAllGestures (TimeMs now)
{
    while (! gestures_.empty())
        commitGesture (gestures_.size() - 1, now);
}

void UndoHistory::endGesture (P::Index param, TimeMs now)
{
    for (std::size_t i = 0; i < gestures_.size(); ++i)
    {
        if (gestures_[i].param == param)
        {
            commitGesture (i, now);
            return;
        }
    }
}

void UndoHistory::edit (P::Index param, float newValue, TimeMs now)
{
    if (param < 0 || param >= P::count || ! std::isfinite (newValue))
        return;
    const auto& def = parameterTable[static_cast<std::size_t> (param)];
    const float v = def.clampToDomain (newValue);

    if (! isTracked (param))
    {
        tracked_[param] = v;
        return;
    }
    if (v == tracked_[param])
        return;

    for (auto& g : gestures_)
    {
        if (g.param == param)
        {
            g.after = v;
            g.changed = g.changed || v != g.before;
            tracked_[param] = v;
            if (! redoStack_.empty())
            {
                redoStack_.clear();
                ++revision_;
            }
            return;
        }
    }

    if (! undoStack_.empty())
    {
        auto& last = undoStack_.back();
        if (! last.sealed && last.changes.size() == 1 && last.changes[0].param == param
            && now - last.time <= config_.coalesceWindowMs && now >= last.time)
        {
            last.changes[0].after = v;
            last.time = now;
            tracked_[param] = v;
            if (last.changes[0].after == last.changes[0].before)
                undoStack_.pop_back(); // edited back to the starting value: nothing to undo
            if (! redoStack_.empty())
                redoStack_.clear();
            ++revision_;
            return;
        }
    }

    Step step;
    step.label = "Edit " + std::string (def.name);
    step.changes.push_back ({ param, tracked_[param], v });
    step.time = now;
    step.sealed = false;
    sealLast();
    tracked_[param] = v;
    pushStep (std::move (step));
}

void UndoHistory::recordTransaction (const std::string& label, const ParamSnapshot& newState, TimeMs now)
{
    commitAllGestures (now);
    sealLast();
    const auto clean = sanitised (newState);

    Step step;
    step.label = label;
    step.time = now;
    step.sealed = true;
    for (int i = 0; i < parameterCount; ++i)
    {
        const auto param = static_cast<P::Index> (i);
        if (! isTracked (param))
        {
            tracked_[param] = clean[param];
            continue;
        }
        if (clean[param] != tracked_[param])
            step.changes.push_back ({ param, tracked_[param], clean[param] });
        tracked_[param] = clean[param];
    }
    if (! step.changes.empty())
        pushStep (std::move (step));
}

bool UndoHistory::undo (ParamSnapshot& state)
{
    commitAllGestures (0);
    if (undoStack_.empty())
        return false;
    Step step = std::move (undoStack_.back());
    undoStack_.pop_back();
    for (auto it = step.changes.rbegin(); it != step.changes.rend(); ++it)
    {
        state[it->param] = it->before;
        tracked_[it->param] = it->before;
    }
    step.sealed = true;
    redoStack_.push_back (std::move (step));
    ++revision_;
    return true;
}

bool UndoHistory::redo (ParamSnapshot& state)
{
    if (redoStack_.empty())
        return false;
    Step step = std::move (redoStack_.back());
    redoStack_.pop_back();
    for (const auto& c : step.changes)
    {
        state[c.param] = c.after;
        tracked_[c.param] = c.after;
    }
    step.sealed = true;
    undoStack_.push_back (std::move (step));
    ++revision_;
    return true;
}
} // namespace snaral
