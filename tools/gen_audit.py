#!/usr/bin/env python3
"""Generate the per-parameter table of docs/release/AUDIT.md (audit-checklist steps 1 to 5).

Inputs: Source/Parameters/Groups/*.def (registry), docs/release/WIRING.md (DSP consumers), Source/UI/AdvancedTabs.cpp (tab and
section of every control), Resources/presets/*.json (factory coverage) and the output of the perturbation harness
(`build-tests/snaral_wiringperturbationtests > file`). Prints markdown to stdout.

Usage: tools/gen_audit.py <perturbation-output.txt>
"""
import json
import re
import sys
from pathlib import Path

root = Path(__file__).resolve().parent.parent
decl = re.compile(r'^\s*SNARAL_(FLOAT|CHOICE|BOOL|INT)\w*\s*\(\s*(\w+)\s*,\s*"([^"]*)"\s*,\s*"([^"]*)"\s*,(.*)$')

SPEC = {
    "macros": "macro-mapping, macros-ui", "voice": "midi-voice", "sub": "sub-mid-engine", "mid": "oscillator-bank",
    "filter_motion": "motion-filter", "drive_texture": "processing-output", "output": "processing-output",
    "utility": "cdl-common-actions, parameter-state", "fx_furnace": "expansion-effect-formant-furnace",
    "fx_fracture": "expansion-effect-harmonic-fracture", "trajectory": "expansion-snarl-trajectory",
    "quality": "expansion-oversampling", "sub_anchor": "expansion-sub-anchor",
}
TAB = {"Voice": "Voice", "Sub": "Sub", "Sources": "Sources", "FilterMotion": "Filter & Motion", "Drive": "Drive",
       "Texture": "Texture", "Effects": "Effects", "Output": "Output"}

params = []  # id, group file, flags text
for f in sorted((root / "Source/Parameters/Groups").glob("*.def")):
    for line in f.read_text().splitlines():
        m = decl.match(line)
        if m:
            params.append((m.group(2), f.stem, m.group(5)))

consumers = {}
for line in (root / "docs/release/WIRING.md").read_text().splitlines():
    c = [x.strip() for x in line.split("|")]
    if len(c) > 6 and c[1].startswith("`"):
        consumers[c[1].strip("`")] = c[4].replace("`", "")

ui = {}
src = (root / "Source/UI/AdvancedTabs.cpp").read_text()
for m in re.finditer(r'\{\s*T::(\w+),\s*"([^"]+)",\s*\{([^}]*)\}', src):
    for pid in re.findall(r'"(\w+)"', m.group(3)):
        ui[pid] = f"{TAB[m.group(1)]} tab: {m.group(2)}"
for pid in ("weight", "growl", "bite", "motion", "space"):
    ui[pid] = "Main: macro knob"
ui["bypass"] = "Main action row: Bypass"
for pid in ("layer_solo", "sub_mute", "mid_mute"):
    ui[pid] = "Advanced header: solo/mute"
for pid in ("traj_x2", "traj_x3", "traj_x4", "traj_y1", "traj_y2", "traj_y3", "traj_y4", "traj_y5"):
    ui[pid] = "Filter & Motion tab: trajectory editor point"

presets = [json.loads(p.read_text())["parameters"] for p in sorted((root / "Resources/presets").glob("*.json"))]
defaults = {}
txt = ""
for f in sorted((root / "Source/Parameters/Groups").glob("*.def")):
    txt += f.read_text() + "\n"

perturb = {}
for line in Path(sys.argv[1]).read_text().splitlines():
    m = re.match(r"^(\w+)\s+(WIRED|UNWIRED)\s+diff\s+(-?[\d.]+) dB", line)
    if m:
        perturb[m.group(1)] = (m.group(2), float(m.group(3)))

def flags_of(rest):
    return ("pfNoPreset" in rest, "pfNoAutomation" in rest)

print("| Feature (parameter id) | Spec | Code location (DSP consumer) | Wired | Notes |")
print("|---|---|---|---|---|")
for pid, grp, rest in params:
    nopreset, noauto = flags_of(rest)
    vals = [p.get(pid) for p in presets if pid in p]
    nondefault = len({json.dumps(v) for v in vals})
    used = f"{len(vals)}/27 serialised, {nondefault} distinct values" if not nopreset else "not in presets (project state)"
    state, d = perturb.get(pid, ("UNVERIFIED", 0.0))
    wired = "Y" if state == "WIRED" else "N"
    auto = "host-automatable" if not noauto else "state only (not automatable)"
    notes = f"{ui.get(pid, 'NO-UI')}; {used}; {auto}; perturbation {d:.1f} dB vs -90 dB limit"
    print(f"| `{pid}` | {SPEC[grp]} | {consumers.get(pid, 'NONE')} | {wired} | {notes} |")
