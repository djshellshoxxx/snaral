#!/usr/bin/env python3
"""Deterministic generator for the Snaral factory presets.

Writes Resources/presets/NN_<slug>.json in EXACTLY the format of the C++ writer (snaral::writePreset,
Source/State/Pure/PresetFormat.cpp): lexically ordered keys, 2-space indent, shortest round-trip float32
numbers in plain decimal notation, trailing newline. Also writes Resources/presets/PROVENANCE.md.

The parameter registry is read from Source/Parameters/ParameterList.def (+ Groups/*.def), so every
registry change (new appended parameter) requires re-running this script:

    python3 tools/gen_presets.py            # regenerate files
    python3 tools/gen_presets.py --check    # exit 1 if any file is stale (CI)

Preset authoring rules live in docs/specs/factory-presets.md. Values are in registry DOMAIN units
(%, Hz, ms, dB, choice names, booleans). Everything not listed in a preset keeps the registry default.
"""
import hashlib
import math
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEF_DIR = ROOT / "Source" / "Parameters"
OUT_DIR = ROOT / "Resources" / "presets"
FORMAT_HEADER = ROOT / "Source" / "State" / "Pure" / "PresetFormat.h"

AUTHOR = "Circuit Drift Labs"
FLAG_VALUES = {"pfNone": 0, "pfNoPreset": 1, "pfNoAutomation": 2, "pfInteger": 4}


# --------------------------------------------------------------------------------------------------------
# registry parsing
# --------------------------------------------------------------------------------------------------------
def f32(x):
    return struct.unpack("f", struct.pack("f", float(x)))[0]


def strip_comments(text):
    return re.sub(r"//[^\n]*", "", text)


def split_args(body):
    args, cur, depth, in_str = [], [], 0, False
    for ch in body:
        if ch == '"':
            in_str = not in_str
            cur.append(ch)
        elif in_str:
            cur.append(ch)
        elif ch == "(":
            depth += 1
            cur.append(ch)
        elif ch == ")":
            depth -= 1
            cur.append(ch)
        elif ch == "," and depth == 0:
            args.append("".join(cur).strip())
            cur = []
        else:
            cur.append(ch)
    tail = "".join(cur).strip()
    if tail:
        args.append(tail)
    return args


def unquote(s):
    assert s.startswith('"') and s.endswith('"'), s
    return s[1:-1]


def num(s):
    return float(s.rstrip("fF"))


def flags(s):
    return sum(FLAG_VALUES[p.strip()] for p in s.split("|"))


def read_registry():
    top = strip_comments((DEF_DIR / "ParameterList.def").read_text())
    ordered = []
    for inc in re.findall(r'#include\s+"([^"]+)"', top):
        text = strip_comments((DEF_DIR / inc).read_text())
        pos = 0
        while True:
            m = re.compile(r"SNARAL_(FLOAT|BOOL|CHOICE)\s*\(").search(text, pos)
            if not m:
                break
            i, depth, in_str = m.end(), 1, False
            while depth:
                c = text[i]
                if c == '"':
                    in_str = not in_str
                elif not in_str:
                    depth += c == "("
                    depth -= c == ")"
                i += 1
            args = split_args(text[m.end():i - 1])
            kind = m.group(1)
            p = {"kind": kind, "id": args[0], "name": unquote(args[1]), "group": unquote(args[2])}
            if kind == "FLOAT":
                p.update(min=f32(num(args[3])), max=f32(num(args[4])), default=f32(num(args[5])),
                         unit=unquote(args[6]), scale=args[7], flags=flags(args[8]))
            elif kind == "BOOL":
                p.update(default=args[3].strip() == "true", flags=flags(args[4]))
            else:
                p.update(default=int(args[3]), flags=flags(args[4]), choices=[unquote(a) for a in args[5:]])
            ordered.append(p)
            pos = i
    ids = [p["id"] for p in ordered]
    assert len(ids) == len(set(ids)), "duplicate parameter id in registry"
    return ordered


# --------------------------------------------------------------------------------------------------------
# number formatting identical to snaral::text::formatFloat
# --------------------------------------------------------------------------------------------------------
def fmt_float(value):
    v = f32(value)
    if v == 0 or math.isnan(v) or math.isinf(v):
        return "0"
    neg = v < 0
    a = abs(v)
    s = None
    for precision in range(1, 10):
        s = "%.*e" % (precision - 1, a)
        if f32(float(s)) == a:
            break
    mant, exp = s.split("e")
    digits = mant.replace(".", "")
    exponent = int(exp)
    digits = digits.rstrip("0") or "0"
    n = len(digits)
    if exponent >= 0:
        if n <= exponent + 1:
            int_part, frac = digits + "0" * (exponent + 1 - n), ""
        else:
            int_part, frac = digits[:exponent + 1], digits[exponent + 1:]
    else:
        int_part, frac = "0", "0" * (-exponent - 1) + digits
    return ("-" if neg else "") + int_part + ("." + frac if frac else "")


def quote(s):
    out = ['"']
    for ch in s:
        o = ord(ch)
        if ch == '"':
            out.append('\\"')
        elif ch == "\\":
            out.append("\\\\")
        elif ch == "\b":
            out.append("\\b")
        elif ch == "\f":
            out.append("\\f")
        elif ch == "\n":
            out.append("\\n")
        elif ch == "\r":
            out.append("\\r")
        elif ch == "\t":
            out.append("\\t")
        elif o < 0x20 or o == 0x7F:
            out.append("\\u%04x" % o)
        else:
            out.append(ch)
    out.append('"')
    return "".join(out)


def writer_state_version():
    m = re.search(r'presetWriterStateVersion\s*=\s*"([^"]*)"', FORMAT_HEADER.read_text())
    assert m, "presetWriterStateVersion not found in PresetFormat.h"
    return m.group(1)


# --------------------------------------------------------------------------------------------------------
# preset serialisation
# --------------------------------------------------------------------------------------------------------
def value_json(p, v):
    if p["kind"] == "FLOAT":
        return fmt_float(v)
    if p["kind"] == "BOOL":
        return "true" if v else "false"
    return quote(p["choices"][v])


def resolve(registry, overrides, preset_name):
    """Validates overrides against the registry and returns {id: canonical value}."""
    by_id = {p["id"]: p for p in registry}
    values = {}
    for p in registry:
        if p["flags"] & FLAG_VALUES["pfNoPreset"]:
            continue
        values[p["id"]] = p["default"]
    for pid, v in overrides.items():
        if pid not in by_id:
            raise SystemExit("%s: unknown parameter %s" % (preset_name, pid))
        p = by_id[pid]
        if p["flags"] & FLAG_VALUES["pfNoPreset"]:
            raise SystemExit("%s: %s is not stored in presets" % (preset_name, pid))
        if p["kind"] == "FLOAT":
            if isinstance(v, bool) or not isinstance(v, (int, float)):
                raise SystemExit("%s: %s expects a number" % (preset_name, pid))
            fv = f32(v)
            if fv < p["min"] or fv > p["max"]:
                raise SystemExit("%s: %s=%s outside [%s, %s]" % (preset_name, pid, v, p["min"], p["max"]))
            if p["flags"] & FLAG_VALUES["pfInteger"] and fv != round(fv):
                raise SystemExit("%s: %s must be a whole number" % (preset_name, pid))
            values[pid] = fv
        elif p["kind"] == "BOOL":
            if not isinstance(v, bool):
                raise SystemExit("%s: %s expects True/False" % (preset_name, pid))
            values[pid] = v
        else:
            if v not in p["choices"]:
                raise SystemExit("%s: %s=%r not in %s" % (preset_name, pid, v, p["choices"]))
            values[pid] = p["choices"].index(v)
    return values


def serialise(registry, preset, state_version):
    values = resolve(registry, preset["params"], preset["name"])
    by_id = {p["id"]: p for p in registry}
    assert len(preset["tags"]) <= 32 and len(set(t.lower() for t in preset["tags"])) == len(preset["tags"])
    assert len(preset["name"]) <= 128 and len(preset["description"]) <= 2048
    for t in preset["tags"]:
        assert 0 < len(t) <= 64
    lines = ["{", '  "format": "snaral-preset",', '  "parameters": {']
    ids = sorted(values.keys(), key=lambda s: s.encode("utf-8"))
    for i, pid in enumerate(ids):
        lines.append("    %s: %s%s" % (quote(pid), value_json(by_id[pid], values[pid]), "," if i + 1 < len(ids) else ""))
    lines += ["  },", '  "plugin": {', '    "name": "Snaral",', '    "state_version": %s' % quote(state_version), "  },",
              '  "preset": {',
              '    "author": %s,' % quote(AUTHOR),
              '    "description": %s,' % quote(preset["description"]),
              '    "name": %s,' % quote(preset["name"]),
              '    "preset_id": %s,' % quote(preset["id"]),
              '    "tags": [%s]' % ", ".join(quote(t) for t in preset["tags"]),
              "  },", '  "schema_version": 1', "}"]
    return "\n".join(lines) + "\n"


# --------------------------------------------------------------------------------------------------------
# the factory set
# --------------------------------------------------------------------------------------------------------
def slug(name):
    return re.sub(r"[^a-z0-9]+", "_", name.lower()).strip("_")


PRESETS = []


def preset(name, tags, description, **params):
    # The registry default LFO depth is 50 %, which would make every patch wobble as soon as Motion is raised.
    # Factory presets only use the LFO when they say so.
    params.setdefault("lfo_depth", 0)
    PRESETS.append({"name": name, "id": "factory." + slug(name), "tags": tags, "description": description, "params": params})


# Tag convention (docs/specs/factory-presets.md): [role, movement, layer balance, texture, performance mode, ...extras]
# --- the ten required roles -------------------------------------------------------------------------------
preset("Foundation", ["sub", "static", "sub-forward", "clean", "mono"],
       "Clean centered low bass. The sine sub carries the note and a soft filtered mid adds just enough definition. Static and mono.",
       weight=85, growl=20, bite=0, motion=0, space=0,
       sub_waveform="sine", sub_level=100, mid_level=45, mid_source_a="saw_stack", mid_blend=0,
       filter_cutoff_hz=420, filter_resonance=8, filter_type="lowpass",
       mid_amp_attack_ms=4, mid_amp_decay_ms=500, mid_amp_sustain=85, mid_amp_release_ms=160,
       drive_type="warm_saturation", drive_amount=4, noise_level=0, metal_level=0, voice_mode="mono")

preset("Plain Weight", ["sub", "static", "sub-forward", "clean", "mono"],
       "Straight sub-forward bass with an easy to hear mid layer. There is no movement, so the macros are simple to learn.",
       weight=78, growl=40, bite=10, motion=0, space=0,
       sub_level=100, mid_level=70, mid_source_a="pulse_pair", mid_pulse_width=50,
       filter_cutoff_hz=700, filter_resonance=12, drive_amount=12, sub_amp_release_ms=150, voice_mode="mono")

preset("Throat Sweep", ["growl", "contour", "balanced", "clean", "mono"],
       "Vowel-like moving growl. The note contour and the trajectory sweep the filter and the formant position together.",
       weight=62, growl=72, bite=38, motion=70, space=10,
       mid_level=90, mid_source_a="formant_pair", mid_source_b="saw_stack", mid_blend=25, mid_table_position=40,
       filter_cutoff_hz=900, filter_resonance=30, filter_type="lowpass",
       contour_attack_ms=5, contour_decay_ms=420, contour_sustain=20, contour_release_ms=220,
       contour_shape="ease_out", contour_depth=75,
       lfo_mode="free", lfo_rate_hz=0.8, lfo_waveform="sine", lfo_depth=15,
       traj_enabled=True, traj_time_mode="ms", traj_length_ms=520, traj_dest="both", traj_depth=65, traj_curve=55,
       drive_amount=30)

preset("Saw Rasp", ["rasp", "contour", "balanced", "clean", "mono"],
       "Dense saw rasp with moderate saturation. Resonance is kept in check so the peak stays controlled.",
       weight=66, growl=76, bite=48, motion=20, space=0,
       mid_level=85, mid_source_a="saw_stack", mid_blend=0,
       filter_cutoff_hz=1500, filter_resonance=28,
       contour_decay_ms=300, contour_sustain=35, contour_shape="ease_in", contour_depth=40,
       drive_type="warm_saturation", drive_amount=45, drive_tone=55, voice_mode="mono")

preset("Folded Jaw", ["rasp", "contour", "balanced", "clean", "mono"],
       "Sharper serrated mid from a folded triangle. Bite works well below maximum, somewhere around half way.",
       weight=60, growl=74, bite=52, motion=25, space=0,
       mid_level=100, mid_source_a="folded_triangle", mid_table_position=60,
       filter_cutoff_hz=1900, filter_resonance=22,
       contour_decay_ms=280, contour_sustain=30, contour_depth=45,
       traj_enabled=True, traj_time_mode="ms", traj_length_ms=450, traj_dest="source_position", traj_depth=45, traj_curve=50,
       drive_type="wave_fold", drive_amount=40, drive_tone=50, voice_mode="mono")

preset("Machine Bell", ["texture", "static", "sub-forward", "metal", "mono"],
       "Quiet metallic edge on the attack with a short decay. It adds bite to the front of the note without changing the body.",
       weight=70, growl=55, bite=25, motion=0, space=0,
       mid_level=80, mid_source_a="saw_stack",
       filter_cutoff_hz=1100, filter_resonance=15,
       metal_level=24, metal_decay_ms=140, metal_tune=55, drive_amount=15, voice_mode="mono")

preset("Sync Stalker", ["wobble", "synced", "balanced", "clean", "mono"],
       "Tempo-locked filter movement at eighth notes. Without a host tempo it falls back to the free LFO rate.",
       weight=70, growl=68, bite=30, motion=85, space=0,
       mid_level=90, mid_source_a="saw_stack",
       filter_cutoff_hz=800, filter_resonance=30,
       contour_depth=20, contour_decay_ms=250, contour_sustain=40,
       lfo_mode="sync", lfo_sync_division="1/8", lfo_rate_hz=2.5, lfo_waveform="triangle", lfo_retrigger=True, lfo_depth=75,
       drive_amount=22, voice_mode="mono")

preset("Free Rotor", ["wobble", "free", "balanced", "clean", "mono"],
       "Free-running filter movement that does not need host tempo. The rate is about three cycles per second.",
       weight=72, growl=66, bite=30, motion=80, space=0,
       mid_level=90, mid_source_a="saw_stack",
       filter_cutoff_hz=850, filter_resonance=28,
       contour_depth=15,
       lfo_mode="free", lfo_rate_hz=3.2, lfo_waveform="sine", lfo_retrigger=False, lfo_depth=70,
       drive_amount=20, voice_mode="mono")

preset("Wide Teeth", ["width", "contour", "balanced", "clean", "mono"],
       "Wide mid texture with a mono sub. Space opens the mid only and the sub stays centered.",
       weight=64, growl=80, bite=40, motion=35, space=72,
       mid_level=95, mid_source_a="pulse_pair", mid_source_b="saw_stack", mid_blend=40, mid_pulse_width=38,
       filter_cutoff_hz=1700, filter_resonance=20,
       contour_decay_ms=320, contour_sustain=35, contour_depth=30,
       drive_amount=25, voice_mode="mono")

preset("Legato Pressure", ["bassline", "contour", "sub-forward", "clean", "legato"],
       "Mono legato bassline patch with a short glide. Notes slide into each other and the pitch stays clear.",
       weight=80, growl=55, bite=30, motion=15, space=0,
       voice_mode="mono", legato=True, glide_ms=70, pitch_bend_range=2,
       mid_level=75, mid_source_a="saw_stack",
       filter_cutoff_hz=650, filter_resonance=18,
       contour_decay_ms=250, contour_sustain=40, contour_depth=30,
       drive_amount=20)

# --- extended library -----------------------------------------------------------------------------------
preset("Rolling Reese", ["reese", "free", "balanced", "clean", "legato"],
       "Slow phasing reese style bass. A saw stack under a gently drifting filter, suited to rolling lines.",
       weight=72, growl=70, bite=25, motion=40, space=0,
       voice_mode="mono", legato=True, glide_ms=25,
       mid_level=90, mid_source_a="saw_stack", mid_source_b="saw_stack", mid_blend=50, mid_tune_cents=14,
       filter_cutoff_hz=650, filter_resonance=14,
       contour_depth=15,
       lfo_mode="free", lfo_rate_hz=0.35, lfo_waveform="triangle", lfo_retrigger=False, lfo_depth=35,
       drive_amount=25)

preset("Neuro Growl", ["neuro", "synced", "mid-forward", "clean", "mono"],
       "Dense neuro growl. Formant Furnace and Harmonic Fracture work together on the mid layer under a synced filter.",
       weight=55, growl=85, bite=55, motion=75, space=0,
       mid_level=100, mid_source_a="formant_pair", mid_source_b="folded_triangle", mid_blend=45, mid_table_position=55,
       filter_cutoff_hz=1300, filter_resonance=35,
       contour_depth=40,
       lfo_mode="sync", lfo_sync_division="1/8", lfo_waveform="triangle", lfo_depth=55,
       drive_type="wave_fold", drive_amount=35,
       furnace_enabled=True, furnace_vowel=45, furnace_spacing=55, furnace_resonance=50, furnace_env_amount=55,
       furnace_lfo_amount=40, furnace_mix=60,
       fracture_enabled=True, fracture_crossover_hz=600, fracture_drive_low=15, fracture_drive_mid=40,
       fracture_drive_high=55, fracture_symmetry=50, fracture_band_mix=55, fracture_comp_db=-2, fracture_mix=40,
       oversampling_mode="x2", output_trim_db=-2, voice_mode="mono")

preset("Vowel Talker", ["growl", "free", "balanced", "clean", "mono"],
       "Talking vowel movement. The furnace vowel is swept by a slow LFO while a bandpass filter shapes the throat.",
       weight=58, growl=78, bite=35, motion=60, space=0,
       mid_level=95, mid_source_a="formant_pair", mid_source_b="pulse_pair", mid_blend=30,
       filter_cutoff_hz=1000, filter_resonance=40, filter_type="bandpass",
       lfo_mode="free", lfo_rate_hz=1.1, lfo_waveform="sine", lfo_depth=40,
       contour_depth=20,
       furnace_enabled=True, furnace_vowel=30, furnace_spacing=45, furnace_resonance=55, furnace_env_amount=0,
       furnace_lfo_amount=70, furnace_mix=70,
       drive_amount=15, voice_mode="mono")

preset("Hollow Stab", ["stab", "contour", "mid-forward", "clean", "poly"],
       "Short hollow stab for chords or hits. Pulse tone through a bandpass filter, polyphonic, and it responds to velocity.",
       weight=35, growl=82, bite=30, motion=30, space=25,
       voice_mode="poly", velocity_to_level=40,
       mid_level=95, mid_source_a="pulse_pair", mid_pulse_width=30,
       filter_cutoff_hz=1100, filter_resonance=30, filter_type="bandpass",
       mid_amp_attack_ms=1, mid_amp_decay_ms=220, mid_amp_sustain=0, mid_amp_release_ms=140,
       sub_amp_decay_ms=250, sub_amp_sustain=20, sub_amp_release_ms=100,
       contour_decay_ms=180, contour_sustain=0, contour_shape="ease_out", contour_depth=60,
       drive_amount=18)

preset("Clean Sub", ["sub", "static", "sub-forward", "clean", "mono"],
       "Pure sine sub and nothing else. Sub Anchor is on to even out the level across the keyboard range.",
       weight=100, growl=0, bite=0, motion=0, space=0,
       sub_waveform="sine", sub_level=100, mid_level=0, drive_amount=0,
       anchor_enabled=True, anchor_note=36, anchor_strength=45, voice_mode="mono")

preset("Wobble Quarter", ["wobble", "synced", "balanced", "clean", "mono"],
       "Slow quarter note wobble with a deep filter sweep. It retriggers on every note so the wobble starts the same each time.",
       weight=74, growl=70, bite=35, motion=90, space=0,
       mid_level=90, mid_source_a="saw_stack",
       filter_cutoff_hz=520, filter_resonance=38,
       contour_depth=10,
       lfo_mode="sync", lfo_sync_division="1/4", lfo_waveform="triangle", lfo_retrigger=True, lfo_depth=80,
       drive_amount=25, voice_mode="mono")

preset("Wobble Triplet", ["wobble", "synced", "balanced", "clean", "mono"],
       "Triplet wobble at eighth note triplets. It gives a bouncing feel against straight drums.",
       weight=72, growl=70, bite=33, motion=85, space=0,
       mid_level=90, mid_source_a="pulse_pair", mid_pulse_width=45,
       filter_cutoff_hz=700, filter_resonance=32,
       contour_depth=10,
       lfo_mode="sync", lfo_sync_division="1/8t", lfo_waveform="sine", lfo_retrigger=True, lfo_depth=75,
       drive_amount=22, voice_mode="mono")

preset("Tearout", ["tearout", "synced", "mid-forward", "noise", "mono"],
       "Aggressive tearout mid with heavy folding and fracture. Oversampling is set to 4x to keep the aliasing down.",
       weight=52, growl=90, bite=75, motion=55, space=0,
       mid_level=100, mid_source_a="saw_stack", mid_source_b="folded_triangle", mid_blend=55,
       filter_cutoff_hz=2400, filter_resonance=40,
       contour_depth=25,
       lfo_mode="sync", lfo_sync_division="1/16", lfo_waveform="saw", lfo_depth=60,
       drive_type="wave_fold", drive_amount=70, drive_tone=60,
       noise_level=10, noise_tone=65,
       fracture_enabled=True, fracture_crossover_hz=800, fracture_drive_low=20, fracture_drive_mid=55,
       fracture_drive_high=70, fracture_symmetry=60, fracture_band_mix=50, fracture_comp_db=-3, fracture_mix=55,
       oversampling_mode="x4", output_trim_db=-3, voice_mode="mono")

preset("Noise Gravel", ["texture", "contour", "balanced", "noise", "mono"],
       "Gritty gravel texture from filtered noise under a saw stack. Good for rough, low sections.",
       weight=62, growl=60, bite=40, motion=20, space=0,
       mid_level=85, mid_source_a="saw_stack",
       filter_cutoff_hz=900, filter_resonance=20,
       contour_decay_ms=260, contour_sustain=30, contour_depth=35,
       noise_level=34, noise_tone=38, drive_amount=35, voice_mode="mono")

preset("Five Hands", ["hybrid", "macros", "contour", "free", "balanced", "clean", "mono"],
       "Mid-range macro settings on purpose. Weight, Growl, Bite, Motion and Space each make a clear change, a good place to learn them.",
       weight=68, growl=58, bite=40, motion=50, space=35,
       mid_level=90, mid_source_a="saw_stack", mid_source_b="folded_triangle", mid_blend=35,
       filter_cutoff_hz=1000, filter_resonance=25,
       contour_decay_ms=320, contour_sustain=30, contour_depth=55,
       lfo_mode="free", lfo_rate_hz=1.6, lfo_waveform="sine", lfo_depth=45,
       drive_amount=28, voice_mode="mono")

preset("Pulse Lurker", ["growl", "free", "balanced", "clean", "mono"],
       "Hollow pulse tone that creeps up and down on a stepped LFO. Subtle at low Motion and more obvious as you raise it.",
       weight=70, growl=62, bite=28, motion=40, space=0,
       mid_level=90, mid_source_a="pulse_pair", mid_source_b="saw_stack", mid_blend=20, mid_pulse_width=32,
       filter_cutoff_hz=750, filter_resonance=22,
       lfo_mode="free", lfo_rate_hz=0.9, lfo_waveform="pulse", lfo_depth=40,
       drive_amount=18, voice_mode="mono")

preset("Murk Chord", ["chord", "contour", "mid-forward", "clean", "poly"],
       "Restrained mid-only chord texture. Slow attack, wide space and a quiet sub, for held chords over a bassline.",
       weight=30, growl=72, bite=15, motion=25, space=60,
       voice_mode="poly",
       sub_level=60, sub_amp_attack_ms=30, sub_amp_release_ms=400,
       mid_level=90, mid_source_a="pulse_pair", mid_source_b="formant_pair", mid_blend=30,
       filter_cutoff_hz=700, filter_resonance=12,
       mid_amp_attack_ms=90, mid_amp_decay_ms=800, mid_amp_sustain=70, mid_amp_release_ms=600,
       contour_attack_ms=60, contour_decay_ms=900, contour_sustain=40, contour_depth=30,
       drive_amount=10)

preset("Trajectory Snarl", ["snarl", "contour", "balanced", "clean", "mono"],
       "Shows the Snarl Trajectory editor: a drawn body, peak and return contour locked to half a bar.",
       weight=66, growl=75, bite=45, motion=50, space=0,
       mid_level=90, mid_source_a="saw_stack", mid_source_b="folded_triangle", mid_blend=25,
       filter_cutoff_hz=600, filter_resonance=32,
       contour_depth=0,
       traj_enabled=True, traj_time_mode="sync", traj_length_div="1/2", traj_dest="cutoff", traj_depth=80, traj_curve=65,
       traj_loop=False, traj_x2=0.12, traj_x3=0.3, traj_x4=0.62,
       traj_y1=0.15, traj_y2=1.0, traj_y3=0.55, traj_y4=0.35, traj_y5=0.1,
       drive_amount=30, voice_mode="mono")

preset("Bounce Pluck", ["pluck", "contour", "balanced", "clean", "mono"],
       "Short plucked bass that bounces. A fast filter decay on every note makes it good for syncopated patterns.",
       weight=60, growl=70, bite=28, motion=35, space=0,
       velocity_to_level=30,
       sub_amp_decay_ms=380, sub_amp_sustain=15, sub_amp_release_ms=90,
       mid_level=90, mid_source_a="saw_stack", mid_amp_decay_ms=160, mid_amp_sustain=10, mid_amp_release_ms=100,
       filter_cutoff_hz=500, filter_resonance=26,
       contour_attack_ms=2, contour_decay_ms=140, contour_sustain=0, contour_shape="ease_out", contour_depth=70,
       drive_amount=20, voice_mode="mono")

preset("Splinter", ["rasp", "contour", "mid-forward", "clean", "mono"],
       "Bright splintered mid from the multiband fracture stage. Keep Weight lower to leave room for the sub.",
       weight=48, growl=88, bite=60, motion=40, space=0,
       mid_level=100, mid_source_a="saw_stack", mid_source_b="pulse_pair", mid_blend=30,
       filter_cutoff_hz=3000, filter_resonance=18,
       contour_decay_ms=240, contour_sustain=30, contour_depth=40,
       drive_amount=20,
       fracture_enabled=True, fracture_crossover_hz=1200, fracture_drive_low=10, fracture_drive_mid=60,
       fracture_drive_high=80, fracture_symmetry=70, fracture_band_mix=65, fracture_comp_db=-3, fracture_mix=70,
       oversampling_mode="x2", output_trim_db=-2, voice_mode="mono")

preset("Slide Weight", ["sub", "contour", "sub-forward", "clean", "legato"],
       "Weighty triangle sub with a long glide for slides between notes. The mid is kept low and soft.",
       weight=88, growl=35, bite=8, motion=10, space=0,
       voice_mode="mono", legato=True, glide_ms=180,
       sub_waveform="triangle", sub_level=100, mid_level=60, mid_source_a="pulse_pair",
       filter_cutoff_hz=500, filter_resonance=10,
       contour_depth=20, drive_amount=8)

preset("Velocity Rasp", ["rasp", "contour", "balanced", "clean", "mono"],
       "Dynamic bass. Harder notes get louder and a quicker attack, so velocity shapes the rasp.",
       weight=62, growl=70, bite=50, motion=30, space=0,
       voice_mode="mono", velocity_to_level=55, velocity_to_attack=25,
       mid_level=90, mid_source_a="saw_stack",
       filter_cutoff_hz=700, filter_resonance=30,
       mid_amp_attack_ms=8,
       contour_decay_ms=260, contour_sustain=25, contour_depth=65,
       drive_amount=30)


# --------------------------------------------------------------------------------------------------------
def file_name(index, p):
    return "%02d_%s.json" % (index + 1, slug(p["name"]))


def build():
    registry = read_registry()
    version = writer_state_version()
    ids = [p["id"] for p in PRESETS]
    names = [p["name"].lower() for p in PRESETS]
    assert len(set(ids)) == len(ids) and len(set(names)) == len(names), "duplicate preset id or name"
    assert len(PRESETS) >= 24
    files = {}
    for i, p in enumerate(PRESETS):
        files[file_name(i, p)] = serialise(registry, p, version)
    files["PROVENANCE.md"] = provenance(files)
    return files


def provenance(files):
    rows = []
    for i, p in enumerate(PRESETS):
        name = file_name(i, p)
        digest = hashlib.sha256(files[name].encode("utf-8")).hexdigest()
        rows.append("| %s | `%s` | %s | %s |" % (p["id"], name, p["name"], digest[:16]))
    return """# Factory preset provenance and license record

This file is generated by `tools/gen_presets.py`. Do not edit it by hand.

## Origin

- All %d factory presets are original parameter sets authored for Snaral by Circuit Drift Labs.
- Each file only contains Snaral parameter values and text metadata (name, author, description, tags).
  There are no samples, wavetables, recorded audio, scripts, paths or other embedded third-party assets.
- Names and descriptions describe the sound or intended use. No preset claims to recreate a named
  artist, track, sample or third-party preset.
- Oscillator, filter and effect algorithms are implemented in this repository. No external oscillator
  data is embedded, so there is no third-party data license to record.

## License

The presets are distributed with Snaral under the license of this repository. The repository does not
contain a license file yet, so the project owner must confirm the final license text before a public
release. Until then the presets are proprietary to Circuit Drift Labs and are not licensed for
redistribution outside the Snaral plug-in.

## Review status

Parameter sets were authored and schema-validated only. No listening review, level matching, peak/RMS
measurement or note/velocity matrix (notes 24, 36, 48, 60 at velocity 64 and 127) has been performed
yet, so the review table required by `docs/specs/factory-presets.md` is still empty and no loudness or
playability claim is made here.

## Files

The hash column is the first 16 hex digits of the SHA-256 of the file bytes (the parameter-state hash for
the future review table).

| Preset id | File | Name | SHA-256 (first 16) |
|---|---|---|---|
%s
""" % (len(PRESETS), "\n".join(rows))


def facets(values, registry):
    """Mirror of snaral::deriveFacets (Source/State/Pure/PresetFacets.h); used to keep tags honest."""
    v = values
    sub = v["weight"] / 100 * v["sub_level"] / 100
    mid = v["growl"] / 100 * v["mid_level"] / 100
    if sub >= 1.5 * mid and sub > 0.001:
        layer = "sub-forward"
    elif mid >= 1.5 * sub and mid > 0.001:
        layer = "mid-forward"
    else:
        layer = "balanced"
    macro = v["motion"] / 100
    motion = set()
    if (macro > 0 and v["contour_depth"] > 0) or (v["traj_enabled"] and v["traj_depth"] > 0):
        motion.add("contour")
    if macro > 0 and v["lfo_depth"] > 0:
        motion.add("synced" if v["lfo_mode"] == 1 else "free")
    if not motion:
        motion.add("static")
    texture = set()
    if v["noise_level"] > 0:
        texture.add("noise")
    if v["metal_level"] > 0:
        texture.add("metal")
    if not texture:
        texture.add("clean")
    return layer, motion, texture


def main():
    if "--facets" in sys.argv[1:]:
        registry = read_registry()
        for p in PRESETS:
            vals = resolve(registry, p["params"], p["name"])
            layer, motion, texture = facets(vals, registry)
            print("%-18s %-12s %-22s %-8s tags=%s" % (p["name"], layer, sorted(motion), sorted(texture), p["tags"]))
        return 0
    check = "--check" in sys.argv[1:]
    files = build()
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    stale = []
    for name, content in files.items():
        path = OUT_DIR / name
        if not path.exists() or path.read_text(encoding="utf-8") != content:
            stale.append(name)
            if not check:
                path.write_text(content, encoding="utf-8", newline="\n")
    for path in OUT_DIR.glob("*.json"):
        if re.match(r"^\d\d_.*\.json$", path.name) and path.name not in files:
            stale.append(path.name + " (obsolete)")
            if not check:
                path.unlink()
    if check:
        if stale:
            print("stale factory presets, run tools/gen_presets.py: " + ", ".join(stale))
            return 1
        print("factory presets up to date (%d)" % len(PRESETS))
        return 0
    print("wrote %d presets (%d changed)" % (len(PRESETS), len(stale)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
