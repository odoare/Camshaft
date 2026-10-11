#!/usr/bin/env python3
"""
import_user_preset.py

Turns a Camshaft user preset into a factory preset (Source/Assets/*.xml).

A user preset made on one of the samples bundled with Camshaft (see
Source/Shell/FactorySamples.h) carries that sample embedded in it. The factory
version names the sample instead (SourceOptions factory="<id>"), so it stays a
few kilobytes: the embedded audio is recognised by its file name and replaced
by the sample's ID. A preset made on the built-in morph is copied as it is; one
made on any other audio is refused (factory presets cannot embed audio).

The player's own things are left out: the MIDI learn map and the Kick-start
locks (the processor keeps the player's when a preset brings none).

    python3 tools/import_user_preset.py "<user preset>.xml" [--name "Idle - Name"]

The output file is named after the preset name ("Idle - Name" ->
Source/Assets/Idle_Name.xml), like the generated factory presets. Rebuild to
embed it.
"""

import argparse
import os
import sys
import xml.etree.ElementTree as ET

HERE = os.path.dirname (os.path.abspath (__file__))
OUT = os.path.join (HERE, "..", "Source", "Assets")

# The bundled samples: file name -> ID (Source/Shell/FactorySamples.h).
SAMPLES = { "Camshaft.wav": "camshaft", "absurde.wav": "absurde" }

def main():
    ap = argparse.ArgumentParser (description = "Turn a Camshaft user preset into a factory preset.")
    ap.add_argument ("preset")
    ap.add_argument ("--name", help = "factory preset name, 'Category - Name' (default: the preset's own)")
    args = ap.parse_args()

    tree = ET.parse (args.preset)
    root = tree.getroot()
    name = args.name or root.get ("presetName") or os.path.splitext (os.path.basename (args.preset))[0]
    root.set ("presetName", name)
    root.attrib.pop ("presetIsFactory", None)

    # The audio: a bundled sample becomes a reference, the morph stays.
    container = root.find ("EmbeddedAudio")
    options = root.find ("SourceOptions")
    if container is not None:
        audio = container.find ("Audio")
        file_name = audio.get ("name") if audio is not None else None
        if file_name not in SAMPLES:
            sys.exit ("'%s' embeds %s, which is not a bundled sample (%s): factory presets cannot embed audio."
                      % (args.preset, file_name, ", ".join (SAMPLES)))
        root.remove (container)
        if options is None:
            options = ET.SubElement (root, "SourceOptions")
        options.set ("factory", SAMPLES[file_name])
        print ("audio: %s -> bundled sample '%s'" % (file_name, SAMPLES[file_name]))
    else:
        if options is not None:
            options.attrib.pop ("factory", None)
        print ("audio: the built-in morph")

    # The player's MIDI learn map and locks are not part of a patch.
    structure = root.find ("Structure")
    if structure is not None:
        for section in ("MidiLearn", "Locks"):
            node = structure.find (section)
            if node is not None:
                for child in list (node):
                    node.remove (child)

    ET.indent (root, "  ")
    out = os.path.join (OUT, name.replace (" - ", "_").replace (" ", "_") + ".xml")
    tree.write (out, encoding = "UTF-8", xml_declaration = True)
    print ("wrote", os.path.relpath (out), "(%d bytes)" % os.path.getsize (out))

if __name__ == "__main__":
    main()
