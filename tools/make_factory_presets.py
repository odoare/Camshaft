#!/usr/bin/env python3
"""
make_factory_presets.py

Writes Camshaft's factory presets into Source/Assets/*.xml.

Each preset lists only the parameters that differ from their defaults, plus
its structural state (modulation routes, timing belt, curves). Loading a
preset first puts every parameter back to its default (the processor's
PresetManager::onBeforeLoad), so a partial file loads the same whatever was
playing before. No audio is embedded: factory presets play the built-in
source (Source/Engine/DemoSource.h), whose 64 cycles of 2048 samples morph
through eight waveforms, eight cycles each:

    0.00 sine, 0.125 triangle, 0.25 saw, 0.375 square, 0.50 pulse,
    0.625 "ah", 0.75 metallic, 0.875 resonant saw

A cam's range is set by its opening (r<n>_start, 0 to 1 of the recording) and
its duration (r<n>_length, samples); mid (position, length) gives the opening
of a range centred on a position of the built-in source.

A modulator slot is off by default; preset() makes every slot a preset uses
(a route from it, or any of its parameters set) an LFO unless its type is
given.

Effect slot types are indices into EffectFactory::types() plus one (0 = Off):
    1 Delay, 2 Tube, 3 EQ, 4 Oct, 5 Comp, 6 Limit, 7 Transient, 8 Cab,
    9 Reverb, 10 Chorus, 11 Flanger, 12 Phaser, 13 Freeze

Categories follow the engine theme: Idle (pads, drones), Torque (basses),
Rev (leads), Backfire (percussive and noisy).

    python3 tools/make_factory_presets.py        (from the repository root)
"""

import os
import xml.etree.ElementTree as ET

HERE = os.path.dirname (os.path.abspath (__file__))
OUT = os.path.join (HERE, "..", "Source", "Assets")

DEMO_SAMPLES = 64 * 2048

def mid (centre, length=2048):
    """Opening of a `length`-sample range centred on `centre` (0 to 1) of the
    built-in source, kept inside it."""
    start = min (max (0.0, centre * DEMO_SAMPLES - 0.5 * length), DEMO_SAMPLES - length)
    return start / DEMO_SAMPLES

def preset (name, params, routes=(), belt=(), curves=()):
    params = dict (params)
    slot = lambda key: int (key[3:]) if key.startswith ("mod") and key[3:].isdigit() else None
    used = { slot (pid.split ("_")[0]) for pid in params } | { slot (src) for src, *rest in routes }
    used.discard (None)
    for m in used:
        params.setdefault ("mod%d_type" % m, 0)   # LFO
    root = ET.Element ("Parameters", { "presetName": name, "schemaVersion": "2" })
    for pid, value in params.items():
        ET.SubElement (root, "PARAM", { "id": pid, "value": repr (float (value)) })
    st = ET.SubElement (root, "Structure")
    r = ET.SubElement (st, "Routes")
    for i, (src, dst, depth, *via) in enumerate (routes):
        ET.SubElement (r, "Route", { "i": str (i), "src": src, "dst": dst, "via": via[0] if via else "none" })
        ET.SubElement (root, "PARAM", { "id": "route%d_depth" % (i + 1), "value": repr (float (depth)) })
    b = ET.SubElement (st, "Belt")
    for src, dst, kind in belt:
        ET.SubElement (b, "Link", { "src": str (src), "dst": str (dst), "type": kind })
    c = ET.SubElement (st, "Curves")
    for mod, points, extra in curves:
        attrs = { "mod": str (mod), "points": ";".join ("%g,%g,%g" % p for p in points) }
        attrs.update ({ k: str (v) for k, v in extra.items() })
        ET.SubElement (c, "Curve", attrs)
    ET.indent (root, "  ")
    file = os.path.join (OUT, name.replace (" - ", "_").replace (" ", "_") + ".xml")
    ET.ElementTree (root).write (file, encoding="UTF-8", xml_declaration=True)
    print ("wrote", os.path.relpath (file))

# ---------------------------------------------------------------------------
preset ("Init", {})

preset ("Idle - Glass Drift",
    {
        "r1_start": mid (0.30), "r1_length": 2048, "r1_cyl": 4, "r1_detune": 18, "r1_spread": 0.8,
        "r1_att": 900, "r1_dec": 1500, "r1_sus": 0.9, "r1_rel": 2600, "r1_volume": -9,
        "r1_f1_on": 1, "r1_f1_type": 1, "r1_f1_cutoff": 3500, "r1_f1_res": 0.2,
        "r1_send1": 0.35,
        "r2_audible": 1, "r2_start": mid (0.72), "r2_length": 2048, "r2_coarse": 12, "r2_volume": -15,
        "r2_cyl": 2, "r2_detune": 9, "r2_att": 1500, "r2_rel": 3000, "r2_send1": 0.4,
        "mod1_rate": 0.15, "mod2_rate": 0.07, "mod2_mode": 1, "mod2_shape": 6,
        "aux1_fx0_type": 9, "aux1_return": -4,
    },
    routes = [ ("mod1", "r1_start", 0.06), ("mod2", "r2_start", 0.08) ])

preset ("Idle - Breathing Choir",
    {
        "r1_start": mid (0.68), "r1_cyl": 3, "r1_detune": 14, "r1_spread": 0.7,
        "r1_att": 600, "r1_rel": 2000, "r1_volume": -8,
        "r1_f1_on": 1, "r1_f1_type": 9, "r1_f1_vowela": 0, "r1_f1_vowelb": 3, "r1_f1_res": 0.35,
        "mod3_type": 2, "mod3_dur": 4, "r1_send1": 0.3,
        "aux1_fx0_type": 10, "aux1_fx1_type": 9, "aux1_return": -3,
    },
    routes = [ ("mod3", "r1_f1_vowelpos", 1.0) ],
    curves = [ (2, [ (0, 0, -0.4), (0.3, 1, 0.3), (0.6, 0.35, 0), (1, 0.8, 0) ],
                { "sustain": 1, "sStart": 1, "sEnd": 3, "loop": 1, "release": 0 }) ])

preset ("Torque - Saw Sub",
    {
        "polymode": 2, "glide_time": 60, "glide_mode": 1,
        "r1_start": mid (0.31), "r1_att": 2, "r1_dec": 400, "r1_sus": 0.6, "r1_rel": 150, "r1_volume": -7,
        "r1_f1_on": 1, "r1_f1_type": 1, "r1_f1_cutoff": 700, "r1_f1_res": 0.35,
        "r1_f1_envamt": 0.45, "r1_f1_keytrk": 0.5,
        "r2_audible": 1, "r2_start": mid (0.05), "r2_coarse": -12, "r2_volume": -8,
        "r2_att": 2, "r2_rel": 150,
    })

preset ("Torque - Ring Growl",
    {
        "r1_start": mid (0.42), "r1_volume": -7, "r1_att": 3, "r1_dec": 600, "r1_sus": 0.7, "r1_rel": 200,
        "r1_f1_on": 1, "r1_f1_type": 0, "r1_f1_cutoff": 1500, "r1_f1_res": 0.4, "r1_f1_envamt": 0.3,
        "r2_start": mid (0.05), "r2_coarse": 7, "r1_xm2_lift": 0.7,
        "mod1_rate": 0.4,
        "master_fx0_type": 2,
    },
    routes = [ ("mod1", "r1_xm2_lift", 0.25), ("modwheel", "r1_f1_cutoff", 0.4) ],
    belt = [ (1, 0, "Ring") ])

preset ("Rev - Formant Lead",
    {
        "polymode": 2, "glide_time": 80,
        "r1_start": mid (0.66), "r1_cyl": 2, "r1_detune": 8, "r1_volume": -7,
        "r1_att": 10, "r1_rel": 300,
        "r1_f1_on": 1, "r1_f1_type": 9, "r1_f1_vowela": 0, "r1_f1_vowelb": 3, "r1_f1_res": 0.45,
        "mod1_rate": 0.8, "mod1_delay": 300, "mod1_fade": 500,
        "r1_send2": 0.3, "aux2_fx0_type": 1, "aux2_return": -6,
    },
    routes = [ ("mod1", "r1_f1_vowelpos", 0.5), ("modwheel", "r1_fine", 0.08) ])

preset ("Rev - Sync Sweep",
    {
        "r1_start": mid (0.90, 1024), "r1_length": 1024, "r1_volume": -8, "r1_att": 5, "r1_rel": 250,
        "r1_cyl": 3, "r1_detune": 10,
        "mod1_type": 1, "mod1_att": 5, "mod1_dec": 900, "mod1_sus": 0.2, "mod1_rel": 300,
        "r1_send2": 0.25, "aux2_fx0_type": 1, "aux2_return": -8,
    },
    routes = [ ("mod1", "r1_length", -0.25), ("velocity", "r1_start", -0.1) ])

preset ("Backfire - Metal Hit",
    {
        "r1_start": mid (0.81), "r1_att": 0, "r1_dec": 350, "r1_sus": 0, "r1_rel": 300,
        "r1_dcurve": -0.8, "r1_volume": -6,
        "r1_f1_on": 1, "r1_f1_type": 2, "r1_f1_cutoff": 300,
        "r2_start": mid (0.05), "r2_coarse": 19, "r1_xm2_lift": 0.6,
        "mod2_type": 1, "mod2_att": 0, "mod2_dec": 200, "mod2_sus": 0, "mod2_rel": 200,
        "r1_send1": 0.25, "aux1_fx0_type": 9,
    },
    routes = [ ("mod2", "r1_xm2_lift", 0.4) ],
    belt = [ (1, 0, "FM") ])

preset ("Backfire - Gravel Pluck",
    {
        "r1_start": mid (0.50, 600), "r1_length": 600, "r1_snap": 1, "r1_att": 0, "r1_dec": 250, "r1_sus": 0,
        "r1_rel": 200, "r1_volume": -7,
        "r1_f1_on": 1, "r1_f1_type": 4, "r1_f1_cutoff": 2200, "r1_f1_res": 0.5, "r1_f1_envamt": 0.6,
        "r1_xm1_lift": 0.35,
        "master_fx0_type": 7, "master_fx1_type": 1,
    },
    routes = [ ("random", "r1_start", 0.2), ("keytrack", "r1_length", -0.3) ],
    belt = [ (0, 0, "FM") ])
