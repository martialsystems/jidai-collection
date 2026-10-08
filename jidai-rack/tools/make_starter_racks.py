#!/usr/bin/env python3
# Copyright (c) 2026 Martial Systems LLC. All rights reserved.
# Writes racks/starter_racks.xml, the JIDAI RACK starter racks:
#   python3 tools/make_starter_racks.py <origami factory.xml> <shogun source dir> racks/starter_racks.xml
# (the SHOGUN source is the pinned one: <build>/_deps/shogun-src). Each starter rack is a complete rack state
# (JIDAIRACK version 3), the same XML the rack saves. Units are set by their own parameters (BUSHIDO PARAM ids, RONIN
# KNOB ids, ORIGAMI params from ORIGAMI's factory bank by name, SHOGUN parameter ids and pattern steps in SHOGUN's own
# patch JSON), never by a unit's preset name, and every connection is a cable on the jacks (the back shows them all).
# The presets test (JidaiPresetTests) loads every rack, checks each cable against the Jidai Cable Standard, and plays it.
import json
import re
import sys
import xml.etree.ElementTree as ET

# ---------------------------------------------------------------------------------------------------- units
BUSHIDO_PARAMS = ([f"{r}:{i}" for r in "ABC" for i in range(1, 13)] +
                  ["CH:PORTA A", "CH:PORTA B", "CH:RANGE A", "CH:RANGE B", "CH:C MODE", "CLOCK:TEMPO", "CLOCK:SOURCE",
                   "MODE:MODE", "MIXER:LEVEL 1", "MIXER:LEVEL 2", "CLOCK:DIV", "CLOCK:EXT SOURCE", "CLOCK:SETTLE",
                   "CLOCK:TRIG MODE", "STEPS:LAW A", "STEPS:LAW B", "STEPS:QUANT A", "STEPS:QUANT B", "MIDI:CH A",
                   "MIDI:CH B", "MIDI:VEL A", "MIDI:VEL B"])
BUSHIDO_DEFAULTS = {**{f"{r}:{i}": 0.5 for r in "ABC" for i in range(1, 13)},
                    "CH:PORTA A": 0, "CH:PORTA B": 0, "CH:RANGE A": 1, "CH:RANGE B": 1, "CH:C MODE": 0, "CLOCK:TEMPO": 0.5,
                    "CLOCK:SOURCE": 0, "MODE:MODE": 0.5, "MIXER:LEVEL 1": 0.7, "MIXER:LEVEL 2": 0.7, "CLOCK:DIV": 0.5,
                    "CLOCK:EXT SOURCE": 0, "CLOCK:SETTLE": 0, "CLOCK:TRIG MODE": 0, "STEPS:LAW A": 0, "STEPS:LAW B": 0,
                    "STEPS:QUANT A": 0, "STEPS:QUANT B": 0, "MIDI:CH A": 0, "MIDI:CH B": 1 / 15, "MIDI:VEL A": 0,
                    "MIDI:VEL B": 0}

# RONIN panel knobs in kPanelKnobs order with their defaults (RONIN Source/UI/PanelGeometry.inc).
RONIN_KNOBS = [("VCO:RANGE", 0.5), ("VCO:FINE", 0.5), ("VCO:PW", 0.5), ("VCO:FM 1", 0), ("VCO:FM 2", 0),
               ("VCF:CUTOFF", 0.45), ("VCF:PEAK", 0.2), ("VCF:MOD", 0.4), ("VCA 1:INITIAL", 0), ("VCA 1:MOD", 0.85),
               ("VCA 1:LOW CUT", 0), ("VCA 2:INITIAL", 0), ("VCA 2:MOD", 1), ("MG:RATE", 0.5), ("MG:PW", 0.5),
               ("EG 1:ATTACK", 0.2079), ("EG 1:DECAY", 0.39), ("EG 1:SUSTAIN", 0.6), ("EG 1:RELEASE", 0.39),
               ("EG 2:HOLD", 0.3), ("EG 2:DELAY", 0), ("EG 2:ATTACK", 0.2079), ("EG 2:RELEASE", 0.39),
               ("S&H:RATE", 0.5), ("DIV:RATIO SWITCH", 0), ("INT:TIME", 0.5), ("MIX:LEVEL 1", 0.8), ("MIX:LEVEL 2", 0.8),
               ("MIX:LEVEL 3", 0.8), ("EXT IN:THRESHOLD", 0.3758), ("EXT IN:RELEASE", 0.5316), ("OUTPUT:MIX", 1),
               ("OUTPUT:LEVEL", 0.7)]
RONIN_KNOB_IDS = {k for k, _ in RONIN_KNOBS}

# RONIN knob helpers. VCO RANGE: 0, 1/3, 2/3, 1 = 32', 16', 8', 4' (8' = C3 at 0 V). VCF CUTOFF: 20 Hz x 900^k.
# EG times (EG 1 all, EG 2 ATTACK/RELEASE): 1 ms x 60000^k. EG 2 HOLD/DELAY: 1 ms x 10^(4k).
import math
FOOT = {32: 0.0, 16: 1 / 3, 8: 2 / 3, 4: 1.0}
ACC = 0.5  # accent step value on BUSHIDO row C (C MODE CV) for the acid racks
def cutoff(hz): return round(math.log(hz / 20.0) / math.log(900.0), 4)
def eg(seconds): return round(math.log(seconds / 0.001) / math.log(60000.0), 4)
def hold(seconds): return round(math.log10(seconds / 0.001) / 4.0, 4)

# ---------------------------------------------------------------------------------------------------- ORIGAMI
ORIGAMI_IDS = []
ORIGAMI_PRESETS = {}
def load_origami(path):
    root = ET.parse(path).getroot()
    assert root.tag == "ORIGAMI_FACTORY"
    for p in root.findall("PRESET"):
        vals = {}
        for e in p.find("ORIGAMI").findall("PARAM"):
            vals[e.get("id")] = e.get("value")
            if e.get("id") not in ORIGAMI_IDS:
                ORIGAMI_IDS.append(e.get("id"))
        ORIGAMI_PRESETS[p.get("name")] = vals

def fmt(v):
    if isinstance(v, str):
        return v
    s = repr(float(v))
    return s[:-2] if s.endswith(".0") else s

def esc(s): return s.replace("&", "&amp;").replace('"', "&quot;").replace("<", "&lt;").replace(">", "&gt;")

# ---------------------------------------------------------------------------------------------------- SHOGUN
# Parameter ids, kinds, defaults and step counts from SHOGUN's own table (engine/params_table.h at the pinned commit).
SHOGUN_PARAMS = {}
SHOGUN_VOICES = ["BD1", "BD2", "SD", "RS", "CP", "CL", "MA", "CB", "CH", "OH", "CY", "LTC", "MTC", "HTC", "LEAD", "BASS"]
def load_shogun(src_dir):
    text = open(src_dir + "/engine/params_table.h").read()
    for m in re.finditer(r'\{"([^"]+)", ParamKind::(\w+), ([0-9.e+-]+)f, -?\d+, (\d+),', text):
        SHOGUN_PARAMS[m.group(1)] = (m.group(2), float(m.group(3)), int(m.group(4)))
    assert len(SHOGUN_PARAMS) > 300, len(SHOGUN_PARAMS)

def step_u(index, n):
    """A stepped SHOGUN control at choice index (the centre of its bin, SHOGUN's stepU)."""
    return round((index + 0.5) / n, 6)

def shogun_choice(pid, index):
    kind, _, steps = SHOGUN_PARAMS[pid]
    assert kind in ("Stepped", "Toggle") and 0 <= index < max(steps, 2), (pid, index)
    return float(index) if kind == "Toggle" else step_u(index, steps)

def drum_steps(text):
    """'x...X...' -> SHOGUN steps: x = on (accent 2), X = accent (3), . = off."""
    out = []
    for i, ch in enumerate(text):
        if ch in "xX":
            out.append({"i": i, "on": True, "acc": 3 if ch == "X" else 2, "prob": 1.0, "micro": 0.0, "flam": 0,
                        "ratchet": 1, "locks": {}})
    return out

def synth_steps(notes):
    """[(step, MIDI note, accent, tie)] -> SHOGUN synth steps."""
    return [{"i": i, "on": True, "acc": acc, "prob": 1.0, "micro": 0.0, "flam": 0, "ratchet": 1, "note": n,
             "tie": tie, "locks": {}} for i, n, acc, tie in notes]

# ---------------------------------------------------------------------------------------------------- devices
class Rack:
    def __init__(self, name, category, about, view="back"):
        self.name, self.category, self.about, self.view = name, category, about, view
        self.devices, self.cables, self.level = [], [], 1.0

    def bushido(self, number=1, name="", steps_a=None, steps_b=None, steps_c=None, **params):
        p = dict(BUSHIDO_DEFAULTS)
        # Host clock (JCS R5.7): runs with the DAW transport, one step per 16th unless DIV says otherwise.
        p.update({"CLOCK:SOURCE": 1, "CLOCK:EXT SOURCE": 1, "MODE:MODE": 0})
        for row, vals in (("A", steps_a), ("B", steps_b), ("C", steps_c)):
            if vals is not None:
                assert len(vals) == 12
                for i, v in enumerate(vals):
                    assert 0.0 <= v <= 1.0, (row, i, v)
                    p[f"{row}:{i + 1}"] = round(v, 6)
        for k, v in params.items():
            key = k.replace("__", ":").replace("_", " ")
            assert key in p, key
            p[key] = v
        self.devices.append(("BUSHIDO", number, name, p))
        return f"BUSHIDO#{number}"

    def ronin(self, number=1, name="", **knobs):
        k = dict(RONIN_KNOBS)
        for key, v in knobs.items():
            kid = key.replace("__", ":").replace("_", " ")
            kid = kid.replace("S H", "S&H")
            assert kid in RONIN_KNOB_IDS, kid
            k[kid] = v
        self.devices.append(("RONIN", number, name, k))
        return f"RONIN#{number}"

    def origami(self, preset, number=1, name="", **changes):
        vals = dict(ORIGAMI_PRESETS[preset])
        for key, v in changes.items():
            assert key in vals, key
            vals[key] = fmt(v)
        self.devices.append(("ORIGAMI", number, name, vals))
        return f"ORIGAMI#{number}"

    def shogun(self, number=1, name="", pattern="", params=None, tracks=None, running=0):
        p = {}
        for pid, v in (params or {}).items():
            assert pid in SHOGUN_PARAMS, pid
            assert 0.0 <= v <= 1.0, (pid, v)
            if abs(float(v) - SHOGUN_PARAMS[pid][1]) < 1e-9:
                continue   # sparse, as SHOGUN's patchToJson writes it: a default value is not stored
            p[pid] = round(float(v), 6)
        trs = []
        for vid, (length, steps) in (tracks or {}).items():
            assert vid in SHOGUN_VOICES, vid
            assert 1 <= length <= 32 and all(0 <= s["i"] < length for s in steps), vid
            trs.append({"id": vid, "len": length, "steps": steps})
        patch = {"format": "shogun-patch", "version": 2, "name": pattern, "params": p, "mod": [], "cables": [],
                 "cvAmt": {}, "inLaw": {}, "seq": {"pattern": pattern, "seed": 1513406686, "tracks": trs}}
        self.devices.append(("SHOGUN", number, name, (patch, running)))
        return f"SHOGUN#{number}"

    def cable(self, a, b):
        self.cables.append((a, b))

    def xml(self, indent="    "):
        out = [f'{indent}<JIDAIRACK version="3" browser="1" view="{self.view}" cablesFront="HIDE PASS-THRU" '
               f'cablesBack="ALL" scale="100">']
        out.append(f'{indent}  <DEVICE kind="RACK I/O" number="1" front="open" format="1" level="{fmt(self.level)}"/>')
        for kind, number, name, vals in self.devices:
            nm = f' name="{esc(name)}"' if name else ""
            if kind == "BUSHIDO":
                out.append(f'{indent}  <DEVICE kind="BUSHIDO" number="{number}"{nm} front="open" format="1" bypass="0" '
                           f'bank="-1" pattern="-1">')
                for pid in BUSHIDO_PARAMS:
                    out.append(f'{indent}    <PARAM id="{esc(pid)}" value="{fmt(vals[pid])}"/>')
            elif kind == "RONIN":
                out.append(f'{indent}  <DEVICE kind="RONIN" number="{number}"{nm} front="open" format="2" program="0" '
                           f'effect="1" triShape="triangle">')
                for kid, _ in RONIN_KNOBS:
                    out.append(f'{indent}    <KNOB id="{esc(kid)}" value="{fmt(vals[kid])}"/>')
            elif kind == "SHOGUN":
                patch, running = vals
                out.append(f'{indent}  <DEVICE kind="SHOGUN" number="{number}"{nm} front="open" format="2">')
                text = json.dumps(patch, separators=(",", ":"))
                out.append(f'{indent}    <SHOGUN version="2" running="{running}">{text.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")}</SHOGUN>')
            else:
                out.append(f'{indent}  <DEVICE kind="ORIGAMI" number="{number}"{nm} front="open" format="1">')
                out.append(f'{indent}    <ORIGAMI format="1" unit="ORIGAMI">')
                for pid in ORIGAMI_IDS:
                    out.append(f'{indent}      <PARAM id="{pid}" value="{vals[pid]}"/>')
                out.append(f'{indent}    </ORIGAMI>')
            out.append(f'{indent}  </DEVICE>')
        for age, (a, b) in enumerate(self.cables):
            out.append(f'{indent}  <CABLE a="{esc(a)}" b="{esc(b)}" age="{age}"/>')
        out.append(f'{indent}</JIDAIRACK>')
        return out


def semis(*notes, span=12):
    """Step knobs for a V/OCT row: semitones above the row's 0 V note, over RANGE (1 V = 12, 5 V = 60 semitones)."""
    return [n / span for n in notes]


def to_main(rack, src_l, src_r=None):
    rack.cable(src_l, "RACK#1/MAIN:OUT L")
    rack.cable(src_r or src_l, "RACK#1/MAIN:OUT R")


def ronin_voice(rack, r, pitch=None, gate=None, filter_cv=None, to_output=True):
    """RONIN as a mono voice: VCO SAW -> VCF -> VCA 1 -> OUTPUT WET, EG 1 opening VCA 1 and the filter."""
    rack.cable(f"{r}/VCO:SAW", f"{r}/VCF:IN")
    rack.cable(f"{r}/VCF:OUT", f"{r}/VCA 1:IN")
    if to_output:
        rack.cable(f"{r}/VCA 1:OUT", f"{r}/OUTPUT:WET")
    rack.cable(f"{r}/EG 1:OUT A", f"{r}/VCA 1:ENV")
    if filter_cv is None:
        rack.cable(f"{r}/EG 1:OUT A", f"{r}/VCF:CUTOFF")
    if pitch:
        rack.cable(pitch, f"{r}/VCO:V/OCT")
    if gate:
        rack.cable(gate, f"{r}/EG 1:TRIG")


# RONIN's ACID factory programs (RONIN docs/presets.md, programs 8, 9 and 12), knob values as stored there.
ACID_LINE = dict(VCO__RANGE=0.3333, VCF__CUTOFF=0.34, VCF__PEAK=0.85, VCF__MOD=0.72, VCA_1__MOD=0.64,
                 EG_1__ATTACK=0.063, EG_1__DECAY=0.5184, EG_1__SUSTAIN=0.8, EG_1__RELEASE=0.2723,
                 EG_2__HOLD=0, EG_2__ATTACK=0.063, EG_2__RELEASE=0.4351, INT__TIME=0.3029)
ACID_SQUELCH = dict(VCO__RANGE=0.3333, VCF__CUTOFF=0.28, VCF__PEAK=1.0, VCF__MOD=0.88, VCA_1__MOD=0.62,
                    EG_1__ATTACK=0.063, EG_1__DECAY=0.5649, EG_1__SUSTAIN=0.85, EG_1__RELEASE=0.3556,
                    EG_2__HOLD=0, EG_2__ATTACK=0.063, EG_2__RELEASE=0.5184, INT__TIME=0.3563)
ACID_DRIVE = dict(VCO__RANGE=0.3333, VCO__PW=0.35, VCF__CUTOFF=0.4, VCF__PEAK=0.92, VCF__MOD=0.75, VCA_1__MOD=0.6,
                  EG_1__ATTACK=0.063, EG_1__DECAY=0.5184, EG_1__SUSTAIN=0.8, EG_1__RELEASE=0.2723,
                  EG_2__HOLD=0, EG_2__ATTACK=0.063, EG_2__RELEASE=0.4351, INT__TIME=0.3029)


def acid_voice(rack, r, pitch, gate, accent=None, pulse=False):
    """RONIN as its ACID programs, driven by a sequencer the way RONIN documents it: pitch into INT IN (INT TIME is
    the slide) and INT OUT into VCO V/OCT, the gate into EG 1 TRIG (VCA) and EG 2 TRIG (filter snap on VCF CUTOFF),
    and the accent CV into VCF CUTOFF. The MG/DIV/MIX self-playing pattern is left unpatched."""
    rack.cable(f"{r}/VCO:SAW", f"{r}/VCF:IN")
    if pulse:
        rack.cable(f"{r}/VCO:PULSE", f"{r}/VCF:IN")
    rack.cable(f"{r}/VCF:OUT", f"{r}/VCA 1:IN")
    rack.cable(f"{r}/VCA 1:OUT", f"{r}/OUTPUT:WET")
    rack.cable(f"{r}/EG 1:OUT A", f"{r}/VCA 1:ENV")
    rack.cable(f"{r}/EG 2:OUT +", f"{r}/VCF:CUTOFF")
    rack.cable(pitch, f"{r}/INT:IN")
    rack.cable(f"{r}/INT:OUT", f"{r}/VCO:V/OCT")
    rack.cable(gate, f"{r}/EG 1:TRIG")
    rack.cable(gate, f"{r}/EG 2:TRIG")
    if accent:
        rack.cable(accent, f"{r}/VCF:CUTOFF")


# ---------------------------------------------------------------------------------------------------- the racks
def build():
    racks = []

    # 0. INIT: the empty rack, RACK I/O only (what a new JIDAI RACK opens with).
    racks.append(Rack("INIT", "INIT", "RACK I/O only: the empty rack.", view="front"))

    # 1. ACID LINE: RONIN's ACID LINE program played by BUSHIDO: row A pitch through INT (the slide), GATE A into
    #    EG 1 and EG 2, row C (C MODE CV) as the accent on VCF CUTOFF.
    r = Rack("Acid Line", "ACID",
             "BUSHIDO plays a 12-step acid line on the host clock (1/16) into RONIN's ACID LINE voice: row A's pitch "
             "goes through RONIN's INT, which slides between notes, GATE A fires EG 1 (VCA) and EG 2 (filter snap), "
             "and row C (C MODE CV) is the accent on VCF CUTOFF.")
    b = r.bushido(name="ACID SEQ",
                  steps_a=semis(0, 0, 12, 0, 3, 0, 7, 10, 0, 12, 5, 3),
                  steps_c=[ACC, 0, 0, 0.8 * ACC, 0, 0, ACC, 0, 0, 0.6 * ACC, 0, 0.9 * ACC],
                  CH__RANGE_A=0, STEPS__QUANT_A=1)
    v = r.ronin(name="ACID BASS", **ACID_LINE)
    acid_voice(r, v, pitch=f"{b}/OUTPUTS:CV A", gate=f"{b}/OUTPUTS:GATE A", accent=f"{b}/OUTPUTS:CV C")
    to_main(r, f"{v}/HOST:OUT L", f"{v}/HOST:OUT R")
    racks.append(r)

    # 2. ACID FOLD: RONIN's ACID SQUELCH program played by BUSHIDO (same wiring as Acid Line), its filter snap (EG 2)
    #    also folding harder through ORIGAMI's VC 1.
    r = Rack("Acid Fold", "ACID",
             "A 12-step acid line through ORIGAMI. RONIN's ACID SQUELCH voice: row A's pitch slides through INT, "
             "GATE A fires EG 1 and EG 2, row C (C MODE CV) accents VCF CUTOFF, and EG 2's filter snap also folds "
             "harder through ORIGAMI's VC 1.")
    b = r.bushido(name="ACID SEQ",
                  steps_a=semis(0, 12, 0, 0, 3, 12, 0, 7, 0, 10, 12, 3),
                  steps_c=[ACC, 0, 0, 0, 0.8 * ACC, 0, 0, 0, 0, ACC, 0, 0],
                  CH__RANGE_A=0, STEPS__QUANT_A=1)
    v = r.ronin(name="ACID BASS", **ACID_SQUELCH)
    o = r.origami("Acid Squelch Fold", name="ACID FOLD", vc1_src=0, vc1_amt=0.5)
    acid_voice(r, v, pitch=f"{b}/OUTPUTS:CV A", gate=f"{b}/OUTPUTS:GATE A", accent=f"{b}/OUTPUTS:CV C")
    r.cable(f"{v}/EG 2:OUT +", f"{o}/VC:VC 1")
    r.cable(f"{v}/HOST:OUT L", f"{o}/IN:IN L")
    r.cable(f"{v}/HOST:OUT R", f"{o}/IN:IN R")
    to_main(r, f"{o}/OUT:OUT L", f"{o}/OUT:OUT R")
    racks.append(r)

    # 3. DRIVING BASS: an 8-step 16th bass (TRIG 9 -> RESET), octave jumps, through ORIGAMI.
    r = Rack("Driving Bass", "EDM",
             "An 8-step rolling 16th bass, one bar of 4/4 on the host clock: TRIG 9 patched into RESET makes BUSHIDO's "
             "12 steps an 8-step loop. RONIN plays a short saw with a little resonance; ORIGAMI adds drive that keeps "
             "the low end clean.")
    b = r.bushido(name="BASS SEQ",
                  steps_a=semis(0, 0, 12, 0, 0, 12, 0, 10, 0, 0, 0, 0),
                  CH__RANGE_A=0, STEPS__QUANT_A=1)
    v = r.ronin(name="BASS", VCO__RANGE=FOOT[16], VCF__CUTOFF=cutoff(260), VCF__PEAK=0.35, VCF__MOD=0.5,
                EG_1__ATTACK=0.0, EG_1__DECAY=eg(0.12), EG_1__SUSTAIN=0.25, EG_1__RELEASE=eg(0.03), OUTPUT__LEVEL=0.7)
    o = r.origami("Sub-Safe Bass Drive", name="DRIVE")
    ronin_voice(r, v, pitch=f"{b}/OUTPUTS:CV A", gate=f"{b}/OUTPUTS:GATE A")
    r.cable(f"{b}/9:TRIG", f"{b}/INPUTS:RESET")
    r.cable(f"{v}/HOST:OUT L", f"{o}/IN:IN L")
    r.cable(f"{v}/HOST:OUT R", f"{o}/IN:IN R")
    to_main(r, f"{o}/OUT:OUT L", f"{o}/OUT:OUT R")
    racks.append(r)

    # 4. PLUCK LEAD: an 8-step lead over two octaves (RANGE 5 V), fast filter pluck, MG on the pulse width.
    r = Rack("Pluck Lead", "EDM",
             "An 8-step pluck lead on the host clock (TRIG 9 -> RESET). Row A spans RANGE 5 V, quantized to semitones; "
             "RONIN's pulse wave has its width moved by the MG, and a fast EG 1 plucks the filter. ORIGAMI adds bite.")
    b = r.bushido(name="LEAD SEQ",
                  steps_a=semis(12, 19, 24, 19, 15, 24, 22, 19, 0, 0, 0, 0, span=60),
                  CH__RANGE_A=1, STEPS__QUANT_A=1)
    v = r.ronin(name="PLUCK", VCO__RANGE=FOOT[8], VCF__CUTOFF=cutoff(600), VCF__PEAK=0.45, VCF__MOD=0.6,
                EG_1__ATTACK=0.0, EG_1__DECAY=eg(0.14), EG_1__SUSTAIN=0.0, EG_1__RELEASE=eg(0.12),
                MG__RATE=0.35, OUTPUT__LEVEL=0.6)
    o = r.origami("Pluck Edge", name="BITE")
    r.cable(f"{v}/VCO:PULSE", f"{v}/VCF:IN")
    r.cable(f"{v}/MG:TRI", f"{v}/VCO:PWM")
    r.cable(f"{v}/VCF:OUT", f"{v}/VCA 1:IN")
    r.cable(f"{v}/VCA 1:OUT", f"{v}/OUTPUT:WET")
    r.cable(f"{v}/EG 1:OUT A", f"{v}/VCA 1:ENV")
    r.cable(f"{v}/EG 1:OUT A", f"{v}/VCF:CUTOFF")
    r.cable(f"{b}/OUTPUTS:CV A", f"{v}/VCO:V/OCT")
    r.cable(f"{b}/OUTPUTS:GATE A", f"{v}/EG 1:TRIG")
    r.cable(f"{b}/9:TRIG", f"{b}/INPUTS:RESET")
    r.cable(f"{v}/HOST:OUT L", f"{o}/IN:IN L")
    r.cable(f"{v}/HOST:OUT R", f"{o}/IN:IN R")
    to_main(r, f"{o}/OUT:OUT L", f"{o}/OUT:OUT R")
    racks.append(r)

    # 5. TWO VOICES: one BUSHIDO, two RONINs. Row A is the bass (CV A), row C a counter line (C MODE CV, CV C).
    r = Rack("Two Voices", "EDM",
             "One BUSHIDO plays two RONINs: row A is the bass on CV A, row C a counter line on CV C (C MODE CV, "
             "0..5 V = five octaves). GATE A fires both envelopes. RONIN 1's MIX sums its own voice (IN 1) and RONIN 2 "
             "(IN 2) into its OUTPUT, and the pair plays through ORIGAMI.")
    b = r.bushido(name="SEQ",
                  steps_a=semis(0, 0, 0, 7, 0, 0, 3, 0, 0, 0, 10, 0),
                  steps_c=semis(24, 27, 31, 24, 34, 31, 27, 36, 24, 31, 29, 27, span=60),
                  CH__RANGE_A=0, STEPS__QUANT_A=1)
    v1 = r.ronin(1, name="BASS", VCO__RANGE=FOOT[16], VCF__CUTOFF=cutoff(220), VCF__PEAK=0.4, VCF__MOD=0.5,
                 EG_1__ATTACK=0.0, EG_1__DECAY=eg(0.15), EG_1__SUSTAIN=0.3, EG_1__RELEASE=eg(0.05), OUTPUT__LEVEL=0.6,
                 MIX__LEVEL_1=0.8, MIX__LEVEL_2=0.6)
    v2 = r.ronin(2, name="COUNTER", VCO__RANGE=FOOT[32], VCF__CUTOFF=cutoff(900), VCF__PEAK=0.3, VCF__MOD=0.45,
                 EG_1__ATTACK=0.0, EG_1__DECAY=eg(0.1), EG_1__SUSTAIN=0.0, EG_1__RELEASE=eg(0.1), OUTPUT__LEVEL=0.6)
    o = r.origami("Warm Bus Glue", name="GLUE")
    ronin_voice(r, v1, pitch=f"{b}/OUTPUTS:CV A", gate=f"{b}/OUTPUTS:GATE A", to_output=False)
    ronin_voice(r, v2, pitch=f"{b}/OUTPUTS:CV C", gate=f"{b}/OUTPUTS:GATE A")
    r.cable(f"{v1}/VCA 1:OUT", f"{v1}/MIX:IN 1")
    r.cable(f"{v2}/HOST:OUT L", f"{v1}/MIX:IN 2")
    r.cable(f"{v1}/MIX:OUT", f"{v1}/OUTPUT:WET")
    r.cable(f"{v1}/HOST:OUT L", f"{o}/IN:IN L")
    to_main(r, f"{o}/OUT:OUT L", f"{o}/OUT:OUT R")
    racks.append(r)

    # 6. STEPPED FOLD: BUSHIDO sequences ORIGAMI's fold as well as the notes (row C on VC 1, TRIG 5 on VC 3).
    r = Rack("Stepped Fold", "EDM",
             "BUSHIDO sequences the fold as well as the notes: row C (C MODE CV) drives ORIGAMI's VC 1, so every step "
             "has its own fold depth, and TRIG 5 kicks VC 3 once a loop. RONIN is a held saw drone gated by GATE A.")
    b = r.bushido(name="FOLD SEQ",
                  steps_a=semis(0, 0, 0, 0, 5, 5, 3, 3, 0, 0, 7, 7),
                  steps_c=[0.0, 0.3, 0.6, 1.0, 0.2, 0.5, 0.8, 0.4, 0.0, 0.7, 1.0, 0.5],
                  CH__RANGE_A=0, STEPS__QUANT_A=1, CH__PORTA_A=0.08)
    v = r.ronin(name="DRONE", VCO__RANGE=FOOT[16], VCF__CUTOFF=cutoff(700), VCF__PEAK=0.25, VCF__MOD=0.3,
                EG_1__ATTACK=eg(0.003), EG_1__DECAY=eg(0.3), EG_1__SUSTAIN=0.7, EG_1__RELEASE=eg(0.08),
                OUTPUT__LEVEL=0.55)
    o = r.origami("Stepped Fold Sequence", name="STEP FOLD")
    ronin_voice(r, v, pitch=f"{b}/OUTPUTS:CV A", gate=f"{b}/OUTPUTS:GATE A")
    r.cable(f"{b}/OUTPUTS:CV C", f"{o}/VC:VC 1")
    r.cable(f"{b}/5:TRIG", f"{o}/VC:VC 3")
    r.cable(f"{v}/HOST:OUT L", f"{o}/IN:IN L")
    r.cable(f"{v}/HOST:OUT R", f"{o}/IN:IN R")
    to_main(r, f"{o}/OUT:OUT L", f"{o}/OUT:OUT R")
    racks.append(r)

    # 7. KEYS THROUGH FOLD: RACK I/O MIDI plays RONIN (the DAW's notes), through ORIGAMI.
    r = Rack("MIDI Fold Synth", "EDM",
             "Play RONIN from your DAW: RACK I/O's MIDI NOTE, GATE and VEL jacks drive RONIN's VCO, EG 1 and filter, "
             "and RONIN plays through ORIGAMI. Send MIDI to the JIDAI RACK track.")
    v = r.ronin(name="SYNTH", VCO__RANGE=FOOT[8], VCF__CUTOFF=cutoff(500), VCF__PEAK=0.4, VCF__MOD=0.5,
                EG_1__ATTACK=eg(0.002), EG_1__DECAY=eg(0.4), EG_1__SUSTAIN=0.5, EG_1__RELEASE=eg(0.25),
                MIX__LEVEL_1=0.8, MIX__LEVEL_2=0.4, OUTPUT__LEVEL=0.6)
    o = r.origami("Lead Bite", name="BITE")
    ronin_voice(r, v, pitch="RACK#1/MIDI:NOTE", gate="RACK#1/MIDI:GATE", filter_cv="mix")
    r.cable(f"{v}/EG 1:OUT A", f"{v}/MIX:IN 1")
    r.cable("RACK#1/MIDI:VEL", f"{v}/MIX:IN 2")
    r.cable(f"{v}/MIX:OUT", f"{v}/VCF:CUTOFF")
    r.cable(f"{v}/HOST:OUT L", f"{o}/IN:IN L")
    r.cable(f"{v}/HOST:OUT R", f"{o}/IN:IN R")
    to_main(r, f"{o}/OUT:OUT L", f"{o}/OUT:OUT R")
    racks.append(r)

    # 8. ACID DRUM JAM: every unit. SHOGUN plays an EDM kit clocked from RACK I/O (CLK 1/16 into CLK IN, RESET into
    #    RST IN, CLOCK:SOURCE EXT); BUSHIDO plays a 12-step acid line on the host clock into RONIN's resonant filter;
    #    ORIGAMI folds the bass. Drums and bass meet at MAIN OUT, where the rack lines them up to the sample (a RET
    #    return would put the bass through SHOGUN's oversampling twice, 23 samples behind its own drums at 2x).
    r = Rack("Acid Drum Jam", "EDM",
             "Every unit: SHOGUN plays a four-on-the-floor kit clocked from RACK I/O (CLK 1/16 -> CLK IN, RESET -> RST IN). "
             "BUSHIDO plays a 12-step acid line into RONIN's ACID DRIVE voice (INT slides, row C accents), ORIGAMI "
             "folds it. Drums and bass meet at MAIN OUT, where the rack lines them up to the sample.")
    sg = r.shogun(name="KIT", pattern="ACID JAM", running=1,
                  params={"CLOCK:SOURCE": shogun_choice("CLOCK:SOURCE", 2), "CLOCK:CLK IN": shogun_choice("CLOCK:CLK IN", 0),
                          "MASTER:GLUE": 0.3, "CH:LEVEL": 0.42, "OH:LEVEL": 0.38, "CP:LEVEL": 0.5},
                  tracks={"BD1": (16, drum_steps("X...x...x...x...")),
                          "CP": (16, drum_steps("....x.......x...")),
                          "CH": (16, drum_steps("x.x.x.x.x.x.x.xX")),
                          "OH": (16, drum_steps("..x...x...x...x."))})
    b = r.bushido(name="ACID SEQ",
                  steps_a=semis(0, 0, 12, 0, 3, 0, 7, 10, 0, 12, 5, 3),
                  steps_c=[ACC, 0, 0, 0.8 * ACC, 0, 0, ACC, 0, 0, 0.6 * ACC, 0, 0.9 * ACC],
                  CH__RANGE_A=0, STEPS__QUANT_A=1)
    v = r.ronin(name="ACID BASS", OUTPUT__LEVEL=0.55, **ACID_DRIVE)
    o = r.origami("Acid Grit", name="GRIT")
    acid_voice(r, v, pitch=f"{b}/OUTPUTS:CV A", gate=f"{b}/OUTPUTS:GATE A", accent=f"{b}/OUTPUTS:CV C", pulse=True)
    r.cable("RACK#1/TRANSPORT:CLK 1/16", f"{sg}/CLOCK:CLK IN")
    r.cable("RACK#1/TRANSPORT:RESET", f"{sg}/CLOCK:RST IN")
    r.cable(f"{v}/HOST:OUT L", f"{o}/IN:IN L")
    to_main(r, f"{sg}/MIX:L", f"{sg}/MIX:R")
    to_main(r, f"{o}/OUT:OUT L", f"{o}/OUT:OUT R")
    racks.append(r)

    # 9. FULL EDM JAM: SHOGUN on the host clock with its full kit and its own LEAD synth; BUSHIDO's 8-step driving bass
    #    on RONIN goes to MAIN OUT (aligned there by the rack); ORIGAMI sits on SHOGUN's mix bus.
    r = Rack("Full EDM Jam", "EDM",
             "Every unit, host-clocked: SHOGUN plays kick, clap, hats, open hat and a tom fill plus a lead riff on its own "
             "LEAD synth. BUSHIDO's 8-step bass (TRIG 9 -> RESET) plays RONIN. SHOGUN's whole mix runs through ORIGAMI "
             "as a glue fold on the bus; the bass joins at MAIN OUT, where the rack lines it up with the drums.")
    sg = r.shogun(name="KIT", pattern="FULL JAM",
                  params={"CLOCK:SOURCE": shogun_choice("CLOCK:SOURCE", 0), "MASTER:GLUE": 0.35, "MASTER:DRIVE": 0.1,
                          "CH:LEVEL": 0.4, "OH:LEVEL": 0.36, "CP:LEVEL": 0.5, "LTC:LEVEL": 0.45,
                          "LEAD:CUTOFF": 0.62, "LEAD:RESO": 0.35, "LEAD:DECAY": 0.35, "LEAD:LEVEL": 0.36,
                          "LEAD:PAN": 0.6, "CH:PAN": 0.42},
                  tracks={"BD1": (16, drum_steps("X...x...x...x...")),
                          "CP": (16, drum_steps("....X.......x...")),
                          "CH": (16, drum_steps("x.x.x.x.x.x.x.x.")),
                          "OH": (16, drum_steps("..x...x...x...X.")),
                          "LTC": (32, drum_steps("..............................x.")),
                          "LEAD": (16, synth_steps([(0, 72, 3, False), (3, 75, 2, False), (6, 79, 2, False),
                                                    (8, 77, 3, False), (11, 75, 2, False), (14, 70, 2, False)]))})
    b = r.bushido(name="BASS SEQ",
                  steps_a=semis(0, 0, 12, 0, 0, 12, 0, 10, 0, 0, 0, 0),
                  CH__RANGE_A=0, STEPS__QUANT_A=1)
    v = r.ronin(name="BASS", VCO__RANGE=FOOT[16], VCF__CUTOFF=cutoff(260), VCF__PEAK=0.35, VCF__MOD=0.5,
                EG_1__ATTACK=0.0, EG_1__DECAY=eg(0.12), EG_1__SUSTAIN=0.25, EG_1__RELEASE=eg(0.03), OUTPUT__LEVEL=0.5)
    o = r.origami("Drum-Bus Glue", name="BUS GLUE")
    ronin_voice(r, v, pitch=f"{b}/OUTPUTS:CV A", gate=f"{b}/OUTPUTS:GATE A")
    r.cable(f"{b}/9:TRIG", f"{b}/INPUTS:RESET")
    r.cable(f"{sg}/MIX:L", f"{o}/IN:IN L")
    r.cable(f"{sg}/MIX:R", f"{o}/IN:IN R")
    to_main(r, f"{o}/OUT:OUT L", f"{o}/OUT:OUT R")
    to_main(r, f"{v}/HOST:OUT L", f"{v}/HOST:OUT R")
    racks.append(r)

    # 10. FILTER FOLD FX: host audio through RONIN's filter (MG sweep) and ORIGAMI.
    r = Rack("Filter Fold FX", "FX",
             "An insert effect for your track: host audio from RACK I/O goes through RONIN's resonant filter, swept by "
             "the MG, then through ORIGAMI, and back to the host.")
    v = r.ronin(name="FILTER", VCF__CUTOFF=cutoff(900), VCF__PEAK=0.55, VCF__MOD=0.45, VCA_1__INITIAL=1.0,
                MG__RATE=0.3, OUTPUT__MIX=1.0, OUTPUT__LEVEL=0.7)
    o = r.origami("Warm Bus Glue", name="GLUE")
    r.cable("RACK#1/HOST:IN L", f"{v}/HOST:IN L")
    r.cable("RACK#1/HOST:IN R", f"{v}/HOST:IN R")
    r.cable(f"{v}/EXT IN:MONO", f"{v}/VCF:IN")
    r.cable(f"{v}/VCF:OUT", f"{v}/VCA 1:IN")
    r.cable(f"{v}/VCA 1:OUT", f"{v}/OUTPUT:WET")
    r.cable(f"{v}/MG:TRI", f"{v}/VCF:CUTOFF")
    r.cable(f"{v}/HOST:OUT L", f"{o}/IN:IN L")
    r.cable(f"{v}/HOST:OUT R", f"{o}/IN:IN R")
    to_main(r, f"{o}/OUT:OUT L", f"{o}/OUT:OUT R")
    racks.append(r)

    # 11. TEMPO GATE FX: host audio chopped in time with the host (TRANSPORT CLK 1/16 fires EG 1 on VCA 1), folded.
    r = Rack("Tempo Gate FX", "FX",
             "A tempo-synced gate for your track: RACK I/O's TRANSPORT CLK 1/16 fires RONIN's EG 1 on every 16th, "
             "which opens VCA 1 on the host audio; ORIGAMI folds the chopped signal. Runs while the DAW plays.")
    v = r.ronin(name="GATE", VCF__CUTOFF=1.0, VCF__PEAK=0.0, VCF__MOD=0.0, VCA_1__INITIAL=0.0, VCA_1__MOD=1.0,
                EG_1__ATTACK=0.0, EG_1__DECAY=eg(0.09), EG_1__SUSTAIN=0.0, EG_1__RELEASE=eg(0.06),
                OUTPUT__MIX=1.0, OUTPUT__LEVEL=0.7)
    o = r.origami("Analog Edge", name="EDGE")
    r.cable("RACK#1/HOST:IN L", f"{v}/HOST:IN L")
    r.cable("RACK#1/HOST:IN R", f"{v}/HOST:IN R")
    r.cable(f"{v}/EXT IN:MONO", f"{v}/VCF:IN")
    r.cable(f"{v}/VCF:OUT", f"{v}/VCA 1:IN")
    r.cable(f"{v}/VCA 1:OUT", f"{v}/OUTPUT:WET")
    r.cable(f"{v}/EG 1:OUT A", f"{v}/VCA 1:ENV")
    r.cable("RACK#1/TRANSPORT:CLK 1/16", f"{v}/EG 1:TRIG")
    r.cable(f"{v}/HOST:OUT L", f"{o}/IN:IN L")
    r.cable(f"{v}/HOST:OUT R", f"{o}/IN:IN R")
    to_main(r, f"{o}/OUT:OUT L", f"{o}/OUT:OUT R")
    racks.append(r)
    return racks


def main():
    load_origami(sys.argv[1])
    load_shogun(sys.argv[2])
    racks = build()
    names = set()
    out = ['<?xml version="1.0" encoding="UTF-8"?>',
           '<!-- JIDAI RACK starter racks. Copyright (c) 2026 Martial Systems LLC. All rights reserved.',
           '     Written by tools/make_starter_racks.py. Each RACK holds one complete rack state (JIDAIRACK version 3). -->',
           '<JIDAI_STARTER_RACKS version="1">']
    for rk in racks:
        assert rk.name not in names
        names.add(rk.name)
        out.append(f'  <RACK name="{esc(rk.name)}" category="{rk.category}" about="{esc(rk.about)}">')
        out += rk.xml()
        out.append('  </RACK>')
    out.append('</JIDAI_STARTER_RACKS>')
    open(sys.argv[3], "w").write("\n".join(out) + "\n")
    print(len(racks), "racks")


if __name__ == "__main__":
    main()
