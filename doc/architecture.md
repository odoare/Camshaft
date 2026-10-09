# Camshaft: architecture

Status (2026-10-09): first implementation of the whole v1 plan
(`doc/Range wavetable synth - development plan.md`), written in one pass.
The JUCE-free parts (FxmeTools core additions and the engine) are built and
tested; the plugin itself has passed a CMake configure and a per-file syntax
check, but has **not been built or run yet** (see `todo.md`, item 1).

Camshaft is a polyphonic synth: the user imports an audio file, four cams each
take a range of it (centre and lobe, its width) as one cycle of a wave and play
it as a band-limited wavetable at the note's pitch. Each cam has its own
cylinders (unison), two filters, amp envelope, mix and sends; 16 modulators and
the MIDI sources drive any parameter through a 64-route matrix; cams modulate
each other on the timing belt (FM, ring, AM); two aux effect chains and a master
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
| `synth/WavetableSet.h` | core | 2048-sample table, 11 mip levels (one per octave), 1 or 2 channels |
| `synth/WavetableBuilder.h/.cpp` | core | range of the source to a table set: snap, overlap, resample, FFT mipmaps |
| `synth/WavetableCache.h/.cpp` | core | per-cam pool of built sets; worker thread; lock-free reads |
| `synth/WavetableReader.h` | core | linear / cubic / sinc reads |
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
table read (cylinders, FM as a phase offset) -> ring / AM from other cams
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

## Wavetables and moving ranges

The source is kept at its original sample rate: a cam's width is in source
samples, so a patch sounds the same at any session rate. (The plan said to
resample on import; that would make a width in samples mean a different time
span, and so a different timbre, at each session rate. The original is embedded
anyway, so the sample player, which will need resampling, can do it later.)

Building (`WavetableBuilder`): range clamped to the file, optional snap (zero
crossings, or a whole number of periods detected around the centre), loop
overlap blended into the start (the cycle becomes `(1 - overlap) x width`),
resampled to a power-of-two work size (2048 to 32768; cubic up, box average
down), one forward FFT, then an inverse 2048 FFT per mip level keeping the
harmonics that level allows. DC removed.

Mip level per cylinder from its frequency (`WavetableSet::levelFor`, with a bias
of half an octave: the top harmonic may reach 1.4 x Nyquist, folding back above
0.29 fs at most, the usual trade between aliasing and dullness). A level change
crossfades over one control block.

**Per-voice position (decided: build the cache in v1).** Every cam has its own
`WavetableCache` (32 sets by default, 8 to 64 in the settings, about 180 kB
each). Positions are quantised into keys (width on a 1/96-octave grid, centre on
a 64th of the width, overlap in 0.5 % steps). Each control block a voice computes
its cam's modulated position; when its key changes it asks the cache: if ready it
takes it and crossfades from its current set over 256 samples; if not, it posts a
request (stream = voice and cam; only the latest request per stream is built) and
keeps playing what it has. A note that starts before its exact table exists
takes the nearest ready one. The engine also requests every cam's unmodulated
position each block, so tables are ready before notes arrive. One worker thread
builds for all four caches.

Concurrency: a slot carries a sequence counter (odd while being written) and a
reference count. A reader increments the count then rechecks the counter; the
worker makes the counter odd then checks the count, and backs off if anyone
holds the slot. A held slot is never rebuilt. Sequentially consistent atomics
make the two checks exclusive.

A new source bumps the cache generation (part of the key): voices crossfade to
tables of the new source as they are built; old sets are recycled first.

## Modulation

Control rate: every 32 samples (`cam::controlBlock`); gains, pans, sends, lifts
and oscillator increments are ramped linearly over the block; filter settings
change once per block.

Sources (`cam::ModSource`): the 16 slots, velocity, release velocity, key track,
aftertouch (channel and poly), pitch bend, mod wheel, 4 assignable CCs (CC 20 to
23 by default), the 4 cams' amp envelopes, a per-voice random value.

Slots: LFO (per voice or global free-running, locked to the host while it
plays), ADSR (per voice), curve (per voice, or global when "stick to DAW time":
its position comes from the host's PPQ). Each has an output level.

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

Cost (`tests/EngineTest.cpp`, this machine, -O2): 24 voices x 4 cams x 4
cylinders with both filters on, 45 to 55 % of one core; a 3-note chord of two
cams, under 3 %.

## Cross-modulation (timing belt)

Per pair src -> dst: FM (phase offset of dst's read, index up to 8 rad at full
lift), ring (`x (1 - lift + lift x src)`) or AM (`x (1 + lift x src)`), from the
source's first cylinder (decided). Feedback FM on itself only. Lift is the
destination cam's `xm<src>_lift` parameter (automatable, modulatable). Cycles are
refused in the editor (`StructureState::setXmod`) and, if one ever reached the
engine, all links but feedback would be dropped.

## State

The APVTS state root ("Parameters") carries:

- the host parameters (`PARAM` children);
- `<Structure>`: routes, belt, curves, CC numbers, MIDI learn map, randomize
  locks, the cache size, and the sample player's markers (reserved in schema 1);
- `<EmbeddedAudio>` (fxme::EmbeddedAudio: the original file, deflated float WAV
  in Base64; the plan said FLAC, which EmbeddedAudio dropped because it clamps
  peaks above full scale) and `<SourceOptions>` (channels, normalise, original
  rate);
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
- Table worker (`fxme::WavetableWorker`, std::thread): builds table sets.
- Source loader (`SourceManager::Loader`): decodes the embedded audio from a
  detached copy of the state, applies the options, hands the source to the
  engine and the editor.
- Effect loader (as in MechanOdd): IR loading for Cab / Reverb slots.
- Message thread: editor, structure edits, MIDI learn assignments (fed from the
  audio thread through a FIFO), presets.

## GUI

Header (`fxme::TopBar`): name, icon (clickable: about box), a small master view
(tachometer and vectorscope), the global Kick-start, the Garage (preset bar and
browser), the gear (settings callout) and help. Then two tabs:

- Synth: file band (import / export / built-in, channels, normalise, waveform
  view with the four ranges, playheads, zoom, drag and drop), scope rows (cam
  profile turning in polar form with a follower, oscilloscope), cam strips
  (pages Cam, Filters, Amp, Mix), bottom band (Modulators with draggable chips
  and per-type editors, Matrix, Timing belt). Dice in the section headers.
- Effects: Turbo 1 and 2 (two slots, return, pan, dry mute), Exhaust (two
  slots), output and rev limiter, a large master view; the selected effect's
  own panel with its module preset bar (shared with FxmeFX's plugins).

Every knob is a `CamKnob`: right-click menu (type a value, MIDI learn, modulate
from a source, remove modulations, lock from Kick-start, reset), modulation ring
and live value, drop target for source chips, badges for MIDI and lock.

Tooltips: `Tooltips.h`, behind the "?" switch (machine-wide, `AppSettings.h`).

## Presets

`fxme::PresetManager` on `FX-Mechanics/Camshaft/Presets`, factory presets from
`Source/Assets/*.xml` (made by `tools/make_factory_presets.py`, categories Idle,
Torque, Rev, Backfire after the engine theme), the browser from the top bar.
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
