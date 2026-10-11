# Camshaft: architecture

Status (2026-10-09): first implementation of the whole v1 plan
(`doc/Range wavetable synth - development plan.md`), written in one pass.
The JUCE-free parts (FxmeTools core additions and the engine) are built and
tested; the plugin itself has passed a CMake configure and a per-file syntax
check, but has **not been built or run yet** (see `todo.md`, item 1).

Camshaft is a polyphonic synth: the user imports an audio file, four cams each
take a range of it (its opening and duration: where it starts and how long it
lasts) as one cycle of a wave and play
it as a band-limited wavetable at the note's pitch. Each cam has its own
cylinders (unison), two filters, amp envelope, mix and sends; 16 modulators and
the MIDI sources drive any parameter through a 64-route matrix; cams modulate
each other on the timing belt (FM, ring, AM, sync, opening); two aux effect chains and a master
chain reuse FxmeFX's effects, and a look-ahead rev limiter ends the chain.

## Layout and what lives where

```
lib/FxmeFX/                        submodule; FxmeTools is nested in it
  lib/FxmeTools/core/FxmeTools/    JUCE-free: synth/ (new) and dsp/ additions
  lib/FxmeTools/FxmeTools/         JUCE module: components/ additions
Source/
  Engine/        JUCE-free engine, built against FxmeCore only (LGPL)
  Shell/         plugin-side state: parameters, structure, audio, MIDI learn, randomize
  Effects/       FxmeFX effect slots and chains (after MechanOdd's)
  GUI/           editor components
  PluginProcessor.*, PluginEditor.*, Theme.h, Tooltips.h, AppSettings.h, ParamIDs.h
  Assets/        icon, splash, logo, factory presets
tests/EngineTest.cpp   the engine alone (target CamshaftEngineTest, not built by default)
tools/make_factory_presets.py
```

The plan put the voice and engine in FxmeTools' `core/synth`. They are
JUCE-free, as planned, but stay in `Source/Engine`: the Camshaft voice (four
cams, its routing, its parameter set) is not reusable by another plugin, while
every generic piece it is built from went into FxmeTools.

### Added to FxmeTools (shared, see `lib/FxmeFX/lib/FxmeTools/doc/api-changes.md`)

| Header | Half | Role in Camshaft |
|---|---|---|
| `synth/MipmappedBuffer.h/.cpp` | core | the source as an octave pyramid; `RangeLoop`: reading a range as a loop |
| `synth/SourceAnalysis.h/.cpp` | core | zero crossings and period track, for snapping |
| `synth/WavetableReader.h` | core | linear / cubic / sinc interpolation (shared by the pyramid) |
| `synth/WavetableSet.h`, `WavetableBuilder.h/.cpp` | core | the cam profile display (a table from the unmodulated range) |
| `synth/WavetableCache.h/.cpp` | core | no longer used by Camshaft (first design; see "Reading the ranges") |
| `synth/CurveAdsr.h` | core | amp envelopes and ADSR modulators |
| `synth/ModulationLfo.h` | core | LFO modulators |
| `synth/BreakpointCurve.h` | core | curve modulators (shape, lock-free hand-over, playhead) |
| `synth/VoiceAllocator.h` | core | poly / mono / legato, stealing, note stack, sustain pedal |
| `synth/ModMatrix.h` | core | route arithmetic, `RouteSet` (atomic routes) |
| `synth/ModGraph.h` | core | timing belt: cycle refusal, processing order |
| `dsp/MultiModeFilter.h` | core | the cams' filters (SVF types and formant) |
| `dsp/LookaheadLimiter.h` | core | the rev limiter |
| `dsp/StereoTap.h` | core | output and per-cam scope taps |
| `dsp/MinMaxPyramid.h` | core | waveform overview |
| `components/Vectorscope.h` | module | master view |
| `components/GearButton.h` | module | settings button (Dede and MechanOdd keep local copies) |
| `components/BreakpointCurveEditor.h` | module | curve modulator editor |

Tests: `core/tests/CoreSynthTests.cpp` (58 checks, all pass).

## Signal flow

Per voice, per cam (in the timing belt's topological order, sample by sample):

```
range read (cylinders, FM as a phase offset) -> DC blocker -> ring / AM from other cams
  -> filter 1, filter 2 (series, or parallel with a balance)
  -> amp envelope x velocity -> volume -> pan -> dry bus
taps (pre-filter = osc x env, post-filter = filtered x env, post-gain), panned,
  x send level -> Turbo 1 / Turbo 2 buses
```

Then, in the processor: Turbo chains (2 FxmeFX slots each), their returns (level
and pan) into the mix, Exhaust (2 slots), output level, rev limiter (look-ahead
1 ms, ceiling -0.3 dBFS, always in the path so the latency stays constant; "off"
raises its ceiling out of reach), output, then the output tap and peak meter.

A cam that is not audible but drives another through the belt is still computed
(`EngineState::camNeeded`), only not heard. Cams neither audible nor driving
anything cost nothing but their envelope.

"Dry mute" on a Turbo silences the dry signal of every cam sending to it.

## Reading the ranges

**No precomputed tables.** The plan's design (a band-limited wavetable per range
position, rebuilt when a range moves, with a per-voice cache) was implemented
first and dropped after testing (2026-10-09): a moving range became a series of
crossfades between tables cut from different places, and for real audio two
such tables are different waveforms whose crossfade partly cancels. Faster
fades, finer quantisation and phase alignment reduced the glitches but could not
remove them. The voice now reads its range straight from the source.

The source (`Engine/EngineSource.h`, made on the loader thread):

- `fxme::MipmappedBuffer`: the recording as an octave pyramid (each level the
  previous one low-passed by a 63-tap windowed sinc and decimated by two).
- `fxme::SourceAnalysis`: every rising zero crossing and a period track (one
  period estimate every 2048 samples), for snapping.

Per cylinder, per sample (`fxme::RangeLoop`): the cycle is
`[start, start + cycle)` of the source, read at `start + phase x cycle`, the
`overlap` samples after it crossfaded into its start (live, so the wrap is
continuous); with no overlap a polyBLEP band-limits the step at the wrap. The
pyramid level follows the read speed (`cycle x frequency / sampleRate` source
samples per output sample), fractional and blended between two octaves, with the
same half-octave bias as before (the top of the band may fold back above 0.29 fs
at worst). A DC blocker (8 Hz) follows, since a range of real audio rarely
averages to zero.

Moving ranges: each control block the voice computes its cam's range from the
(modulated) opening, duration, overlap and snap (`cam::camRange`: the range
starts at the opening and lasts the duration, cut short at the end of the
recording, at least 16 samples; the editor's displays use the same function),
smooths it with a one-pole of 4 ms
(this absorbs stepped automation and the jumps of a snapped range), and ramps
start, cycle and overlap per sample towards it. A moving range is therefore a
continuous scan of the source. Per-voice modulation of the range costs nothing
extra: there is no cache, no worker and no build latency. A new note starts on
its range directly; a new recording too (no glide across files).

Snap: zero crossing moves both edges to the nearest rising zero crossings
(within a quarter of the duration, at most 2048 samples); period moves the
opening to the nearest rising zero crossing and makes the duration a whole
number of the periods detected in the middle of the range. Both are lookups in
the precomputed analysis.

The engine publishes every voice's range as it reads it (start, cycle, overlap,
phase, per cam: `Display::ranges`), so the waveform shows what each voice plays,
modulation, snapping and smoothing included.

The source reaches the audio thread through an atomic pointer with a hazard
pointer (`CamEngine::setSource` / `beginBlock`): a replaced source is freed only
once the audio thread has announced it uses the new one.

The FxmeTools wavetable classes (`WavetableBuilder`, `WavetableCache`...) stay
in the library and are still used for the cam profile display (built on the
message thread from the unmodulated range).

## Modulation

Control rate: every 32 samples (`cam::controlBlock`); gains, pans, sends, lifts
and oscillator increments are ramped linearly over the block; filter settings
change once per block.

Sources (`cam::ModSource`): the 16 slots, velocity, release velocity, key track,
aftertouch (channel and poly), pitch bend, mod wheel, 4 assignable CCs (CC 20 to
23 by default), the 4 cams' amp envelopes, a per-voice random value.

Slots: LFO (per voice or global free-running, locked to the host while it
plays: its phase is the host position, taken at the end of each control span
like every modulator value, and kept exact whatever the host's block size), ADSR (per voice), curve (per voice, or global when "stick to DAW time":
its time comes from the host's PPQ, from the song's start, and
`CurvePlayer::setElapsed` turns it into a position as for a note held that long:
the start once, then the sustain region forever; the whole curve repeats only
without a sustain region), or Off (the default: an empty slot).
Each has an output level.

Only the slots that feed something are computed (`CamEngine::updateUsage`, each
block): a slot is used when a route from it (or through it, as the via) reaches
a cam or a global destination, or reaches another slot that is itself used. The
others output 0, their curves do not hold a voice in release, and their chips
are drawn dimmed. A slot that starts being used mid-note starts from its
note-on state (the LFO from its start phase, the envelope from its attack).
Routing from an Off slot makes it an LFO (`StructureState::setRoute`).

Destinations: one flat index space (`CamParams.h`): cam fields and modulator
fields marked `modulatable` in the spec tables (applied by the engine, per
voice), then every other continuous parameter (output, aux returns and pans,
glide, every effect parameter), "global destinations", applied by the shell once
per block by writing the parameter's raw value (the value the DSP reads),
recomputed each block from the parameter's own value so automation and the GUI
are never fought (MechanOdd's method). A per-voice source routed to a global
destination uses the newest voice.

A route adds `depth x source (x via)` to its destination's normalised value; the
engine converts back with the very spec the host parameter was built from.
Modulator-to-modulator routes are evaluated in slot order (a route to a lower
slot is one control block late).

Route depths are host parameters (`route<n>_depth`); sources, destinations and
vias are structural (below). Routes reach the engine through `fxme::RouteSet`
(atomics), curves through `fxme::SharedCurveShape`, the belt through atomics.

## Voices

24 voices allocated (polyphony setting 1 to 24), up to 4 cylinders per cam
(decided: 24 voices, 4 cylinders). `fxme::VoiceAllocator` decides; a stolen
voice fades out over 5 ms before restarting. Mono retriggers envelopes from
their current level, legato only moves the pitch. Glide: time in ms or a tempo
division, always or legato only, from the last note played. A voice ends when
every cam's amp envelope is idle and no curve release region is still playing.

Silent cams: per voice, a cam that cannot be heard (its amp envelope finished,
or its volume off with no send that could carry it) and modulates no other cam
is skipped, its ramps moving on (`CamVoice::skipCam`).

Rendering order: per control block, one cam at a time over the whole span, in
the timing belt's order (a cam's first-cylinder output is complete, in
`outBuf`, before the cams it modulates read it; self FM is computed inside the
first cylinder's loop from its previous sample), one cylinder at a time inside
it, the loop compiled per interpolation and per mono / stereo source. Stereo
reads share their position arithmetic (`MipmappedBuffer::readPair`).

Cost (best of alternating runs, -O3, a laptop i7-10810U whose clock varies with
its heat): 8 voices x 4 cams x 4 cylinders with both filters, 21 to 23 % of one
core on a stereo file; the full load (24 voices), 63 to 70 %. The cylinder
reads are 70 % of that, a third of each read being the second octave of the
blend; control-rate updates about 9 %. See todo 10 for what is left.

## Cross-modulation (timing belt)

Per pair src -> dst, from the source's first cylinder (decided):

- FM: a phase offset of dst's read, index up to 8 rad at full lift.
- Ring `x (1 - lift + lift x src)` and AM `x (1 + lift x src)`: the same
  sidebands, ring removing dst's own pitch as the lift rises, AM keeping it.
- Sync: each time src's cycle begins, dst's cylinders restart theirs from the
  sub-sample moment it began (src records its cycle starts per sample,
  `Cam::syncBuf`, before dst is rendered). The jump is band-limited with a
  polyBLEP, its first half added on the sample before from a look-ahead
  (missed only across a span boundary). The lift raises dst's pitch by up to
  three octaves while synced: the classic sync sweep.
- Opening: dst's read position moves by `lift x src x cycle` samples, at
  audio rate (outside the range's smoothing).
 Feedback FM on itself only. Lift is the
destination cam's `xm<src>_lift` parameter (automatable, modulatable). Cycles are
refused in the editor (`StructureState::setXmod`) and, if one ever reached the
engine, all links but feedback would be dropped.

## State

The APVTS state root ("Parameters") carries:

- the host parameters (`PARAM` children);
- `<Structure>`: routes, belt, curves, CC numbers, MIDI learn map, randomize
  locks, and the sample player's markers (reserved in schema 1);
- `<EmbeddedAudio>` (fxme::EmbeddedAudio: the original file, deflated float WAV
  in Base64; the plan said FLAC, which EmbeddedAudio dropped because it clamps
  peaks above full scale) and `<SourceOptions>` (channels, normalise, original
  rate, and `factory`: a bundled sample's ID). The source is the embedded
  audio if there is some, else the bundled sample named by `factory`
  (`Shell/FactorySamples.h`, the WAV files of `Source/Assets` in the binary
  data, read from memory, never embedded), else the built-in morph. A state
  naming a sample the build lacks plays the morph and says so;
- `schemaVersion` (1). `StructureState::migrate` is where upgrades go. Unknown
  children and parameters are left in place.

Loading a state: parameters it does not mention take their defaults. Loading a
preset: every parameter goes to its default first (factory presets list only
what differs), the MIDI learn map and the locks are kept when the preset has
none, and the audio can be kept ("Keep audio when loading presets", per
instance).

Parameter IDs: `<section><index>_<name>` (`ParamIDs.h`, `CamParams.h`), frozen
once released. About 1300 host parameters, grouped per cam, modulator, routes,
voices and output, and per effect chain. Most are the effect slots: every slot
holds every FxmeFX effect, as in MechanOdd.

## Threads

- Audio: `processBlock` (engine render, effects, limiter). No locks, no
  allocation once prepared.
- Source loader (`SourceManager::Loader`): imports a file (embeds it into a
  detached tree, handed to the state on the message thread when done), decodes
  the embedded audio from a detached copy of the state, applies the options,
  hands the source to the engine and the editor. A failed decode keeps the
  current audio and reports why.
- Effect loader (as in MechanOdd): IR loading for Cab / Reverb slots.
- Message thread: editor, structure edits, MIDI learn assignments (fed from the
  audio thread through a FIFO), presets.

## GUI

Header (`fxme::TopBar`): name, icon (clickable: about box), a small master view
(tachometer and vectorscope), the global Kick-start, the Garage (preset bar and
browser), the gear (settings callout) and help. Then two tabs:

- Top bar: the output level as a horizontal stereo meter (`OutputMeter`,
  fxme::StereoLevelMeter).
- Synth: file band (import / export / built-in, channels, normalise, waveform
  view with the four ranges, every voice's live range and read position, the
  curve probe, zoom, drag and drop), scope rows (cam
  profile turning in polar form with a follower, oscilloscope), cam strips
  (pages Cam, Filters, Amp, Mix), bottom band: Belt & Viz first (the timing
  belt on the left half, the dashboard on the right: tachometer, circular
  spectrum, vectorscope, warning lights, fuel and temperature gauges, gear
  selector, rev limiter switch, hazard button, throttle pedal for the output;
  `Dashboard.h` lists them), Modulators (draggable chips and per-type
  editors), Matrix, then the effect chains, one pane each (`ChainPane`): Turbo
  1 and 2 (return level, pan, dry mute) and the Exhaust (output level, rev
  limiter), each over its two slots, every slot with its effect selector and
  module preset bar over the effect's controls in their compact layout.
  Dice in the section headers. There are no top tabs: the editor is this
  one page.

Every knob is a `CamKnob`: right-click menu (type a value, MIDI learn, modulate
from a source, remove modulations, lock from Kick-start, reset), modulation ring
and live value, drop target for source chips, badges for MIDI and lock.

Tooltips: `Tooltips.h`, behind the "?" switch (machine-wide, `AppSettings.h`).

## Presets

`fxme::PresetManager` on `FX-Mechanics/Camshaft/Presets`, factory presets from
the XML files of `Source/Assets` (made by `tools/make_factory_presets.py`, or
from a user preset by `tools/import_user_preset.py`, which replaces a bundled
sample's embedded copy by its ID; categories Idle, Torque, Rev, Backfire after
the engine theme), the browser from the top bar.
Effect module presets in `FX-Mechanics/Modules/<Effect>/Presets`, shared with
FxmeFX and MechanOdd. Recommendations followed (merged into the plan): see its
"Presets" section.

## Building

```sh
git submodule update --init --recursive      # FxmeFX, FxmeTools inside it, WDL inside that
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target Camshaft_VST3 -j2
```

The JUCE-free engine test: `cmake --build build --target CamshaftEngineTest -j2`
then `./build/CamshaftEngineTest`. The FxmeTools core tests:
`cmake -S lib/FxmeFX/lib/FxmeTools/core -B build-core && cmake --build build-core -j2 && ctest --test-dir build-core`.
