#include "TestSupport.h"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <set>

#include "State/Pure/AtomicFile.h"
#include "State/Pure/FactoryPresets.h"
#include "State/Pure/PresetFacets.h"
#include "State/Pure/PresetFormat.h"

using namespace snaral;
using namespace snaral::testing;
namespace fs = std::filesystem;

namespace
{
struct Loaded
{
    std::string fileName;
    std::string bytes;
    PresetData data;
};

std::string lower (std::string s)
{
    for (auto& c : s)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char> (c - 'A' + 'a');
    return s;
}

bool hasTag (const PresetData& d, const std::string& tag)
{
    return std::find (d.tags.begin(), d.tags.end(), tag) != d.tags.end();
}

const Loaded* findByName (const std::vector<Loaded>& all, const std::string& name)
{
    for (const auto& l : all)
        if (l.data.name == name)
            return &l;
    return nullptr;
}
} // namespace

int main()
{
    const fs::path dir = fileio::pathFromUtf8 (sourceDir() + "/Resources/presets");
    std::vector<fs::path> files;
    for (const auto& e : fs::directory_iterator (dir))
        if (e.path().extension() == ".json")
            files.push_back (e.path());
    std::sort (files.begin(), files.end());
    CHECK_MSG (files.size() >= 24, "need at least 24 factory presets, found " + std::to_string (files.size()));

    PresetParseOptions strict;
    strict.requireComplete = true;
    std::vector<Loaded> all;
    std::set<std::string> ids, names;
    for (const auto& f : files)
    {
        Loaded l;
        l.fileName = fileio::pathToUtf8 (f.filename());
        const auto io = fileio::readFileLimited (f, maxPresetFileBytes, l.bytes);
        CHECK_MSG (io.ok, io.error);

        // 1) parses and validates completely, with no warnings of any kind
        const auto r = parsePreset (l.bytes, strict);
        CHECK_MSG (r.ok, l.fileName + ": " + r.firstError());
        CHECK_MSG (r.warnings.empty(), l.fileName + ": " + describe (r.warnings.front()));
        CHECK (r.defaultedParameters.empty() && r.ignoredParameters.empty());
        l.data = r.data;

        // 2) re-serialises byte-identically (generator and C++ writer agree)
        std::string again;
        CHECK (writePreset (r.data, again));
        CHECK_MSG (again == l.bytes, l.fileName + " is not in canonical writer format; run tools/gen_presets.py");

        // 3) basic hygiene
        CHECK (snapshotIsValid (r.data.params));
        CHECK_MSG (l.fileName.size() > 3 && std::isdigit (static_cast<unsigned char> (l.fileName[0])) != 0
                       && std::isdigit (static_cast<unsigned char> (l.fileName[1])) != 0 && l.fileName[2] == '_',
                   l.fileName);
        CHECK (r.data.presetId.rfind ("factory.", 0) == 0);
        CHECK (ids.insert (r.data.presetId).second);
        CHECK (names.insert (lower (r.data.name)).second);
        CHECK (! r.data.description.empty() && r.data.description.size() <= 220);
        CHECK (r.data.author == "Circuit Drift Labs");
        CHECK (r.data.description.find ("\xE2\x80\x94") == std::string::npos); // no em dash
        CHECK (r.data.tags.size() >= 5 && r.data.tags.size() <= 8);
        all.push_back (std::move (l));
    }

    // distinct sounds: no pair identical, every pair differs in several parameters
    for (std::size_t i = 0; i < all.size(); ++i)
        for (std::size_t j = i + 1; j < all.size(); ++j)
        {
            const auto diff = differingSoundParams (all[i].data.params, all[j].data.params).size();
            CHECK_MSG (diff >= 6, all[i].fileName + " vs " + all[j].fileName + " differ in only " + std::to_string (diff));
        }

    // required roles of docs/specs/factory-presets.md
    for (const char* name : { "Foundation", "Plain Weight", "Throat Sweep", "Saw Rasp", "Folded Jaw", "Machine Bell",
                              "Sync Stalker", "Free Rotor", "Wide Teeth", "Legato Pressure" })
        CHECK_MSG (findByName (all, name) != nullptr, std::string ("missing required preset ") + name);

    {
        const auto& p = findByName (all, "Foundation")->data.params;
        CHECK (p.choice (P::sub_waveform) == 0 && p[P::noise_level] == 0.0f && p[P::metal_level] == 0.0f);
        CHECK (p.percent (P::weight) * p.percent (P::sub_level) > 2.0f * p.percent (P::growl) * p.percent (P::mid_level));
        CHECK (p[P::motion] == 0.0f);
    }
    CHECK (findByName (all, "Plain Weight")->data.params[P::motion] == 0.0f);
    {
        const auto& p = findByName (all, "Throat Sweep")->data.params;
        CHECK (p.choice (P::mid_source_a) == 3 && p.flag (P::traj_enabled) && p[P::contour_depth] > 0.0f);
        CHECK (p.choice (P::traj_dest) == 2); // cutoff + source position
    }
    {
        const auto& p = findByName (all, "Saw Rasp")->data.params;
        CHECK (p.choice (P::mid_source_a) == 0 && p[P::drive_amount] > 20.0f && p[P::drive_amount] < 70.0f);
        CHECK (p[P::filter_resonance] <= 40.0f);
    }
    {
        const auto& p = findByName (all, "Folded Jaw")->data.params;
        CHECK (p.choice (P::mid_source_a) == 2 && p[P::bite] > 10.0f && p[P::bite] < 90.0f);
    }
    {
        const auto& p = findByName (all, "Machine Bell")->data.params;
        CHECK (p[P::metal_level] > 0.0f && p[P::metal_level] <= 40.0f && p[P::metal_decay_ms] <= 300.0f);
    }
    {
        const auto& p = findByName (all, "Sync Stalker")->data.params;
        CHECK (p.choice (P::lfo_mode) == 1 && p[P::lfo_depth] > 0.0f && p[P::motion] > 0.0f);
        CHECK (p[P::lfo_rate_hz] >= 0.5f && p[P::lfo_rate_hz] <= 8.0f); // meaningful free-rate fallback
    }
    {
        const auto& p = findByName (all, "Free Rotor")->data.params;
        CHECK (p.choice (P::lfo_mode) == 0 && p[P::lfo_depth] > 0.0f && p[P::motion] > 0.0f);
    }
    CHECK (findByName (all, "Wide Teeth")->data.params[P::space] > 0.0f);
    {
        const auto& p = findByName (all, "Legato Pressure")->data.params;
        CHECK (p.choice (P::voice_mode) == 0 && p.flag (P::legato) && p[P::glide_ms] > 0.0f);
    }

    // one patch where all five macros are mid-range
    {
        const auto& p = findByName (all, "Five Hands")->data.params;
        for (const auto id : { P::weight, P::growl, P::bite, P::motion, P::space })
            CHECK (p[id] >= 20.0f && p[id] <= 80.0f);
    }

    // sane musical settings across the set
    int noisy = 0, metallic = 0, subForward = 0, midForward = 0, balanced = 0;
    unsigned motionSeen = 0, textureSeen = 0;
    for (const auto& l : all)
    {
        const auto& p = l.data.params;
        CHECK (p.flag (P::limiter_enabled));
        CHECK (p[P::limiter_ceiling_db] <= -0.3f && p[P::limiter_ceiling_db] >= -6.0f);
        CHECK (p[P::output_trim_db] >= -6.0f && p[P::output_trim_db] <= 3.0f);
        CHECK (p[P::master_tune_hz] == 440.0f && p[P::transpose_semitones] == 0.0f);
        const float sub = p.percent (P::weight) * p.percent (P::sub_level);
        const float mid = p.percent (P::growl) * p.percent (P::mid_level);
        CHECK_MSG (sub >= 0.15f, l.fileName + ": sub level too low to be a bass");
        CHECK_MSG (sub <= 1.0f && mid <= 1.0f, l.fileName);
        CHECK_MSG (sub + mid >= 0.5f, l.fileName);
        CHECK (p[P::filter_resonance] <= 45.0f); // controlled peak
        if (p.flag (P::fracture_enabled) || p.flag (P::furnace_enabled) || p.choice (P::drive_type) == 1)
            CHECK (p[P::output_trim_db] <= 0.0f);
        noisy += p[P::noise_level] > 0.0f ? 1 : 0;
        metallic += p[P::metal_level] > 0.0f ? 1 : 0;

        // tags agree with the facets the browser filters on
        const auto f = deriveFacets (p);
        CHECK_MSG (hasTag (l.data, std::string (toString (f.layer))), l.fileName + ": layer tag disagrees with facets");
        subForward += f.layer == LayerBalance::SubForward;
        midForward += f.layer == LayerBalance::MidForward;
        balanced += f.layer == LayerBalance::Balanced;
        CHECK_MSG (hasTag (l.data, "static") == ((f.motion & motionStatic) != 0), l.fileName + ": static tag");
        if (hasTag (l.data, "contour")) CHECK_MSG ((f.motion & motionContour) != 0, l.fileName + ": contour tag");
        if (hasTag (l.data, "synced")) CHECK_MSG ((f.motion & motionSynced) != 0, l.fileName + ": synced tag");
        if (hasTag (l.data, "free")) CHECK_MSG ((f.motion & motionFree) != 0, l.fileName + ": free tag");
        CHECK_MSG (hasTag (l.data, "clean") == ((f.texture & textureClean) != 0), l.fileName + ": clean tag");
        CHECK_MSG (hasTag (l.data, "noise") == ((f.texture & textureNoise) != 0), l.fileName + ": noise tag");
        CHECK_MSG (hasTag (l.data, "metal") == ((f.texture & textureMetal) != 0), l.fileName + ": metal tag");
        motionSeen |= f.motion;
        textureSeen |= f.texture;

        // performance mode tag
        const bool poly = p.choice (P::voice_mode) == 1;
        const bool legato = p.flag (P::legato) && ! poly;
        CHECK (hasTag (l.data, "poly") == poly);
        CHECK (hasTag (l.data, "legato") == legato);
        CHECK (hasTag (l.data, "mono") == (! poly && ! legato));
    }
    CHECK (noisy >= 1 && noisy < static_cast<int> (all.size()) / 3); // noise and metal are not on in every patch
    CHECK (metallic >= 1 && metallic < static_cast<int> (all.size()) / 3);
    CHECK (subForward >= 3 && midForward >= 3 && balanced >= 3);
    CHECK (motionSeen == (motionStatic | motionContour | motionSynced | motionFree));
    CHECK (textureSeen == (textureClean | textureNoise | textureMetal));

    // feature coverage: every nonlinear/advanced block is demonstrated by at least one patch
    auto anyWith = [&] (auto pred)
    {
        return std::any_of (all.begin(), all.end(), [&] (const Loaded& l) { return pred (l.data.params); });
    };
    CHECK (anyWith ([] (const ParamSnapshot& p) { return p.flag (P::furnace_enabled); }));
    CHECK (anyWith ([] (const ParamSnapshot& p) { return p.flag (P::fracture_enabled); }));
    CHECK (anyWith ([] (const ParamSnapshot& p) { return p.flag (P::traj_enabled); }));
    CHECK (anyWith ([] (const ParamSnapshot& p) { return p.flag (P::anchor_enabled); }));
    CHECK (anyWith ([] (const ParamSnapshot& p) { return p.choice (P::oversampling_mode) == 2; }));
    CHECK (anyWith ([] (const ParamSnapshot& p) { return p.choice (P::voice_mode) == 1; }));
    CHECK (anyWith ([] (const ParamSnapshot& p) { return p.choice (P::filter_type) == 1; }));
    CHECK (anyWith ([] (const ParamSnapshot& p) { return p.choice (P::sub_waveform) == 1; }));
    CHECK (anyWith ([] (const ParamSnapshot& p) { return p.choice (P::lfo_sync_division) == 11; })); // 1/8t

    // embedded copy equals the files on disk (the CMake-generated blobs)
    CHECK (embedded::factoryPresetBlobCount == static_cast<int> (all.size()));
    for (int i = 0; i < embedded::factoryPresetBlobCount; ++i)
    {
        const auto& blob = embedded::factoryPresetBlobs[i];
        const std::string embeddedBytes (reinterpret_cast<const char*> (blob.data), blob.size);
        const auto it = std::find_if (all.begin(), all.end(), [&] (const Loaded& l) { return l.fileName == blob.fileName; });
        CHECK_MSG (it != all.end(), blob.fileName);
        CHECK_MSG (it->bytes == embeddedBytes, std::string (blob.fileName) + " embedded bytes differ from file; re-run CMake");
    }
    const auto& set = factoryPresets();
    CHECK_MSG (set.problems.empty(), set.problems.empty() ? "" : describe (set.problems.front()));
    CHECK (set.presets.size() == all.size());
    for (std::size_t i = 0; i < all.size(); ++i)
    {
        CHECK (set.presets[i].fileName == all[i].fileName);
        CHECK (set.presets[i].data.name == all[i].data.name);
        CHECK (soundParamsEqual (set.presets[i].data.params, all[i].data.params, 0.0f));
        CHECK (factoryEntryId (set.presets[i].data) == "factory:" + all[i].data.presetId);
    }

    // provenance record lists every preset
    std::string provenance;
    CHECK (fileio::readFileLimited (dir / "PROVENANCE.md", 1 << 20, provenance).ok);
    for (const auto& l : all)
        CHECK_MSG (provenance.find (l.data.presetId) != std::string::npos && provenance.find (l.fileName) != std::string::npos,
                   "PROVENANCE.md lacks " + l.fileName);
    CHECK (provenance.find ("License") != std::string::npos);

    std::cout << "FactoryPresetTests passed (" << all.size() << " presets)\n";
    return 0;
}
