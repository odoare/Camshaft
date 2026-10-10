# Camshaft: done

Newest first. Items that came from `todo.md` keep their number.

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
