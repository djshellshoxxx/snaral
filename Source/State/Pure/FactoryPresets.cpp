#include "FactoryPresets.h"

#include <algorithm>

namespace snaral
{
std::string factoryEntryId (const PresetData& preset)
{
    return "factory:" + (preset.presetId.empty() ? preset.name : preset.presetId);
}

const FactoryPresetSet& factoryPresets()
{
    static const FactoryPresetSet set = []
    {
        FactoryPresetSet s;
        PresetParseOptions options;
        options.requireComplete = true;
        for (int i = 0; i < embedded::factoryPresetBlobCount; ++i)
        {
            const auto& blob = embedded::factoryPresetBlobs[i];
            const std::string_view text (reinterpret_cast<const char*> (blob.data), blob.size);
            auto r = parsePreset (text, options);
            if (! r.ok)
            {
                for (const auto& e : r.errors)
                    s.problems.push_back ({ blob.fileName, describe (e) });
                continue;
            }
            if (r.data.presetId.empty())
            {
                s.problems.push_back ({ blob.fileName, "factory presets must carry a preset_id" });
                continue;
            }
            s.presets.push_back ({ blob.fileName, std::move (r.data), std::move (r.warnings) });
        }
        std::sort (s.presets.begin(), s.presets.end(),
                   [] (const FactoryPreset& a, const FactoryPreset& b) { return a.fileName < b.fileName; });
        return s;
    }();
    return set;
}
} // namespace snaral
