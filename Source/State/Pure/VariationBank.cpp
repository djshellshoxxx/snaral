#include "VariationBank.h"

#include "Json.h"

namespace snaral
{
VariationBank::VariationBank()
{
    reset();
}

void VariationBank::reset()
{
    for (int i = 0; i < variationSlotCount; ++i)
    {
        auto& s = slots_[static_cast<std::size_t> (i)];
        s.name = defaultName (i);
        s.empty = true;
        s.snapshot = ParamSnapshot {};
    }
    active_ = 0;
}

std::string VariationBank::defaultName (int index)
{
    return std::string (1, static_cast<char> ('A' + (index < 0 ? 0 : index % variationSlotCount)));
}

const VariationSlot& VariationBank::slot (int index) const
{
    return slots_[static_cast<std::size_t> (index < 0 || index >= variationSlotCount ? 0 : index)];
}

bool VariationBank::isEmpty (int index) const
{
    return index < 0 || index >= variationSlotCount || slots_[static_cast<std::size_t> (index)].empty;
}

bool VariationBank::store (int index, const ParamSnapshot& current)
{
    if (index < 0 || index >= variationSlotCount)
        return false;
    auto& s = slots_[static_cast<std::size_t> (index)];
    s.snapshot = sanitised (current);
    s.empty = false;
    return true;
}

bool VariationBank::recall (int index, const ParamSnapshot& current, ParamSnapshot& out)
{
    if (isEmpty (index))
        return false;
    out = withSoundParams (current, slots_[static_cast<std::size_t> (index)].snapshot);
    active_ = index;
    return true;
}

bool VariationBank::clear (int index)
{
    if (index < 0 || index >= variationSlotCount)
        return false;
    auto& s = slots_[static_cast<std::size_t> (index)];
    s.empty = true;
    s.snapshot = ParamSnapshot {};
    return true;
}

bool VariationBank::normaliseName (const std::string& in, std::string& out)
{
    std::size_t b = 0, e = in.size();
    while (b < e && (in[b] == ' ' || in[b] == '\t'))
        ++b;
    while (e > b && (in[e - 1] == ' ' || in[e - 1] == '\t'))
        --e;
    const std::string trimmed = in.substr (b, e - b);
    if (! json::isValidUtf8 (trimmed))
        return false;
    for (const char ch : trimmed)
    {
        const auto c = static_cast<unsigned char> (ch);
        if (c < 0x20 || c == 0x7F)
            return false;
    }
    if (json::codePointCount (trimmed) > maxVariationNameChars)
        return false;
    out = trimmed;
    return true;
}

bool VariationBank::rename (int index, const std::string& name)
{
    if (index < 0 || index >= variationSlotCount)
        return false;
    std::string clean;
    if (! normaliseName (name, clean))
        return false;
    slots_[static_cast<std::size_t> (index)].name = clean.empty() ? defaultName (index) : clean;
    return true;
}

bool VariationBank::setActiveIndex (int index) noexcept
{
    if (index < 0 || index >= variationSlotCount)
        return false;
    active_ = index;
    return true;
}

bool VariationBank::operator== (const VariationBank& other) const
{
    if (active_ != other.active_)
        return false;
    for (std::size_t i = 0; i < slots_.size(); ++i)
    {
        const auto& a = slots_[i];
        const auto& b = other.slots_[i];
        if (a.name != b.name || a.empty != b.empty)
            return false;
        if (! a.empty && ! soundParamsEqual (a.snapshot, b.snapshot, 0.0f))
            return false;
    }
    return true;
}

// ----------------------------------------------------------------------------------------------------------

void AbCompare::reset()
{
    engaged_ = false;
    haveBoth_ = false;
    active_ = Side::A;
    snaps_ = {};
    matchTrim_ = true;
}

void AbCompare::engage (const ParamSnapshot& current)
{
    if (engaged_)
        return;
    const auto clean = sanitised (current);
    if (! haveBoth_)
    {
        snaps_[0] = clean;
        snaps_[1] = clean;
        active_ = Side::A;
        haveBoth_ = true;
    }
    else
    {
        // Re-engaging: the live state becomes the active side; the other side is kept as it was.
        snaps_[static_cast<std::size_t> (active_)] = clean;
    }
    engaged_ = true;
}

void AbCompare::captureLive (const ParamSnapshot& current)
{
    if (engaged_)
        snaps_[static_cast<std::size_t> (active_)] = sanitised (current);
}

bool AbCompare::toggle (const ParamSnapshot& current, ParamSnapshot& out)
{
    if (! engaged_)
        return false;
    const auto activeIdx = static_cast<std::size_t> (active_);
    const auto otherIdx = 1 - activeIdx;
    snaps_[activeIdx] = sanitised (current);

    ParamSnapshot next = withSoundParams (current, snaps_[otherIdx]);
    if (matchTrim_)
        next[P::output_trim_db] = current[P::output_trim_db];
    out = next;
    active_ = active_ == Side::A ? Side::B : Side::A;
    return true;
}

void AbCompare::copyToOther (const ParamSnapshot& current)
{
    if (! engaged_)
        return;
    const auto activeIdx = static_cast<std::size_t> (active_);
    snaps_[activeIdx] = sanitised (current);
    snaps_[1 - activeIdx] = snaps_[activeIdx];
}

bool AbCompare::copyFromOther (const ParamSnapshot& current, ParamSnapshot& out)
{
    if (! engaged_)
        return false;
    const auto activeIdx = static_cast<std::size_t> (active_);
    ParamSnapshot next = withSoundParams (current, snaps_[1 - activeIdx]);
    if (matchTrim_)
        next[P::output_trim_db] = current[P::output_trim_db];
    snaps_[activeIdx] = sanitised (next);
    out = next;
    return true;
}

void AbCompare::restore (bool engaged, Side active, const ParamSnapshot& a, const ParamSnapshot& b, bool matchTrim)
{
    engaged_ = engaged;
    haveBoth_ = true;
    active_ = active;
    snaps_[0] = sanitised (a);
    snaps_[1] = sanitised (b);
    matchTrim_ = matchTrim;
}

bool AbCompare::operator== (const AbCompare& other) const
{
    if (engaged_ != other.engaged_ || haveBoth_ != other.haveBoth_ || active_ != other.active_
        || matchTrim_ != other.matchTrim_)
        return false;
    return ! haveBoth_ || (soundParamsEqual (snaps_[0], other.snaps_[0], 0.0f)
                           && soundParamsEqual (snaps_[1], other.snaps_[1], 0.0f));
}
} // namespace snaral
