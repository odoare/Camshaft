# Camshaft: done

Newest first. Items that came from `todo.md` keep their number.

## Timing belt: sync and opening modulation (2026-10-11)

Two new belt types (appended to `XmodType`, stored by name):
- **Sync**: the driven cam's cycle restarts at each cycle of the driving cam,
  at the sub-sample moment it began, the jump band-limited (polyBLEP, half
  before from a look-ahead, half after). The lift sweeps the driven cam up to
  three octaves while synced. Checked: a cam detuned by an irrational interval
  repeats at its driver's period once synced (correlation 0.999 against
  -0.34 free).
- **Opening**: the driving cam's output moves the driven cam's read position
  at audio rate, up to a cycle either way at full lift. Checked: it changes
  the sound, and nothing at zero lift.

Patches without them are unchanged (same output as before within 2e-5).

## Modulator shape displays; short labels (2026-10-11)

- The LFO and ADSR editors keep their controls on the left half and show the
  modulator's shape on the right: one LFO cycle (random shapes as an example)
  or the envelope (segments sized by their times, the sustain a quarter of the
  width), with a dot where the newest voice (or the global LFO) is. The
  engine publishes each modulator's phase, or envelope stage and position
  (`Display::modPhase`, `modStage`; FxmeTools `CurveAdsr::getSegmentPosition`).
- The LFO and ADSR controls are back in the slot's colour; the curve editor
  keeps the accent for its controls and blue for its sustain ones.
- Short knob labels: Cutoff, Res, Gain, Ktr, Env, Vowel on the filter pages;
  A curve, D curve, R curve on the Amp page.

## GUI: larger cam knobs, curve colours, darker belt (2026-10-11)

- The Cam, Amp and Mix pages' knob rows grow into the strip's spare height
  (`ControlPage::addRow` takes a maximum), so their knobs are larger while the
  strips and the labels keep their sizes; the Filters page is unchanged. Its
  knobs lose the "F1" / "F2" prefix (the host names keep it). Toggles in a
  page keep a button's size (Rnd phase now looks like Audible).
- The modulator chips share the whole height of their column.
- Curve editor: the curve in its modulator's colour, the sustain and release
  switches and the loop mode in the sustain region's blue. The editor's other
  controls are back to the plugin's orange (the slot's colour stays on its
  name and its curve).
- The timing belt is darker, its pulleys too, and they turn at the cam
  profiles' speed.
- The knobs' discs are almost black (theme `knobCentre`).

## GUI: modulator colours, flat knobs, larger text (2026-10-11)

- Each modulator slot has its own colour (theme `modColour`, hues spread by
  the golden ratio): its chip wears it (a stripe, the outline), grey when the
  slot is off or modulates nothing, and every knob it modulates shows the
  modulation as an outline in that colour, one per route (amp envelopes in
  their cam's colour, MIDI and voice sources in steel). The slot's editor
  uses it as its accent.
- Knobs use a new flat style (FxmeTools `FxmeLookAndFeel::KnobStyle::flat`):
  no light rim, thicker arcs, a bold pointer mark, a larger bold label.
- Labels and read-outs are 12 % larger (theme `fontScale`), the layouts
  unchanged.
- The timing belt sits on dark grey with lighter grey pulleys; its lift knobs
  are larger.
- The dashboard's tachometer has its ticks labelled in dB.

## Belt & Viz and the dashboard; a level meter in the top bar (2026-10-10)

- The bottom band opens on "Belt & Viz" (first tab): the timing belt on the
  left half, a dashboard on the right, laid out like a car's instrument
  cluster: the tachometer, the spectrum in the middle where the speedometer
  sits (new FxmeTools component `CircularSpectrum`), the vectorscope; warning
  lights REV (limiter), CHECK (DSP load above 90 %), STEAL (a voice stolen),
  MIDI, HOLD (sustain pedal), SYNC (host playing), T1 and T2 (Turbo signal);
  FUEL (free voices) and TEMP (DSP load) gauges; the voice mode as a P / M / L
  gear selector, the rev limiter switch, and a hazard button (panic: every
  voice silenced at once, from the audio thread at its next block).
- The top bar shows the output as a horizontal stereo meter (RMS bars, held
  peaks; FxmeTools `StereoLevelMeter` gained a horizontal layout) instead of
  the small tachometer and vectorscope. The Effects tab keeps its large
  master view.
- The processor measures its DSP load (JUCE's load measurer), meters both
  output channels, feeds the spectrum with the mid signal while it is shown,
  and publishes the Turbo outputs' peaks; the engine counts notes and steals
  and shows the pedal. Checked in the engine test (counters, panic); the
  dashboard itself has not been seen yet.

## Range parameter IDs follow their names (2026-10-10)

`r<n>_start` and `r<n>_length` became `r<n>_opening` and `r<n>_duration`, so
IDs, names, presets and code use the same words before the IDs freeze at the
first release. Schema 3 migrates sessions and presets saved with the previous
IDs exactly (and those with the plan's centre and width through schema 2 as
before); the factory presets were regenerated.

## Silent cams are not rendered (2026-10-10)

Per voice, a cam that cannot be heard and modulates no other cam is skipped
(oscillator, filters, gains): when its amp envelope has finished (cams release
at their own pace, so in a long pad the short cams stop early), or when its
volume is off (-60 dB) and no send can carry it (a send tapped before the
volume still can). The decision uses the voice's modulated values and waits
for a fade to zero to end; the ramps move on while it is skipped, so it comes
back where its settings are. Two of four cams at -60 dB: half the CPU (8 voices
x 4 cams x 4 cylinders, 22 % to 11 %). The output of a patch where nothing is
silent is unchanged. Checked in the engine test (rendered when heard, when
feeding another cam, when sending before its volume; skipped when its volume is
off, when sending after it, after its envelope ended).

## CPU, first pass (2026-10-10)

About 30 % less CPU at the same sound, measured against the previous code
(alternating runs, best of each, this laptop's clock varies with its heat): 8
voices x 1 cam from 3.3 to 2.2 % of one core, 8 voices x 4 cams x 4 cylinders
with both filters from 32 to 23 %, the full load from 97 to 70 %.

- The voice renders one cam at a time over the control block (in the timing
  belt's order, so a cam's output is complete before the cams it modulates
  read it), one cylinder at a time inside it, the loop compiled per
  interpolation and per mono / stereo source: about 22 %. Checked against the
  previous code on a patch with FM, ring, AM, self FM, stealing, both send
  taps and a stereo source: the same output within 2e-5 of full scale (strong
  self FM is chaotic and drifts apart from any change of rounding, as it
  would from one build to another).
- Stereo files read both channels with the position arithmetic shared, the
  cubic as four weights shared by the channels (FxmeTools
  `MipmappedBuffer::readPair`): about 7 %.
- The equal-power crossfade of the overlap uses a polynomial instead of sin
  and cos; the amp envelopes carry their exponential from sample to sample
  instead of two `exp` per sample (FxmeTools `CurveAdsr`, in double so a
  long segment does not drift); a filter pair computes its coefficients once
  and only when they change; a cylinder's pyramid level comes from its pitch
  without a logarithm.
- GUI: the waveform is drawn once into an image (it was redrawn column by
  column 30 times a second while notes played); the knobs on hidden pages
  skip their timer, and the others keep their list of routes until the routes
  change.

## Decisions settled (2026-10-10)

- **1. First build and run**: done; it works as expected.
- **4. Source rate**: the recording keeps its own rate. It is read at any
  speed through the pyramid, so resampling it to the session rate would gain
  nothing, and a duration in samples means the same sound at any session rate.
- **5. Dry mute**: per Turbo, as implemented.
- **6. Engine location**: stays in `Source/Engine` for now.
- **24. Live range reading**: kept.
- **17. On-screen keyboard**: dropped, not wanted.

## Curve editor beat grid (2026-10-10)

When a curve modulator's duration is in beats, its editor draws the beat grid
(lines at the chosen division, stronger on each beat, or on each bar for
divisions of a beat or more) instead of plain eighths. A switch in the settings
callout ("Curves") makes the points dragged or added snap to it, alt held moving
one freely; a menu next to it picks the division (a bar, a beat, 1/2, 1/3, 1/4,
1/6, 1/8 of a beat). Both are machine-wide, like the "?" switch (AppSettings).
FxmeTools: `BreakpointCurveEditor::setTimeGrid`.

## Curve modulators: back and forth, markers above the plot, sustain with Stick to DAW (2026-10-10)

- **Back and forth.** The sustain region's mode is now a choice: hold, loop, or
  back and forth (from its end back to its start, then forwards, with no jump).
  Stored as `pingpong` on the curve (FxmeTools: `CurveShape::sustainPingPong`).
- **Sustain markers** are tabs in a band above the plot, so they no longer sit
  on the points at the top of the curve; the line between them shows the mode
  (one arrow for a loop, two for back and forth).
- **Stick to DAW ignored the sustain**: the global curve's position was the
  host time wrapped over the whole curve. It now plays like a note held from
  the song's start: the start once, then the sustain region (looped, back and
  forth, or held); the whole curve repeats only without a sustain region.
- Checked: core tests (back and forth stays in its region, goes both ways,
  never jumps, releases to the end; the host-timeline positions) and an engine
  test (a curve on a playing host's timeline plays its start, then stays in
  its sustain loop; the old code repeated the whole curve and fails it).

## Ranges by opening and duration; live ranges, curve probe, Off modulators (2026-10-10)

- **Opening and duration.** A cam's range is now set by where it starts
  (`r<n>_start`, "Opening", 0 to 1 of the file) and how long it lasts
  (`r<n>_length`, "Duration", samples) instead of its centre and width. On the
  waveform, dragging an edge moves that edge alone, the other stays; the
  numbered flag always shows the hand and moves the range, however narrow it
  is. The engine, the waveform and the cam profile share one function for the
  range (`cam::camRange`). Schema 2 migrates sessions (see todo 25); the factory
  presets were regenerated with the exact positions.
- **15. Modulated range on the waveform.** While notes play, one lane per cam
  along the bottom of the waveform shows the range every voice reads
  (modulation, snapping and smoothing included) with a tick where it reads; the
  newest voice's read position runs across the whole height. The engine
  publishes each voice's range per block (96 small records, a few atomic
  stores).
- **Curve probe.** While a point of a curve modulator is held, the waveform
  marks where its value puts the opening or the end of each range that curve
  modulates, with the range as a ghost (FxmeTools:
  `BreakpointCurveEditor::onPointHeld`).
- **Off modulators, unused ones not computed.** Modulator slots have a fourth
  type, Off, the default. Only slots that feed something (directly, through a
  via, or through another used slot) are computed; the others output 0, do not
  keep a voice alive in a curve release, and are drawn dimmed with no meter.
  Routing from an Off slot turns it into an LFO. Kick-start leaves Off slots
  off.
- Checked in the engine test: a range opens at its opening and lasts its
  duration, cut short at the end of the file, never under 16 samples; an
  unconnected LFO is not computed, a connected one runs, one feeding a used
  LFO runs, an Off one never does. The sweep test was made deterministic
  (random phase off, 4-block windows) and checks the middle of the saw region,
  away from the morph's boundaries.

## Modulator meters drawn on one scale (2026-10-09)

The small bars under the modulator chips jumped at every zero crossing of a
bipolar LFO: positive values were drawn from the left edge over the full width,
negative ones from the centre at half scale. A bipolar source now grows from
the centre both ways (-1 at the left edge, +1 at the right), a unipolar one from
the left edge, refreshed at 30 Hz instead of 15. The engine side was checked
first: a new engine test samples the published value of a 2 Hz LFO under a held
note, and its largest step per block stays within the sine's own slope.

## Ranges read live from the source; LFO phase fixes (2026-10-09)

- **Moving ranges still glitched** after the first fix. The cause was the
  design: one precomputed table per range position, so a moving range was a
  series of crossfades between different waveforms. The engine now reads each
  range straight from the source (an octave pyramid built at load, the level
  following the read speed, the overlap crossfaded live, a polyBLEP at the wrap
  when there is no overlap), with start, cycle and overlap smoothed and ramped
  per sample: a moving range is a continuous scan. The table cache, its worker
  and the "table cache" setting are gone; snapping uses zero crossings and a
  period track computed at load. Checked: the sweep test (mid-sweep sound
  matches a held note), new core tests (the pyramid does not alias at speed 8,
  its levels line up with the source, an overlapped loop wraps without a step,
  the period track and zero crossings). Cost: the full-load test now takes
  about 90 % of one core (two octaves blended per read); see todo 10.
- **LFO phase.** The phase knob was ignored at note-on: the voice restarted
  its LFOs before giving them that block's parameters, so a note started from
  the previous note's phase. `ModulationLfo::noteOn` now takes the start phase
  at the next advance. And a global LFO locked to the host jittered by up to
  32 samples per block with host block sizes that are not multiples of 32 (it
  took the block's start position for the first control update, which comes
  later); it now uses the exact position, at the end of each control span like
  every other modulator. Both pinned by tests (the host-lock test with
  441-sample blocks fails with the old code, error 1.6e-2, against 4e-7).

Existing sessions: they sound the same but for these fixes; the "table cache"
property left in older states is ignored.

## Smooth range moves; imported files load reliably (2026-10-09)

From the first test in a host.

- **Moving a centre or a lobe while playing stepped.** A voice only took the
  table of the exact position it wanted; during a sweep that position changes
  every control block and was never ready in time, so the voice kept its old
  table until the sweep stopped, then jumped. It now moves to any ready table
  closer to where it should be (`WavetableCache::distance`), while asking for
  the exact one. And every table is now phase-aligned in the builder, so the
  crossfade between two neighbouring tables no longer combs. Checked by a new
  sweep test in `tests/EngineTest.cpp` (mid-sweep, the sound matches a note
  held at that centre; with the old logic it was still the starting sine).
- **A new file kept the built-in length, or did not load.** The cause was in
  FxmeTools: `EmbeddedAudio::createReader` inflated from a destroyed
  temporary, so large files failed at random, and `SourceManager` then fell
  back to the built-in source without a word. Fixed in FxmeTools (see its
  `api-changes.md`: ConvolReverb's external IRs and SuperMoTo had the same
  latent bug). A decode failure now keeps the current audio and shows a
  message. The import itself (encoding up to 60 s of float audio) moved off
  the message thread, so the editor no longer freezes during it; the
  embedded copy lands in the state when ready, and changing the channel or
  normalise option meanwhile re-runs the import, while a session or preset
  load cancels it.

Existing sessions: none affected (playheads aside, a table's rotation is not
audible on a held note).

## First implementation of the v1 plan (2026-10-09)

The whole of `doc/Range wavetable synth - development plan.md`, milestones 1
to 9, in one pass, plus the FX-Mechanics shell. What was checked: the FxmeTools
core tests (58 synth checks, all pass; the JUCE-free guard passes), the engine
test (a chord, voices freed after release, full load finite; 45 to 55 % of one core
at 24 voices x 4 cams x 4 cylinders), a CMake configure, and a syntax-only
compile of every translation unit (no errors, no warnings in Camshaft's files).
Not checked: a link, a run, a listen (todo 1).

- **FxmeTools** (core): wavetable set, builder (snap, overlap, FFT mipmaps,
  period detection), lock-free table cache with its worker, table reader, curved
  ADSR, modulation LFO, breakpoint curve with sustain / release regions, voice
  allocator, modulation matrix arithmetic, oscillator graph; multimode filter
  (SVF types and formant), look-ahead limiter, stereo tap, min / max pyramid.
  (module): vectorscope, gear button, breakpoint curve editor. Recorded in
  FxmeTools' `doc/api-changes.md`.
- **Engine** (`Source/Engine`, JUCE-free): four cams per voice, cylinders with
  detune / spread / random phase, per-voice table keys with crossfades, mip
  crossfades, two filters series / parallel with key tracking and envelope
  amount, curved amp envelope, sends with three tap points, timing belt (FM,
  ring, AM, feedback FM), 16 modulators (per voice or global), 64 routes with via
  sources, global destinations, glide, mono / legato, built-in demo source.
- **Shell**: parameters from the engine's spec tables, grouped; structural state
  with a schema version and migration hook; embedded audio with channel and
  normalisation options, export of the original; MIDI learn with a machine-wide
  default; Kick-start per section with locks; presets (Garage) with partial
  factory presets, the MIDI map and locks kept across loads, optional audio
  keep; FxmeFX effects in two Turbo chains and the Exhaust with their module
  presets; rev limiter; output tap and meters.
- **GUI**: top bar with master view, Kick-start, presets, settings, help, icon
  and splash; Synth tab (waveform with ranges, scope rows with rotating cam
  profiles, strips, modulators with draggable chips, matrix, timing belt);
  Effects tab; `CamKnob` (menu, modulation ring, live value, drop target).
- **Project**: CMake (universal macOS settings before `project()`, every
  auxiliary target on FxmeCore), release workflow, README with the macOS
  quarantine and MIDI routing notes, doc triplet, factory preset generator.
- The plan's "Presets" section now carries the FX-Mechanics recommendations, as
  decided in its open questions.
