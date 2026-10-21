#include "Core/MidiLearn.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <thread>

#include "State/Pure/NumberText.h"

namespace snaral::midi
{
bool isReservedCc (int cc) noexcept
{
    if (cc < 0 || cc > 127)
        return true;
    return cc == 0 || cc == 32 || cc == 6 || cc == 38 || cc == 64 || (cc >= 98 && cc <= 101) || cc >= 120;
}

bool isLearnable (int paramIndex) noexcept
{
    if (paramIndex < 0 || paramIndex >= parameterCount)
        return false;
    const auto& d = parameterTable[static_cast<std::size_t> (paramIndex)];
    if (d.type == ParamType::Choice || ! d.automatable() || (d.flags & pfInteger) != 0)
        return false;
    switch (paramIndex)
    {
        case P::master_tune_hz: case P::limiter_enabled: case P::limiter_ceiling_db:
        case P::bypass: case P::sub_mute: case P::mid_mute:
            return false;
        default:
            return true;
    }
}

std::vector<int> learnableParameters()
{
    std::vector<int> out;
    for (int i = 0; i < parameterCount; ++i)
        if (isLearnable (i))
            out.push_back (i);
    return out;
}

std::string validateMapping (const MidiMapping& m)
{
    if (! isLearnable (m.target)) return "target parameter is not learnable";
    if (m.cc < 0 || m.cc > 127) return "cc out of range 0..127";
    if (isReservedCc (m.cc)) return "cc " + std::to_string (m.cc) + " is a reserved MIDI message";
    if (m.channel < 0 || m.channel > 16) return "channel out of range 0..16";
    if (! std::isfinite (m.min) || ! std::isfinite (m.max) || m.min < 0.0f || m.max > 1.0f)
        return "min/max must be finite and inside [0,1]";
    if (m.min > m.max) return "min is greater than max";
    return {};
}

std::string validateMappings (const std::vector<MidiMapping>& list)
{
    if (list.size() > static_cast<std::size_t> (maxMappings))
        return "more than 16 mappings";
    for (std::size_t i = 0; i < list.size(); ++i)
    {
        if (auto e = validateMapping (list[i]); ! e.empty())
            return "entry " + std::to_string (i + 1) + ": " + e;
        for (std::size_t j = 0; j < i; ++j)
        {
            if (list[i].target == list[j].target)
                return "entry " + std::to_string (i + 1) + ": duplicate target";
            if (list[i].cc == list[j].cc && channelsOverlap (list[i].channel, list[j].channel))
                return "entry " + std::to_string (i + 1) + ": cc/channel already mapped";
        }
    }
    return {};
}

float convertCc (const MidiMapping& m, int value) noexcept
{
    const int v = value < 0 ? 0 : (value > 127 ? 127 : value);
    // Integer arithmetic at the ends keeps v=0 -> min and v=127 -> max exact.
    if (v == 0)   return m.invert ? m.max : m.min;
    if (v == 127) return m.invert ? m.min : m.max;
    float t = static_cast<float> (v) / 127.0f;
    if (m.invert)
        t = 1.0f - t;
    const float n = m.min + (m.max - m.min) * t;
    return n < m.min ? m.min : (n > m.max ? m.max : n);
}

std::string describeMapping (const MidiMapping& m)
{
    const auto name = (m.target >= 0 && m.target < parameterCount)
                          ? std::string (parameterTable[static_cast<std::size_t> (m.target)].name) : std::string ("?");
    return "CC " + std::to_string (m.cc) + (m.channel == 0 ? " omni" : " ch " + std::to_string (m.channel)) + " -> " + name;
}

// ------------------------------------------------------------------ text

std::string toText (const MidiMapData& data)
{
    std::string s = "snaral-midimap 1\n";
    for (const auto& m : data.mappings)
    {
        s += "map target=";
        s += (m.target >= 0 && m.target < parameterCount) ? std::string (parameterTable[static_cast<std::size_t> (m.target)].id) : "?";
        s += " cc=" + std::to_string (m.cc) + " ch=" + std::to_string (m.channel);
        s += " min=" + text::formatFloat (m.min) + " max=" + text::formatFloat (m.max);
        s += std::string (" invert=") + (m.invert ? "1" : "0") + "\n";
    }
    return s;
}

namespace
{
std::vector<std::string_view> splitWs (std::string_view line)
{
    std::vector<std::string_view> out;
    std::size_t i = 0;
    while (i < line.size())
    {
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t' || line[i] == '\r')) ++i;
        const std::size_t b = i;
        while (i < line.size() && line[i] != ' ' && line[i] != '\t' && line[i] != '\r') ++i;
        if (i > b) out.push_back (line.substr (b, i - b));
    }
    return out;
}
} // namespace

ParseResult fromText (std::string_view textIn)
{
    ParseResult r;
    auto fail = [&r] (int line, const std::string& why) { r.ok = false; r.data = {}; r.error = "line " + std::to_string (line) + ": " + why; return r; };

    if (textIn.size() > 64 * 1024)
        return fail (0, "text too large");

    int lineNo = 0;
    bool headerSeen = false;
    std::size_t pos = 0;
    while (pos <= textIn.size())
    {
        const auto nl = textIn.find ('\n', pos);
        const auto line = textIn.substr (pos, nl == std::string_view::npos ? std::string_view::npos : nl - pos);
        pos = nl == std::string_view::npos ? textIn.size() + 1 : nl + 1;
        ++lineNo;
        const auto tok = splitWs (line);
        if (tok.empty() || tok[0].front() == '#')
            continue;
        if (! headerSeen)
        {
            if (tok.size() != 2 || tok[0] != "snaral-midimap")
                return fail (lineNo, "missing 'snaral-midimap' header");
            if (tok[1] != "1")
                return fail (lineNo, "unsupported version");
            headerSeen = true;
            continue;
        }
        if (tok[0] != "map")
            return fail (lineNo, "unknown record '" + std::string (tok[0]) + "'");

        MidiMapping m;
        bool hasTarget = false, hasCc = false;
        for (std::size_t i = 1; i < tok.size(); ++i)
        {
            const auto eq = tok[i].find ('=');
            if (eq == std::string_view::npos) return fail (lineNo, "expected key=value");
            const auto key = tok[i].substr (0, eq);
            const auto val = tok[i].substr (eq + 1);
            long long iv = 0;
            if (key == "target")
            {
                m.target = findParameterIndex (val);
                if (m.target < 0) return fail (lineNo, "unknown target '" + std::string (val) + "'");
                hasTarget = true;
            }
            else if (key == "cc")      { if (! text::parseInt (val, iv)) return fail (lineNo, "bad cc"); m.cc = static_cast<int> (iv); hasCc = true; }
            else if (key == "ch")      { if (! text::parseInt (val, iv)) return fail (lineNo, "bad ch"); m.channel = static_cast<int> (iv); }
            else if (key == "min")     { if (! text::parseFloat (val, m.min)) return fail (lineNo, "bad min"); }
            else if (key == "max")     { if (! text::parseFloat (val, m.max)) return fail (lineNo, "bad max"); }
            else if (key == "invert")  { if (val != "0" && val != "1") return fail (lineNo, "bad invert"); m.invert = val == "1"; }
            else return fail (lineNo, "unknown key '" + std::string (key) + "'");
            if (std::abs (iv) > 1000000) return fail (lineNo, "number out of range");
        }
        if (! hasTarget || ! hasCc)
            return fail (lineNo, "target and cc are required");
        if (auto e = validateMapping (m); ! e.empty())
            return fail (lineNo, e);
        r.data.mappings.push_back (m);
        if (auto e = validateMappings (r.data.mappings); ! e.empty())
            return fail (lineNo, e);
    }
    if (! headerSeen)
        return fail (1, "empty or missing header");
    r.ok = true;
    return r;
}

// ------------------------------------------------------------------ lookup table

std::unique_ptr<MidiLearnTable> MidiLearnTable::build (const std::vector<MidiMapping>& list)
{
    auto t = std::make_unique<MidiLearnTable>();
    t->slotOf_.fill (-1);
    for (const auto& m : list)
    {
        if (t->count_ >= maxMappings || ! validateMapping (m).empty())
            continue;
        const int lo = m.channel == 0 ? 1 : m.channel;
        const int hi = m.channel == 0 ? 16 : m.channel;
        bool clash = false;
        for (int c = lo; c <= hi; ++c)
            clash |= t->slotOf_[static_cast<std::size_t> (m.cc * channelSlots + c)] >= 0;
        if (clash)
            continue; // first mapping wins; validated lists never clash
        const int slot = t->count_++;
        t->mappings_[static_cast<std::size_t> (slot)] = m;
        for (int c = lo; c <= hi; ++c)
            t->slotOf_[static_cast<std::size_t> (m.cc * channelSlots + c)] = static_cast<std::int8_t> (slot);
    }
    return t;
}

TablePublisher::TablePublisher()
{
    publish ({});
}

void TablePublisher::publish (const std::vector<MidiMapping>& list)
{
    auto fresh = MidiLearnTable::build (list);
    const MidiLearnTable* raw = fresh.get();
    auto& slot = ring_[static_cast<std::size_t> (next_)];
    // Readers that entered acquire() before this point may still hold the table in `slot` (published `retired`
    // publishes ago). Wait for them to leave before freeing it. New readers see only tables that stay alive.
    if (slot != nullptr)
    {
        for (int spins = 0; readers_.load (std::memory_order_seq_cst) != 0 && spins < 200000; ++spins)
            std::this_thread::yield();
    }
    slot = std::move (fresh); // frees the table published `retired` publishes ago
    next_ = (next_ + 1) % static_cast<int> (ring_.size());
    current_.store (raw, std::memory_order_seq_cst);
}

// ------------------------------------------------------------------ runtime

void MidiLearnRuntime::notifyExternalChange (int slot, float normalised) noexcept
{
    if (slot < 0 || slot >= maxMappings)
        return;
    auto& s = slots_[static_cast<std::size_t> (slot)];
    s.current = normalised;
    s.currentKnown = true;
    s.engaged = false;
    s.lastCc = -1.0f;
}

void MidiLearnRuntime::resetAll() noexcept
{
    slots_ = {};
}

CcApply MidiLearnRuntime::apply (const MidiLearnTable& table, int cc, int channel1to16, int value) noexcept
{
    CcApply out;
    const int slot = table.find (cc, channel1to16);
    if (slot < 0)
        return out;
    const auto& m = table.mapping (slot);
    auto& s = slots_[static_cast<std::size_t> (slot)];
    const auto& def = parameterTable[static_cast<std::size_t> (m.target)];

    const float n = convertCc (m, value);
    out.mapped = true;
    out.target = m.target;
    out.normalised = n;
    out.apply = true;

    if (mode_ == Mode::Pickup && def.type != ParamType::Bool && s.currentKnown && ! s.engaged)
    {
        const float tol = (m.max - m.min) / 127.0f * 0.75f + 1e-6f;
        const bool near = std::abs (n - s.current) <= tol;
        const bool crossed = s.lastCc >= 0.0f && (s.lastCc - s.current) * (n - s.current) <= 0.0f;
        if (near || crossed)
            s.engaged = true;
        else
            out.apply = false;
    }
    s.lastCc = n;
    if (out.apply)
    {
        s.current = n;
        s.currentKnown = true;
        s.engaged = true;
        // Bool targets: on when n >= 0.5 (spec); fromNormalized rounds half away from zero, i.e. the same rule.
        out.domain = def.fromNormalized (n);
        if (def.type == ParamType::Bool)
            out.domain = n >= 0.5f ? 1.0f : 0.0f;
    }
    return out;
}

void LearnCapture::offer (int cc, int channel0to15) noexcept
{
    if (! armed_.load (std::memory_order_acquire))
        return;
    if (isReservedCc (cc))
    {
        reserved_.store (true, std::memory_order_release);
        return;
    }
    const std::uint32_t packed = static_cast<std::uint32_t> (cc & 127) | (static_cast<std::uint32_t> (channel0to15 & 15) << 7) | (1u << 12);
    std::uint32_t expected = 0;
    event_.compare_exchange_strong (expected, packed, std::memory_order_acq_rel); // first one wins
}

bool LearnCapture::poll (Event& out) noexcept
{
    const auto v = event_.exchange (0, std::memory_order_acq_rel);
    if ((v & (1u << 12)) == 0)
        return false;
    out.cc = static_cast<int> (v & 127);
    out.channel0to15 = static_cast<int> ((v >> 7) & 15);
    armed_.store (false, std::memory_order_release);
    return true;
}

// ------------------------------------------------------------------ model

namespace
{
std::string paramName (int idx) { return std::string (parameterTable[static_cast<std::size_t> (idx)].name); }
}

MidiLearnModel::ArmResult MidiLearnModel::arm (int paramIndex)
{
    if (! isLearnable (paramIndex))
        return ArmResult::NotLearnable;
    pending_ = false;
    pendingConflict_ = {};
    armed_ = true;
    armedTarget_ = paramIndex;
    capture_.arm();
    return ArmResult::Armed;
}

void MidiLearnModel::cancel()
{
    armed_ = false;
    pending_ = false;
    armedTarget_ = -1;
    pendingConflict_ = {};
    capture_.disarm();
}

bool MidiLearnModel::pollCapture (CaptureResult& out)
{
    LearnCapture::Event e;
    if (! capture_.poll (e))
    {
        if (capture_.pollReservedSeen() && armed_)
        {
            out = {};
            out.kind = CaptureKind::Reserved;
            out.status = "Reserved MIDI message";
            return true;
        }
        return false;
    }
    out = onCcCaptured (e.cc, e.channel0to15);
    return true;
}

int MidiLearnModel::indexOfTarget (int paramIndex) const noexcept
{
    for (std::size_t i = 0; i < mappings_.size(); ++i)
        if (mappings_[i].target == paramIndex)
            return static_cast<int> (i);
    return -1;
}

MidiLearnModel::CaptureResult MidiLearnModel::onCcCaptured (int cc, int channel0to15)
{
    CaptureResult r;
    if (! armed_ || pending_)
        return r;
    if (isReservedCc (cc))
    {
        r.kind = CaptureKind::Reserved;
        r.status = "Reserved MIDI message";
        capture_.arm(); // stay armed
        return r;
    }
    const int ch1 = std::clamp (channel0to15, 0, 15) + 1;
    const int target = armedTarget_;
    const bool targetMapped = indexOfTarget (target) >= 0;

    if (! targetMapped && mappings_.size() >= static_cast<std::size_t> (maxMappings))
    {
        r.kind = CaptureKind::LimitReached;
        r.status = "Limit of 16 MIDI mappings reached. Remove one first.";
        cancel();
        return r;
    }

    Conflict c;
    c.targetAlreadyMapped = targetMapped;
    for (const auto& m : mappings_)
        if (m.cc == cc && m.target != target)
            c.ccHolders.push_back (m);

    if (c.ccHolders.empty() && ! targetMapped)
    {
        MidiMapping m;
        m.target = target; m.cc = cc; m.channel = 0;
        mappings_.push_back (m);
        ++revision_;
        r.kind = CaptureKind::Assigned;
        r.assigned = m;
        r.status = "CC " + std::to_string (cc) + " ch " + std::to_string (ch1) + " mapped to " + paramName (target);
        cancel();
        return r;
    }

    // KeepBoth: possible only when the new mapping and every holder can sit on distinct explicit channels.
    c.keepBothAvailable = ! c.ccHolders.empty();
    int omniHolders = 0;
    for (const auto& h : c.ccHolders)
    {
        if (h.channel == 0) ++omniHolders;
        else if (h.channel == ch1) c.keepBothAvailable = false;
    }
    if (omniHolders > 1) c.keepBothAvailable = false;
    if (c.ccHolders.empty())
        c.keepBothUnavailableReason = "No other control uses this CC.";
    else if (! c.keepBothAvailable)
        c.keepBothUnavailableReason = "The existing mapping already uses this exact channel, so both cannot get distinct channels.";
    else if (omniHolders == 1)
    {
        for (int cand = 1; cand <= 16; ++cand)
        {
            bool used = cand == ch1;
            for (const auto& h : c.ccHolders) used |= h.channel == cand;
            if (! used) { c.suggestedExistingChannel = cand; break; }
        }
    }
    pending_ = true;
    pendingCc_ = cc;
    pendingChannel_ = channel0to15;
    pendingConflict_ = c;
    capture_.disarm();
    r.kind = CaptureKind::NeedsChoice;
    r.conflict = c;
    r.status = c.ccHolders.empty() ? paramName (target) + " is already mapped. Replace or cancel?"
                                   : "CC " + std::to_string (cc) + " is already mapped to " + paramName (c.ccHolders.front().target) + ".";
    return r;
}

std::string MidiLearnModel::resolve (Choice choice, int existingChannel)
{
    if (! pending_)
        return {};
    const int target = armedTarget_;
    const int cc = pendingCc_;
    const int ch1 = pendingChannel_ + 1;
    const auto conflict = pendingConflict_;
    auto done = [this] (std::string s) { cancel(); return s; };

    if (choice == Choice::Cancel)
        return done ("Cancelled. Mappings unchanged.");

    auto removeTarget = [this] (int t) { std::erase_if (mappings_, [t] (const MidiMapping& m) { return m.target == t; }); };
    MidiMapping fresh;
    fresh.target = target; fresh.cc = cc; fresh.channel = 0;

    if (choice == Choice::KeepBoth)
    {
        if (! conflict.keepBothAvailable)
            return conflict.keepBothUnavailableReason; // stay pending so the UI can offer Replace/Cancel
        int pin = existingChannel != 0 ? existingChannel : conflict.suggestedExistingChannel;
        const bool hasOmni = std::any_of (conflict.ccHolders.begin(), conflict.ccHolders.end(), [] (const auto& h) { return h.channel == 0; });
        if (hasOmni && (pin < 1 || pin > 16 || pin == ch1))
            return "Choose a different channel (1 to 16) for the existing mapping.";
        removeTarget (target);
        for (auto& m : mappings_)
            if (m.cc == cc && m.channel == 0 && m.target != target)
                m.channel = pin;
        fresh.channel = ch1;
        mappings_.push_back (fresh);
    }
    else // Replace
    {
        removeTarget (target);
        std::erase_if (mappings_, [cc, target] (const MidiMapping& m) { return m.cc == cc && m.target != target; });
        mappings_.push_back (fresh);
    }
    ++revision_;
    return done ("CC " + std::to_string (cc) + " ch " + std::to_string (ch1) + " mapped to " + paramName (target));
}

bool MidiLearnModel::remove (int paramIndex)
{
    const int i = indexOfTarget (paramIndex);
    if (i < 0)
        return false;
    mappings_.erase (mappings_.begin() + i);
    ++revision_;
    return true;
}

void MidiLearnModel::clearAll()
{
    if (! mappings_.empty())
        ++revision_;
    mappings_.clear();
}

bool MidiLearnModel::update (const MidiMapping& edited, std::string* error)
{
    const int i = indexOfTarget (edited.target);
    auto setErr = [error] (std::string s) { if (error) *error = std::move (s); return false; };
    if (i < 0)
        return setErr ("no mapping for that target");
    auto copy = mappings_;
    copy[static_cast<std::size_t> (i)] = edited;
    if (auto e = validateMappings (copy); ! e.empty())
        return setErr (e);
    mappings_ = std::move (copy);
    ++revision_;
    return true;
}

bool MidiLearnModel::load (const MidiMapData& d, std::string* error)
{
    if (auto e = validateMappings (d.mappings); ! e.empty())
    {
        if (error) *error = e;
        return false;
    }
    cancel();
    mappings_ = d.mappings;
    ++revision_;
    return true;
}
} // namespace snaral::midi
