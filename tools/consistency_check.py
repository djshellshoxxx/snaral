#!/usr/bin/env python3
"""Cross-checks every parameter ID across registry, presets, help, UI layout, MIDI-learn rules, randomizer windows and
the ID baseline. Exits non-zero on any mismatch. Usage: tools/consistency_check.py [repo_root]"""
import json, os, re, sys

root = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
errors = []


def fail(msg):
    errors.append(msg)


def read(rel):
    with open(os.path.join(root, rel), encoding="utf-8") as f:
        return f.read()


# ---------------------------------------------------------------- registry ----------------------------------------
reg = {}   # id -> dict(type, min, max, default, flags, group, choices, unit)
order = []
groups_dir = os.path.join(root, "Source/Parameters/Groups")
list_def = read("Source/Parameters/ParameterList.def")
included = re.findall(r'#include\s+"Groups/([^"]+)"', list_def)
on_disk = sorted(f for f in os.listdir(groups_dir) if f.endswith(".def"))
for f in on_disk:
    if f not in included:
        fail(f"registry: Groups/{f} exists but is not included by ParameterList.def")
for f in included:
    if f not in on_disk:
        fail(f"registry: ParameterList.def includes missing Groups/{f}")


def num(s):
    return float(s.strip().rstrip("fF"))


def split_args(s):
    out, cur, depth, q = [], "", 0, False
    for ch in s:
        if ch == '"':
            q = not q
        if not q and ch in "([":
            depth += 1
        if not q and ch in ")]":
            depth -= 1
        if not q and depth == 0 and ch == ",":
            out.append(cur.strip()); cur = ""
        else:
            cur += ch
    if cur.strip():
        out.append(cur.strip())
    return out


for f in included:
    text = re.sub(r"//[^\n]*", "", read(f"Source/Parameters/Groups/{f}"))
    for m in re.finditer(r"SNARAL_(FLOAT|BOOL|CHOICE)\s*\((.*?)\)\s*(?=SNARAL_|$)", text, re.S):
        kind, body = m.group(1), m.group(2)
        a = split_args(body)
        pid = a[0]
        if pid in reg:
            fail(f"registry: duplicate id {pid}")
        if not re.fullmatch(r"[a-z][a-z0-9_]*", pid):
            fail(f"registry: bad id spelling {pid}")
        d = {"kind": kind, "group": a[2].strip('"'), "name": a[1].strip('"'), "file": f}
        if kind == "FLOAT":
            d.update(min=num(a[3]), max=num(a[4]), default=num(a[5]), unit=a[6].strip('"'), scale=a[7], flags=a[8])
            if not (d["min"] < d["max"]):
                fail(f"registry: {pid} min >= max")
            if not (d["min"] <= d["default"] <= d["max"]):
                fail(f"registry: {pid} default outside range")
            if d["scale"] == "Log" and d["min"] <= 0:
                fail(f"registry: {pid} log scale with min <= 0")
        elif kind == "BOOL":
            d.update(min=0, max=1, default=1.0 if a[3] == "true" else 0.0, flags=a[4], choices=[])
        else:
            ch = [c.strip('"') for c in a[5:]]
            d.update(default=float(a[3]), flags=a[4], choices=ch, min=0, max=len(ch) - 1)
            if not (0 <= d["default"] <= len(ch) - 1):
                fail(f"registry: {pid} default choice index out of range")
            if len(set(ch)) != len(ch):
                fail(f"registry: {pid} duplicate choice names")
            if len(ch) > 24:
                fail(f"registry: {pid} more than maxChoices (24)")
        d.setdefault("flags", "pfNone")
        reg[pid] = d
        order.append(pid)

if not reg:
    fail("registry: parsed zero parameters (script out of sync with the .def format)")


def has_flag(pid, flag):
    return flag in reg[pid]["flags"]


preset_ids = [p for p in order if not has_flag(p, "pfNoPreset")]
auto_ids = [p for p in order if not has_flag(p, "pfNoAutomation")]
UTILITY = {p for p in order if reg[p]["group"] == "Utility"}
MACROS = {p for p in order if reg[p]["group"] == "Macros"}
TRAJ_POINTS = {p for p in order if re.fullmatch(r"traj_[xy]\d", p)}

# ---------------------------------------------------------------- baseline ----------------------------------------
base_path = os.path.join(root, "tests/fixtures/param_ids_baseline.txt")
if os.path.exists(base_path):
    base = [l.strip() for l in open(base_path) if l.strip() and not l.startswith("#")]
    if order[:len(base)] != base:
        fail("baseline: registry order is not an append-only extension of tests/fixtures/param_ids_baseline.txt")
else:
    fail("baseline: tests/fixtures/param_ids_baseline.txt is missing")

# ---------------------------------------------------------------- presets -----------------------------------------
pdir = os.path.join(root, "Resources/presets")
for fn in sorted(os.listdir(pdir)):
    if not fn.endswith(".json"):
        continue
    try:
        doc = json.load(open(os.path.join(pdir, fn), encoding="utf-8"))
    except Exception as e:
        fail(f"preset {fn}: not valid JSON ({e})"); continue
    params = doc.get("parameters", {})
    for pid in params:
        if pid not in reg:
            fail(f"preset {fn}: unknown parameter {pid}")
        elif has_flag(pid, "pfNoPreset"):
            fail(f"preset {fn}: contains non-preset parameter {pid}")
    for pid in preset_ids:
        if pid not in params:
            fail(f"preset {fn}: missing parameter {pid}")
    for pid, v in params.items():
        d = reg.get(pid)
        if not d:
            continue
        if d["kind"] == "FLOAT":
            if isinstance(v, bool) or not isinstance(v, (int, float)) or not (d["min"] <= v <= d["max"]):
                fail(f"preset {fn}: {pid}={v!r} outside [{d['min']}, {d['max']}]")
            elif "pfInteger" in d["flags"] and v != round(v):
                fail(f"preset {fn}: {pid}={v!r} must be whole")
        elif d["kind"] == "BOOL":
            if not isinstance(v, bool):
                fail(f"preset {fn}: {pid} must be boolean")
        else:
            if v not in d["choices"]:
                fail(f"preset {fn}: {pid}={v!r} not one of {d['choices']}")
    nm = doc.get("preset", {}).get("name", "")
    if not nm:
        fail(f"preset {fn}: empty name")

# ---------------------------------------------------------------- help --------------------------------------------
help_src = read("Source/UI/HelpContent.cpp")
purposes = re.findall(r'\{\s*"([a-z0-9_]+)"\s*,\s*"', help_src.split("const Purpose purposes[]")[1].split("};")[0])
seen = set()
for pid in purposes:
    if pid in seen:
        fail(f"help: duplicate purpose entry {pid}")
    seen.add(pid)
    if pid not in reg:
        fail(f"help: purpose entry for unknown parameter {pid}")
for pid in order:
    if pid not in seen and pid not in TRAJ_POINTS:
        fail(f"help: no purpose sentence for parameter {pid}")

# ---------------------------------------------------------------- UI layout ---------------------------------------
tabs_src = read("Source/UI/AdvancedTabs.cpp")
sec_block = tabs_src.split("static const std::vector<AdvancedSectionSpec> specs = {")[1].split("};")[0]
ui_ids = re.findall(r'"([a-z][a-z0-9_]*)"', re.sub(r'\{\s*T::\w+,\s*"[^"]*",', "{", sec_block))
cnt = {}
for pid in ui_ids:
    cnt[pid] = cnt.get(pid, 0) + 1
    if pid not in reg:
        fail(f"ui: AdvancedTabs references unknown parameter {pid}")
for pid, c in cnt.items():
    if c > 1:
        fail(f"ui: DUPLICATE-UI {pid} appears {c} times in AdvancedTabs")
main_view = MACROS | UTILITY
for pid in order:
    if pid in TRAJ_POINTS or pid in main_view:
        if pid in cnt and pid in TRAJ_POINTS:
            fail(f"ui: {pid} is edited by the trajectory widget but also listed in AdvancedTabs")
        continue
    if pid not in cnt:
        fail(f"ui: NO-UI {pid} has no control in AdvancedTabs")
tab_groups = set(re.findall(r'"([A-Za-z]+)"', tabs_src.split("advancedGroupsForTab")[1].split("isEditedByDedicatedWidget")[0]))
for g in {reg[p]["group"] for p in order} - tab_groups - {"Macros", "Utility"}:
    fail(f"ui: group {g} is not covered by advancedGroupsForTab")

# ---------------------------------------------------------------- MIDI learn ---------------------------------------
ml_src = read("Source/Core/MidiLearn.cpp")
sw = ml_src.split("switch (paramIndex)")[1].split("default:")[0]
excl = set(re.findall(r"P::([a-z0-9_]+)", sw))
spec_excl = {"master_tune_hz", "limiter_enabled", "limiter_ceiling_db", "bypass", "sub_mute", "mid_mute"}
if excl != spec_excl:
    fail(f"midi-learn: exclusion list {sorted(excl)} differs from spec {sorted(spec_excl)}")
for pid in excl:
    if pid not in reg:
        fail(f"midi-learn: exclusion references unknown parameter {pid}")
learnable = [p for p in order if reg[p]["kind"] != "CHOICE" and p in auto_ids and "pfInteger" not in reg[p]["flags"] and p not in spec_excl]
for must in ("weight", "growl", "bite", "motion", "space", "output_trim_db", "legato", "anchor_enabled", "anchor_strength", "traj_enabled", "traj_depth"):
    if must not in learnable:
        fail(f"midi-learn: spec says {must} is learnable but the registry rules exclude it")

# ---------------------------------------------------------------- randomizer ---------------------------------------
rz = read("Source/Core/Randomizer.cpp")
ctor = rz.split("RuleTable()")[1].split("Intersect windows")[0]
rand_ids = set(re.findall(r"P::([a-z0-9_]+)", ctor))
never = {"output_trim_db", "limiter_enabled", "limiter_ceiling_db", "master_tune_hz", "transpose_semitones", "pitch_bend_range",
         "voice_mode", "velocity_to_level", "velocity_to_attack", "sub_tune_cents", "anchor_enabled", "anchor_note",
         "anchor_strength", "oversampling_mode"} | UTILITY
for pid in rand_ids:
    if pid not in reg:
        fail(f"randomizer: rule for unknown parameter {pid}")
    if pid in never:
        fail(f"randomizer: {pid} must never be randomized (spec)")
for pid in order:
    if pid not in rand_ids and pid not in never:
        fail(f"randomizer: {pid} is neither randomized nor on the spec never-randomize list")
for m in re.finditer(r"\b(uni|logUni)\s*\(\s*P::(\w+)\s*,\s*\w+\s*,\s*(-?[\d.]+)f?\s*,\s*(-?[\d.]+)f?\s*\)", ctor):
    pid, lo, hi = m.group(2), float(m.group(3)), float(m.group(4))
    d = reg.get(pid)
    if d and d["kind"] == "FLOAT" and not (d["min"] <= lo <= hi <= d["max"]):
        fail(f"randomizer: window [{lo}, {hi}] for {pid} is outside the registry range [{d['min']}, {d['max']}]")

# ---------------------------------------------------------------- stray id literals --------------------------------
prefixes = sorted({re.match(r"[a-z]+", p).group(0) + "_" for p in order if "_" in p})
lit = re.compile(r'"((?:%s)[a-z0-9_]*)"' % "|".join(prefixes))
exempt = re.compile(r"(_ms|_hz|_db)$")
for sub in ("Source/UI", "Source/Core", "Source/State", "Source/Parameters"):
    for dp, _, fns in os.walk(os.path.join(root, sub)):
        for fn in fns:
            if not fn.endswith((".cpp", ".h")):
                continue
            rel = os.path.relpath(os.path.join(dp, fn), root)
            for ln, line in enumerate(open(os.path.join(dp, fn), encoding="utf-8", errors="replace"), 1):
                code = line.split("//")[0]
                for m in lit.finditer(code):
                    tok = m.group(1)
                    if tok not in reg and not tok.endswith("_") and not re.search(r"[?*]", tok):
                        fail(f"{rel}:{ln}: string literal \"{tok}\" looks like a parameter id but is not in the registry")

# ---------------------------------------------------------------- factory preset coverage -------------------------
enable_ids = [p for p in order if reg[p]["kind"] == "BOOL" and p.endswith("_enabled") and p in preset_ids]
choice_used = {p: set() for p in preset_ids if reg[p]["kind"] == "CHOICE"}
on_used = {p: False for p in enable_ids}
for fn in sorted(os.listdir(pdir)):
    if not fn.endswith(".json"):
        continue
    params = json.load(open(os.path.join(pdir, fn), encoding="utf-8")).get("parameters", {})
    for p in enable_ids:
        on_used[p] |= params.get(p) is True
    for p in choice_used:
        if p in params:
            choice_used[p].add(params[p])
for p, used in on_used.items():
    if p in ("limiter_enabled",):
        continue
    if not used:
        fail(f"presets: no factory preset switches {p} on")
warnings = []
for p, used in choice_used.items():
    missing = [c for c in reg[p]["choices"] if c not in used]
    # division lists are not meant to be exhaustive; other gaps are reported as warnings (audit: "where musically sensible")
    if missing and p not in ("oversampling_mode", "voice_mode", "lfo_sync_division", "traj_length_div"):
        warnings.append(f"presets: choice {p} never uses {missing} in the factory set")

print(f"consistency_check: {len(order)} parameters, {len(preset_ids)} preset, {len(auto_ids)} automatable, "
      f"{len(learnable)} learnable, {len(rand_ids)} randomized")
for w in warnings:
    print("WARN:", w)
if errors:
    for e in errors:
        print("FAIL:", e)
    print(f"consistency_check: {len(errors)} problem(s)")
    sys.exit(1)
print("consistency_check: OK")
