#include "HelpContent.h"
#include "AdvancedTabs.h"
#include "ParamFormat.h"

#include <map>

namespace snaral::ui::HelpContent
{
namespace
{
struct Purpose
{
    const char* id;
    const char* text;
};

// One short, accurate sentence per parameter. Ranges, units and defaults are NOT repeated here; they are formatted
// from the parameter table when the page is built.
const Purpose purposes[] = {
    // Macros
    { "weight", "Macro. Amount of the Sub layer (curved response). At 0 the Sub is silent." },
    { "growl",  "Macro. Level of the Mid layer and a lean toward Source B. At 0 the Mid and texture layers are silent." },
    { "bite",   "Macro. Adds filter emphasis and extra drive to the Mid layer. The Sub is not affected." },
    { "motion", "Macro. Scales the depth of contour and LFO movement. At 0 there is no modulation depth." },
    { "space",  "Macro. Widens the Mid layer. At 0 the Mid is mono. The Sub always stays centred." },
    // Voice
    { "voice_mode", "Mono plays one note at a time. Poly plays chords." },
    { "legato", "When on, overlapping notes in Mono mode continue without retriggering the envelopes." },
    { "glide_ms", "Portamento time between notes. 0 turns glide off." },
    { "pitch_bend_range", "How far the pitch wheel bends, in semitones each way." },
    { "velocity_to_level", "How much note velocity changes loudness." },
    { "velocity_to_attack", "How much note velocity changes envelope attack time." },
    { "master_tune_hz", "Reference pitch of A4." },
    { "transpose_semitones", "Shifts every note by whole semitones." },
    // Sub
    { "sub_waveform", "Shape of the Sub oscillator: sine or triangle." },
    { "sub_tune_cents", "Fine tune of the Sub layer, in cents." },
    { "sub_level", "Level of the Sub layer. Together with Weight it sets the Sub gain." },
    { "sub_amp_attack_ms", "Sub envelope attack time." },
    { "sub_amp_decay_ms", "Sub envelope decay time." },
    { "sub_amp_sustain", "Sub envelope sustain level." },
    { "sub_amp_release_ms", "Sub envelope release time." },
    { "anchor_enabled", "Sub Anchor keeps the Sub balance consistent across the note range. Off by default." },
    { "anchor_note", "The note at which Sub Anchor applies no compensation." },
    { "anchor_strength", "How much Sub Anchor compensation is applied as notes move away from the anchor note." },
    // Mid sources
    { "mid_source_a", "First oscillator family of the Mid layer: Saw Stack, Pulse Pair, Folded Triangle or Formant Pair." },
    { "mid_source_b", "Second oscillator family of the Mid layer." },
    { "mid_blend", "Crossfade between Source A (0) and Source B (100)." },
    { "mid_table_position", "Source position for Folded Triangle (fold amount) and Formant Pair (vowel-like peaks)." },
    { "mid_pulse_width", "Pulse width of the Pulse Pair source." },
    { "mid_tune_cents", "Fine tune of the Mid layer, in cents." },
    { "mid_level", "Level of the Mid layer. Together with Growl it sets the Mid gain." },
    { "mid_amp_attack_ms", "Mid envelope attack time." },
    { "mid_amp_decay_ms", "Mid envelope decay time." },
    { "mid_amp_sustain", "Mid envelope sustain level." },
    { "mid_amp_release_ms", "Mid envelope release time." },
    // Filter and motion
    { "filter_cutoff_hz", "Filter cutoff on a logarithmic scale. Contour, LFO and Trajectory move it." },
    { "filter_resonance", "Filter resonance (emphasis at the cutoff)." },
    { "filter_type", "Low-pass or band-pass emphasis. The filter affects the Mid layer only." },
    { "contour_attack_ms", "Time for the contour to rise from its baseline to the peak." },
    { "contour_decay_ms", "Time for the contour to fall from the peak to the sustain level." },
    { "contour_sustain", "Level the contour holds while the note is held." },
    { "contour_release_ms", "Time for the contour to return to baseline after note-off." },
    { "contour_shape", "Contour curve: linear, ease-in or ease-out." },
    { "contour_depth", "How far the contour moves the filter and source position. Scaled by Motion." },
    { "lfo_mode", "Free runs at a rate in Hz. Sync follows the host tempo and falls back to the free rate when no tempo is available." },
    { "lfo_rate_hz", "LFO speed in free mode." },
    { "lfo_sync_division", "LFO length in sync mode, from a whole note to a 1/32 note, straight, dotted or triplet." },
    { "lfo_waveform", "LFO shape: sine, triangle, saw or pulse." },
    { "lfo_retrigger", "When on, each new note restarts the LFO phase. When off the LFO keeps running." },
    { "lfo_phase", "Starting phase of the LFO in degrees." },
    { "lfo_depth", "How far the LFO moves the filter and source position. Scaled by Motion." },
    // Snarl Trajectory
    { "traj_enabled", "Applies the Snarl Trajectory (drawn body, peak and return contour) to the Mid layer." },
    { "traj_time_mode", "Trajectory length in milliseconds (ms) or as a tempo-synced division (sync)." },
    { "traj_length_ms", "Trajectory length in free (ms) mode." },
    { "traj_length_div", "Trajectory length in sync mode." },
    { "traj_dest", "What the trajectory moves: filter cutoff, source position or both. The Sub is never affected." },
    { "traj_depth", "How strongly the trajectory is applied." },
    { "traj_curve", "Shape between points: toward 0 more linear, in the middle smooth, toward 100 an eased exponential." },
    { "traj_loop", "Repeats the trajectory while the note is held." },
    { "traj_x2", "Time position of trajectory point 2, as a fraction of the length. Edited by dragging in the trajectory editor." },
    { "traj_x3", "Time position of trajectory point 3, as a fraction of the length. Edited in the trajectory editor." },
    { "traj_x4", "Time position of trajectory point 4, as a fraction of the length. Edited in the trajectory editor." },
    { "traj_y1", "Depth of trajectory point 1 (note start). Edited in the trajectory editor." },
    { "traj_y2", "Depth of trajectory point 2. Edited in the trajectory editor." },
    { "traj_y3", "Depth of trajectory point 3. Edited in the trajectory editor." },
    { "traj_y4", "Depth of trajectory point 4. Edited in the trajectory editor." },
    { "traj_y5", "Depth of trajectory point 5 (return point at the end of the length). Edited in the trajectory editor." },
    // Drive and quality
    { "drive_type", "Drive character: warm saturation or wave fold. Drive affects the Mid layer only." },
    { "drive_amount", "How hard the Mid layer is driven. Bite adds to this." },
    { "drive_tone", "Tilts the tone of the driven signal." },
    { "drive_mix", "Blend between the dry and driven Mid signal." },
    { "oversampling_mode", "Oversamples nonlinear Mid processing (Off, x2, x4). Less aliasing, more CPU, and the plug-in reports the added latency to the host." },
    // Texture
    { "noise_level", "Level of the noise layer. Off at 0. Mid layer only." },
    { "noise_tone", "Brightness of the noise." },
    { "metal_level", "Level of the metallic resonant layer. Off at 0. Mid layer only." },
    { "metal_decay_ms", "How long the metallic layer rings." },
    { "metal_tune", "Tuning of the metallic layer, following the played note." },
    // Effects
    { "furnace_enabled", "Turns Formant Furnace on. It adds moving vowel-like emphasis to the Mid layer only." },
    { "furnace_vowel", "Position of the moving resonant peaks." },
    { "furnace_spacing", "Distance between the resonant peaks." },
    { "furnace_resonance", "Strength of the resonant peaks." },
    { "furnace_env_amount", "How much the contour moves the vowel position." },
    { "furnace_lfo_amount", "How much the LFO moves the vowel position." },
    { "furnace_mix", "Blend between the dry and Furnace signal." },
    { "fracture_enabled", "Turns Harmonic Fracture on. It applies different waveshaping to each of three bands of the Mid layer." },
    { "fracture_crossover_hz", "Frequency where the bands split." },
    { "fracture_drive_low", "Waveshaping drive of the low band." },
    { "fracture_drive_mid", "Waveshaping drive of the middle band." },
    { "fracture_drive_high", "Waveshaping drive of the high band." },
    { "fracture_symmetry", "Symmetry of the fold in the waveshaper." },
    { "fracture_band_mix", "Balance between the bands." },
    { "fracture_comp_db", "Output compensation for the effect." },
    { "fracture_mix", "Blend between the dry and Fracture signal." },
    // Output
    { "output_trim_db", "Output level after the Sub and Mid are summed." },
    { "limiter_enabled", "Safety limiter on the output. A peak safety stage, not a loudness maximiser." },
    { "limiter_ceiling_db", "Highest level the limiter lets through." },
    // Utility
    { "bypass", "Click-safe bypass of the instrument output. Host bypass is also supported." },
    { "layer_solo", "Listen to only the Sub or only the Mid for diagnosis. Not stored in presets." },
    { "sub_mute", "Mutes the Sub layer. Not stored in presets." },
    { "mid_mute", "Mutes the Mid layer. Not stored in presets." },
};

const std::map<juce::String, juce::String>& purposeMap()
{
    static const auto m = [] {
        std::map<juce::String, juce::String> r;
        for (const auto& p : purposes)
            r[p.id] = p.text;
        return r;
    }();
    return m;
}

HelpTopic make (const char* id, const char* category, const juce::String& title, const juce::String& body,
                const juce::String& keywords = {})
{
    return { id, category, title, body, keywords };
}

juce::String idFor (const juce::String& tabName)
{
    return "tab-" + tabName.toLowerCase().replace (" & ", "-").replace (" ", "-");
}

juce::String sectionReference (const AdvancedSectionSpec& spec)
{
    juce::String out = spec.title;
    out << "\n";
    for (const auto* id : spec.parameterIds)
        if (const auto* d = findParamDef (id))
            out << "  " << describeParameter (*d) << "\n";
    return out;
}

std::vector<HelpTopic> build()
{
    std::vector<HelpTopic> t;
    const juce::String nl = "\n";

    t.push_back (make ("quick-start", "Getting started", "Quick start",
        "1. Pick a preset in the header, or start from the default sound.\n"
        "2. Play notes from your keyboard or host. Choose Mono or Poly under Advanced > Voice.\n"
        "3. Shape the sound with the five macros: Weight, Growl, Bite, Motion and Space.\n"
        "4. Open Advanced when you need detail. Tabs: Voice, Sub, Sources, Filter & Motion, Drive, Texture, Effects, Output.\n"
        "5. Right-click any control for reset, copy, paste, typing a value and MIDI learn.\n"
        "6. Open Help (this page) any time, and run the introduction again from Options.",
        "start begin tutorial first"));

    t.push_back (make ("signal-flow", "Getting started", "Signal flow",
        "Snaral has two layers that follow the same played pitch.\n\n"
        "Sub: a centred, mono sine or triangle with its own envelope. It bypasses the Mid filter, motion, drive, texture, "
        "effects and Space, and is identical in the left and right channels.\n\n"
        "Mid: Source A and Source B are blended, with optional noise and metal texture, then pass the filter (moved by the contour, "
        "LFO and Snarl Trajectory), the optional Formant Furnace and Harmonic Fracture effects, Drive (optionally oversampled) and "
        "width.\n\n"
        "The Sub and Mid are summed, then Output Trim and the safety Limiter are applied.",
        "layers sub mid flow chain routing"));

    // ---- macros ----
    t.push_back (make ("macros", "Macros", "The five macros",
        "Weight, Growl, Bite, Motion and Space are the main controls. Each one moves several detailed parameters in a fixed, "
        "documented way, and each can be automated by the host.\n\n"
        "Double-click a macro to reset it. Click its value to type a number. Right-click for the full menu.",
        "weight growl bite motion space"));

    const char* macroIds[] = { "weight", "growl", "bite", "motion", "space" };
    const char* macroDetail[] = {
        "Weight sets how much Sub you hear. The response is curved, so the lowest part of the range is gentle. At 0 the Sub is silent.",
        "Growl sets the level of the Mid layer (curved response) and leans the Source A/B blend toward Source B by up to 20 points. At 0 the Mid and the texture layers are silent.",
        "Bite raises filter emphasis (up to 18 semitones of peak offset) and adds up to 12 dB of drive to the Mid layer. It never touches the Sub.",
        "Motion scales how deep the contour and LFO movements are. At 0 there is no modulation depth. It cannot change pitch or the Sub.",
        "Space widens the Mid layer, up to 120 percent width at 100. At 0 the Mid is mono. The Sub stays centred at every setting."
    };
    for (int i = 0; i < 5; ++i)
    {
        const auto* d = findParamDef (macroIds[i]);
        const auto name = toJuce (d->name);
        t.push_back (make (("macro-" + juce::String (macroIds[i])).toRawUTF8(), "Macros", name,
                           juce::String (macroDetail[i]) + "\n\n" + describeParameter (*d), "macro " + name));
    }

    // ---- Advanced tabs ----
    for (int ti = 0; ti < numAdvancedTabs; ++ti)
    {
        const auto tab = static_cast<AdvancedTab> (ti);
        const juce::String tabName = advancedTabName (tab);
        juce::String body = "Controls on the " + tabName + " tab. Each line gives the name, range and default, then what it does.\n\n";
        juce::String keywords = tabName;
        for (const auto& spec : advancedSections())
            if (spec.tab == tab)
            {
                body << sectionReference (spec) << "\n";
                keywords << " " << spec.title;
                for (const auto* id : spec.parameterIds)
                    keywords << " " << id;
            }
        if (tab == AdvancedTab::FilterMotion)
            body << "Snarl Trajectory editor\n  Drag the five points to draw the body, peak and return of the movement. Points 1 and 5 move "
                    "up and down only; points 2 to 4 also move in time and can never cross their neighbours. Arrow keys nudge the "
                    "selected point; Page Up and Page Down choose another point. The length is shown in milliseconds or as a musical "
                    "division.\n";
        t.push_back (make (idFor (tabName).toRawUTF8(), "Advanced", tabName, body.trimEnd(), keywords));
    }

    // ---- interaction guides ----
    t.push_back (make ("shortcuts", "Reference", "Keyboard shortcuts",
        "Tab / Shift+Tab: move between controls.\n"
        "Knobs and sliders: Up/Right and Down/Left change the value by 1 percent of the range (Shift 10 percent, Ctrl 0.1 percent; "
        "whole steps for integer controls). Home and End jump to the minimum and maximum.\n"
        "Enter or F2: type a value. Enter applies it, Escape keeps the old value.\n"
        "Delete or Backspace: reset to the default.\n"
        "Shift+F10: open the control's context menu.\n"
        "Mouse wheel changes a value only while the control has keyboard focus, so scrolling a panel never changes sounds.\n"
        "Advanced tabs: Left/Right, Home and End choose the tab. Space or Enter opens and closes a section.\n"
        "Trajectory editor: Page Up / Page Down or [ and ] choose a point; arrows move it; Delete resets it.\n"
        "Introduction tour: Right or Enter for next, Left for back, Escape to skip.",
        "keys keyboard hotkeys accessibility"));

    t.push_back (make ("midi-learn", "Reference", "MIDI learn",
        "Right-click a control (or press Shift+F10) and choose MIDI learn. The control shows a LEARN badge while it waits; move a "
        "knob or fader on your controller to assign it. Mapped controls show a MIDI badge. Choose Unlink MIDI mapping in the same "
        "menu to remove the assignment.\n\n"
        "Mappings never change other controls silently. In a plug-in your host owns MIDI routing; Snaral only learns what reaches it.",
        "cc controller assign map unlink"));

    t.push_back (make ("snarl-trajectory", "Reference", "Snarl Trajectory",
        "The trajectory is a five-point curve (body, peak, return) that moves the filter and/or the source position after each "
        "note. It is drawn on a canvas in Filter & Motion. Length is in milliseconds or a tempo-synced division. Shapes between "
        "points follow the Curve amount. The Sub is never affected. The trajectory does nothing until Trajectory On is enabled.",
        "snarl trajectory editor points curve"));

    t.push_back (make ("signal-displays", "Reference", "Scope, level meter and DSP CPU",
        "Scope: a recent-output waveform refreshed about 30 times per second, with Output, Sub and Mid views for diagnosis. It is a "
        "display aid; a good-looking waveform does not prove the sound is good.\n"
        "Level meter: peak and RMS in dBFS. The tick holds the peak for 1.5 seconds then falls at 20 dB per second. CLIP appears "
        "only when the output actually reached 0 dBFS, and clears after 2 seconds or when clicked.\n"
        "DSP CPU: Snaral's processing time per audio block as a percentage of the time available, smoothed over 1 second, for one "
        "audio core. It is not the host's total CPU use. OVERRUN appears only when a callback measurably exceeded its deadline.\n"
        "Both displays can be hidden in Options. With animations off they update twice a second without motion.",
        "oscilloscope meter cpu clip overrun"));

    t.push_back (make ("options", "Reference", "Options",
        "Theme: CDL (default), Midnight, High Contrast, Teal Drift and Daylight. Tooltips on/off. Animations on/off (off = reduced "
        "motion). UI scale. Show or hide the scope and the CPU gauge. Run the introduction again.\n"
        "Standalone only: audio and MIDI device selection and a 440 Hz reference tone at -12 dBFS that you start and stop yourself.\n"
        "In a plug-in the host owns audio devices, sample rate and buffer size, so those settings are not offered.\n"
        "Options are stored per user and are not part of presets or host automation.",
        "theme tooltips animation scale reference tone standalone"));

    t.push_back (make ("accessibility", "Reference", "Accessibility",
        "Every control can be reached and operated from the keyboard and has a visible focus ring and an accessible name. State is "
        "never shown by colour alone: toggles carry a check mark, statuses carry an icon and a word, learn state carries a badge. "
        "Use the High Contrast theme and UI scale for readability. Turning tooltips off does not remove accessible descriptions. "
        "Support for screen readers depends on the host and plug-in format.",
        "screen reader contrast zoom"));

    t.push_back (make ("formats", "Reference", "Formats and platforms",
        "Snaral ships as VST3 and Standalone on Windows, macOS and Linux; CLAP on all three; Audio Unit on macOS; and LV2 on "
        "Linux (best effort). Beta builds: Windows x64, macOS arm64 and Intel, Linux x64.\n\n"
        "Not available yet: MPE, Variation Morph, Motion Snapshot, Readability Monitor, Preset Exchange bundles and the Spectral "
        "Shard, Clockwork Gate and Inharmonic Striker effects.",
        "vst3 clap au lv2 standalone windows mac linux limitations"));

    t.push_back (make ("troubleshooting", "Support", "Troubleshooting",
        "No sound: check that notes reach Snaral, that Bypass is off, that Layer Solo and the Sub/Mid mutes are clear, that Weight "
        "or Growl are above 0, and that Output Trim and the Limiter ceiling are sensible.\n"
        "LFO or Trajectory not following tempo: the host did not supply a valid tempo, so the free rate is used. The status bar says "
        "when this happens.\n"
        "High CPU: set Oversampling to Off or x2, reduce voices in Poly mode, and read the DSP CPU gauge, which shows Snaral's own "
        "load only.\n"
        "Settings look wrong after an update: Reset on the action row restores sound defaults without touching your Options.",
        "problem silent cpu tempo no sound"));

    t.push_back (make ("support", "Support", "About and support",
        "Snaral is made by Circuit Drift Labs. Website: " + juce::String (cdlWebsite) + "\n"
        "The website opens in your browser only when you choose the link below. Support contact details will be listed here once "
        "the support process is set up.",
        "circuit drift labs website contact version about"));

    return t;
}
} // namespace

const std::vector<HelpTopic>& topics()
{
    static const auto all = build();
    return all;
}

const HelpTopic* findTopic (const juce::String& id)
{
    for (const auto& t : topics())
        if (t.id == id)
            return &t;
    return nullptr;
}

juce::String parameterPurpose (const juce::String& id)
{
    const auto& m = purposeMap();
    const auto it = m.find (id);
    return it == m.end() ? juce::String() : it->second;
}

std::vector<juce::String> undocumentedParameterIds()
{
    std::vector<juce::String> out;
    for (const auto& d : parameterTable)
        if (parameterPurpose (toJuce (d.id)).isEmpty())
            out.push_back (toJuce (d.id));
    return out;
}

juce::String describeParameter (const ParamDef& d)
{
    juce::String s = toJuce (d.name);
    if (d.type == ParamType::Float && d.unit.size() > 0)
        s << " (" << toJuce (d.unit) << ")";
    s << ": " << describeParamRange (d) << ", default " << formatParamValue (d, d.defaultValue) << ". "
      << parameterPurpose (toJuce (d.id));
    return s;
}

juce::String topicIdForParameter (const juce::String& id)
{
    const auto* d = findParamDef (id);
    if (d == nullptr)
        return {};
    if (toJuce (d->group) == "Macros")
        return "macro-" + id;
    for (const auto& spec : advancedSections())
        for (const auto* pid : spec.parameterIds)
            if (id == pid)
                return idFor (advancedTabName (spec.tab));
    for (int i = 0; i < numAdvancedTabs; ++i)
    {
        const auto groups = advancedGroupsForTab (static_cast<AdvancedTab> (i));
        for (const auto& g : groups)
            if (g == toJuce (d->group))
                return idFor (advancedTabName (static_cast<AdvancedTab> (i)));
    }
    if (isEditedByDedicatedWidget (id))
        return "tab-filter-motion";
    return "quick-start";
}

std::vector<int> search (const juce::String& query)
{
    juce::StringArray terms;
    terms.addTokens (query.trim().toLowerCase(), " ", "\"");
    terms.removeEmptyStrings();

    std::vector<std::pair<int, int>> scored;   // (score, index)
    const auto& all = topics();
    for (int i = 0; i < static_cast<int> (all.size()); ++i)
    {
        const auto& t = all[static_cast<std::size_t> (i)];
        const auto title = t.title.toLowerCase();
        const auto keys = t.keywords.toLowerCase();
        const auto body = t.body.toLowerCase();
        int score = 0;
        bool all_ = true;
        for (const auto& term : terms)
        {
            int s = 0;
            if (title.contains (term)) s += 10;
            if (keys.contains (term))  s += 5;
            if (body.contains (term))  s += 1;
            if (s == 0)
            {
                all_ = false;
                break;
            }
            score += s;
        }
        if (all_)
            scored.emplace_back (-score, i);
    }
    if (terms.isEmpty())
    {
        scored.clear();
        for (int i = 0; i < static_cast<int> (all.size()); ++i)
            scored.emplace_back (0, i);
    }
    std::stable_sort (scored.begin(), scored.end(), [] (auto& a, auto& b) { return a.first < b.first; });
    std::vector<int> out;
    for (auto& p : scored)
        out.push_back (p.second);
    return out;
}
} // namespace snaral::ui::HelpContent
