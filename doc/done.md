# Camshaft: done

Newest first. Items that came from `todo.md` keep their number.

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
