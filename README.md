# Camshaft

A range wavetable synth, by FX-Mechanics.

Import an audio file, set up to four ranges on it, and play each range as a
wavetable at the pitch of the note: the whole range is one cycle of the wave,
whatever its length, so a slice of a voice, a drum loop or a field recording
becomes an oscillator. Each range (a "cam") has its own unison, two filters,
amp envelope, mix and effect sends; sixteen modulators (LFOs, envelopes, drawn
curves) and the MIDI sources can drive any knob, including where each range
sits in the file, per voice; the cams can modulate each other (FM, ring, AM, hard sync, and opening
modulation, which moves a cam's read position at audio rate);
two effect chains and a master chain reuse the FxmeFX effects.

The audio is stored inside the session and the presets, so a patch never
depends on a file path. Without a file, a built-in morph of waveforms plays.

See [doc/architecture.md](doc/architecture.md) for how it works.

## Using it

- **Audio.** Import a file (or drop it on the waveform). On the waveform, drag
  inside a cam's range (or its numbered flag) to move it, drag an edge to
  move that edge alone; while notes play, the bars along the bottom show the
  range each voice reads. The wheel zooms, a drag on empty space scrolls, a
  double-click shows the whole file.
- **Cams.** Under each cam's scope row (its wave drawn as a cam profile, and
  its output), four pages: the cam itself (opening and duration (the range's
  start and length), overlap (loop
  crossfade), snap, tuning, cylinders (unison)), its filters, its amp envelope,
  and its mix (volume, pan, the two Turbo sends).
- **Modulation.** Drag a modulator (or a MIDI source) from the bottom band onto
  any knob, or right-click the knob and choose "Modulate from". The knob's outer
  ring shows the modulation, the dot its live value. The Matrix tab lists every
  route. Slots are off until given a type (dragging one onto a knob makes it
  an LFO); a slot that modulates nothing is dimmed and not computed. While you
  hold a point of a curve, the waveform marks where it puts the ranges it
  modulates.
- **Belt & Viz.** The timing belt: drag from one cam's pulley to another to
  make the first modulate the second; click a belt's badge to switch FM,
  ring, AM or off. Beside it, the dashboard: output level, spectrum and
  stereo image, warning lights, free voices and DSP load, the voice mode (P
  M L) and a hazard button that silences every voice.
- **Effects.** Turbo 1 and 2 are fed by the cams' sends, the Exhaust takes the
  whole mix, the rev limiter (a safety limiter at -0.3 dBFS) is always last.
- **Presets** (the "Garage", top bar): the arrows step through them, "..."
  opens the browser to save, rename and delete. The dice ("Kick-start")
  randomize a section, or everything from the top bar; right-click a knob to
  lock it.
- Right-click any knob to type a value or learn a MIDI controller.

Camshaft is an instrument: put it on an instrument (MIDI) track. If nothing
sounds, the cam scope rows stay flat and the profiles do not turn: no notes are
arriving.

## Installing

Download the archive for your system from the releases page and copy the
plugin to your plugin folder:

| System | VST3 | AU |
|---|---|---|
| Linux | `~/.vst3/` | |
| Windows | `C:\Program Files\Common Files\VST3\` | |
| macOS | `/Library/Audio/Plug-Ins/VST3/` | `/Library/Audio/Plug-Ins/Components/` |

Then rescan the plugins in your host (most hosts cache the plugins they have
seen).

### macOS

Camshaft is free software and is not signed with an Apple Developer ID. macOS
marks anything downloaded through a browser as untrusted, and the host then
skips the plugin **silently** during its scan. Remove the quarantine flag for
what you installed, in Terminal:

```sh
xattr -dr com.apple.quarantine /Library/Audio/Plug-Ins/VST3/Camshaft.vst3
xattr -dr com.apple.quarantine /Library/Audio/Plug-Ins/Components/Camshaft.component
```

then rescan. An unsigned `.pkg` opens with right-click, Open. The builds are
universal (Apple Silicon and Intel) and support macOS 10.13 and later.

## Building

JUCE is expected as a sibling directory of this repository (`../JUCE`).
FxmeTools comes inside the FxmeFX submodule and carries WDL as its own nested
submodule, so clone recursively:

```sh
git clone --recurse-submodules <this repository>
# or, in an existing clone:
git submodule update --init --recursive

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target Camshaft_VST3 -j2
```

Builds do not install: copy `build/Camshaft_artefacts/Release/VST3/Camshaft.vst3`
to your VST3 folder and rescan.

The sound engine builds and tests without JUCE:

```sh
cmake --build build --target CamshaftEngineTest -j2 && ./build/CamshaftEngineTest
```

## Licence

Same terms as FxmeTools: the JUCE-free engine (`Source/Engine`) is under the
LGPL 3.0 or later; the plugin around it under the AGPL 3.0 or later, or
commercial terms from the author. See [LICENSE.md](LICENSE.md).
