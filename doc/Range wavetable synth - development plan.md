# Camshaft — development plan

Oct 8, 2026 · @Olivier

## Overview and scope

A polyphonic JUCE synth under the Fx-Mechanics brand: the user imports an audio file, defines up to 4 ranges on it, and each range is played as a wavetable at the MIDI note frequency. Each range has its own filters, mixer strip and oscilloscope; 16 modulators drive any parameter; an effects tab reuses FxmeFX as in MechanOdd.

The development should follow the rules defined in the local Fxmechenics super skill.

**In v1**

- 4 range players (wavetable mode), unison, hardwired amp envelope per range
- 2 filters per range per voice (FxmeTools types + formant), series or parallel
- Cross-modulation between ranges (FM, ring mod, multiply) with a routing graph
- 16 modulators (LFO, ADSR, curve) plus MIDI sources, modulation matrix, MIDI learn
- Audio embedded in presets, preset browser, randomize
- Effects tab: 2 aux chains and 2 master slots, safety limiter, master visualization

**Deferred**

- Sample-player mode (attack/loop/release markers, root key): the data model reserves it from day one, the engine comes later
- Undo/redo
- Rules for which voice each oscilloscope row displays

## Architecture and code layout

The DSP lives in the JUCE-free FxmeTools core, so the engine survives a later move away from JUCE; only the plugin shell, parameters and GUI depend on JUCE.

| Layer | Contents | Depends on |
| --- | --- | --- |
| `core/synth` (new, in FxmeTools) | Wavetable builder, oscillator, voice, voice allocator, modulators, mod matrix | FxmeTools core only |
| `core/filters` (existing) | Classic filter types, formant filter | — |
| FxmeFX | Aux and master effect slots | FxmeTools |
| Plugin shell | Processor, APVTS, state and preset I/O, MIDI learn | JUCE |
| GUI | Waveform view, scopes, range strips, modulator panel, routing graph, effects tab | JUCE |

Real-time rules as elsewhere: no allocation, locks or exceptions in `processBlock`. Everything sized at `prepareToPlay` for the max voice count, max unison and max table size. GUI to audio communication goes through lock-free FIFOs; new audio files and rebuilt tables are swapped in by atomic pointer exchange, with old buffers released on the message thread.

Any new functionnality that could be shared by other project should be implemented in FxmeTools using a clear and documented API.

## Voice engine

One voice = one note, carrying all 4 range players, their filters, amp envelopes and per-voice modulators.

- **Polyphony:** user setting, 1 to 32 voices (allocated at max). Stealing order: released voices first, then oldest, with a short fade (about 5 ms) to avoid clicks.
- **Mono / legato:** mono retriggers envelopes; legato keeps envelopes and curves running and only moves pitch. Last-note priority by default.
- **Glide:** time in ms or tempo-synced, mode "always" or "legato only".
- **Unison per range:** 1 to 8 sub-voices, detune (cents, spread symmetrically), stereo spread, and random start phase so stacked voices don't phase-lock. All sub-voices read the same table, so cost is mostly interpolation.
- **Amp envelope per range:** hardwired ADSR on the range's gain, velocity amount, and curve shape per segment. It is also exposed as a modulation source.
- **Voice lifetime:** a voice ends when every range's amp envelope has finished and any curve release has completed.

## Wavetable engine

Voices share one band-limited table set per range, rebuilt at block rate when center or width change, with a crossfade between the previous and the new set inside each block. This works as long as center and width are global; per-voice modulation of them needs a cache (below).

**Building a table set from a range**

1. Read the range `[center − width/2, center + width/2]` from the source buffer.
2. Optional snap: move both edges to the nearest zero crossing, or set the width to a whole number of detected periods (autocorrelation, computed on the message thread when the range is edited).
3. Loop crossfade: blend the last `xf` samples into the first `xf` (equal-power or linear, `xf` as a % of width) so the loop point is continuous.
4. Resample to a fixed length L = 2048 (oversampling for short ranges, decimation for long ones), via FFT.
5. Mipmaps: one table per octave (about 10), keeping only the harmonics below Nyquist at the highest note that table serves. One forward FFT, then one inverse FFT per level.

**Playback**

- Phase accumulator at the note frequency: the whole range is one period, so pitch is independent of the range content.
- Mipmap chosen from the current frequency, with a crossfade between adjacent levels during glide or pitch modulation.
- Interpolation quality: linear, cubic (Hermite), or windowed sinc (8 taps). Default cubic.

**When the range moves (your question)**

- Global center/width (from global LFOs, host automation, the GUI): rebuild once per block per range into a back buffer, then each voice reads both old and new sets and crossfades linearly over the block. Cost: 4 ranges × about 11 FFTs of size 2048 per block when moving, independent of voice count.
- Rebuild only when the change exceeds a threshold (e.g. one source sample), and cap the rebuild rate (e.g. every 2 blocks) if needed.
- Per-voice center/width (velocity, key tracking, per-voice LFO or curve): voices can no longer share. Proposal: quantize the position to a cache of K table sets per range (K = 32 to 64 slots, LRU), rebuilt by a worker thread, with voices crossfading between the two nearest cached positions. This bounds the cost but adds latency to the first appearance of a new position.
- Fallback if the cache is too complex for v1: only allow global modulators on center and width, and grey out per-voice sources for those two destinations.

**Memory**

4 ranges × 2 buffers × 11 levels × 2048 floats × 2 channels is about 1.4 MB. A cache of 64 sets per range adds about 46 MB in stereo, so K should be a setting.

## Range routing, filters and cross-modulation

Each range runs oscillator → cross-mod → filters → amp envelope → gain → pan, with the sends tapped before or after gain.

**Filters (2 per range, per voice)**

- Types from FxmeTools: LP / HP / BP / notch / peak / shelves, plus formant.
- Routing switch: series (F1 → F2) or parallel (F1 + F2, with a balance control).
- Classic controls: cutoff, resonance, gain where relevant, key tracking amount, envelope amount (shortcut to the mod matrix).
- Formant: vowel start, vowel end, position (0 to 1) between them, plus resonance/bandwidth. The position is the natural modulation target.
- Coefficients updated once per block from smoothed parameters; for fast filter sweeps, a sub-block update (every 16 or 32 samples) is an option.

**Cross-modulation between ranges**

Three connection types, each with an amount that is itself a modulation destination:

| Type | Operation | Notes |
| --- | --- | --- |
| FM | Phase modulation of the target's read position | Through-zero, index in radians |
| Ring | target × source (bipolar) | Classic ring modulation |
| AM | target × (1 + depth · source) | Unipolar multiplication |

- Taps the oscillator outputs, before the filters.
- Graph must be acyclic (no A → B → A); self-FM on one range is allowed as feedback. Ranges are processed per sample in topological order.
- Each range gets an "audible" toggle so it can act as a modulator only.
- Unison: the source uses its own first sub-voice (or its sum), to be decided by ear.

**Routing graph (GUI)**

Four boxes (ranges) on a small canvas. Drag from one box to another to create a connection; click the connection to cycle FM / ring / AM; a small knob on the edge sets the amount. Connections that would create a cycle are refused with a visual cue.

## Modulation system

Modulation is evaluated once per block and smoothed per sample at each destination; 16 modulator slots, each set to LFO, ADSR or curve, plus fixed MIDI sources.

**Modulator types**

| Type | Main controls | Scope |
| --- | --- | --- |
| LFO | Shape (sine, tri, saw, square, S&H, smooth random), rate in Hz or synced, phase, delay, fade-in, unipolar/bipolar | Option per LFO: per-voice retrigger or global free-running |
| ADSR | A, D, S, R, segment curves, velocity amount | Per-voice |
| Curve | Breakpoint editor with per-segment curvature; total duration absolute (s) or musical (beats/bars) | Per-voice, or global when "stick to DAW time" is on |

**Curve behaviour**

- Optional sustain region (start and end markers). While the note is held, the sustain region either holds its last value or loops.
- Optional release region after the sustain end, played on note-off. If disabled, note-off does nothing to the curve: it keeps looping (or holding) until the voice ends.
- "Stick to DAW time": the curve position is computed from the host's PPQ position instead of time since note-on, so every voice reads the same point. Falls back to free running when the host is stopped.

**Sources beyond the 16 slots**

Velocity, release velocity, key tracking, aftertouch (channel and poly), pitch bend, mod wheel, any MIDI CC, the 4 amp envelopes, a per-voice random value at note-on. MPE (pitch, pressure, slide per note) is planned in the data model and enabled later.

**Matrix**

- Each modulation = source, destination (any parameter, including modulator rates, depths and curve speed), bipolar depth, optional via-source (e.g. mod wheel scales the depth).
- Up to 64 routes. Destinations are chosen from the target control (right-click or drag a modulator onto a knob), and listed in a matrix view.
- Per-voice routes are summed per voice; global routes once per block. A route is per-voice if its source or destination is per-voice.
- Knobs show a modulation ring with the depth range, plus the live value of the last active voice.
- Modulator-to-modulator routes are evaluated in slot order; a route to a lower slot introduces one block of delay (documented, not a bug).

**MIDI learn**

Right-click any control → learn; mappings saved in the preset and optionally as a global default.

## Sample handling, presets and randomize

The audio file is embedded in the preset and in the host session state, so a patch never depends on a path.

**Import**

- Drag-and-drop or file dialog; WAV, AIFF, FLAC (whatever JUCE's format manager reads).
- Stereo kept as stereo; mono duplicated. A per-file option to use L, R or mid only.
- Resampled once to the session rate on import (high-quality offline resampler, on a background thread), original rate stored for re-export.
- Optional normalization (peak) applied at import, with the gain stored so it can be undone.
- Length cap (e.g. 60 s at 48 kHz) to keep presets and state reasonably small.

**Storage**

- Audio stored as FLAC inside the state (base64 in the XML, or a binary chunk if the state format moves to binary), to keep size down.
- The original file name is kept for display only.

**Waveform view**

Min/max overview pyramid computed at import; zoom and scroll; range handles for center and width; crossfade zone and snap points drawn on the waveform; live playhead per range when a note is held.

**Presets**

Follow the preset browser recommendations from the local Fx-Mechanics skills (not visible from here; to be merged into this section).

**Randomize**

Per-section dice (ranges, filters, modulators, effects) plus a global one, with lock icons to exclude parameters. Randomization respects sensible ranges (no cutoff at 20 Hz with zero resonance and full volume) and never touches the master limiter or output gain.

## Mixer, sends, effects and master

Each range feeds a dry bus and two send buses; the aux chains return into the master, which ends in the safety limiter.

**Range strip parameters**

Center position, width (samples), volume, pan, send 1, send 2, a tap point per send (pre-filter, post-filter, post-gain), coarse tune (semitones), fine tune (cents), filter controls, unison controls, amp envelope. Sends are computed per voice and summed into the aux buses, so per-voice pan and modulation carry into the effects.

**Effects tab (as in MechanOdd)**

- Aux 1 and Aux 2: 2 FxmeFX slots each, return level and return pan, optional mute of the dry path for 100% wet use.
- Master: 2 FxmeFX slots after the sum of dry and returns.
- Slot UI reused from MechanOdd: effect selector, bypass, per-effect parameters; all effect parameters are modulation destinations (global routes only).

**Master output**

- Output gain, then a safety limiter (look-ahead about 1 ms, ceiling fixed at −0.3 dBFS, can be disabled), always last.
- Visualization: to define with the GUI work. Candidates: stereo vectorscope (Lissajous), spectrum with a decaying trail, or a radial waveform driven by the master envelope. It runs on the GUI thread from a lock-free ring buffer and is capped at 30 to 60 fps.

## GUI layout

Two tabs, Synth and Effects; the Synth tab stacks file, scopes, range strips and modulators from top to bottom.

&#91;embedded content: synth tab layout · 5 bands\]

Scope rows and range strips share the same column order, so each range reads vertically from its scope to its controls. The Effects tab reuses the MechanOdd layout. The header (presets, tab switch, master view) stays visible on both tabs.

## Name and engine identity

The plugin is called Camshaft: a cam turns a rotating profile into motion, as the oscillator turns a range of the file into a periodic wave. The engine theme applies to section names and visuals; detailed parameters keep standard synth names so the plugin stays easy to use.

**Engine vocabulary (display names only, parameter IDs stay neutral)**

| Synth concept | Camshaft name | Why |
| --- | --- | --- |
| Range | Cam 1 to 4 | Each cam carries its own profile |
| Range width | Lobe | The lobe is the shape the follower reads |
| Loop crossfade | Overlap | Valve overlap: both valves open at once, like the two ends blending |
| Unison voices | Cylinders (1 to 8) | More cylinders, a fuller sound |
| Cross-mod routing graph | Timing belt | Links the cams together |
| Cross-mod amount | Lift | Valve lift: how far one cam pushes another |
| Aux effect chains | Turbo 1 and 2 | Extra power, fed in parallel |
| Master effects | Exhaust | The last stage before the output |
| Safety limiter | Rev limiter | Cuts in at the redline |
| Randomize | Kick-start |  |
| Preset browser | Garage |  |

Tooltips give the standard term ("Overlap: loop crossfade length"), so a user who doesn't follow the metaphor isn't lost.

**Visual design**

- Signature view: each cam row shows its wavetable drawn in polar form, as a real cam profile that rotates while a note plays (slowed to a visible rate), with a follower riding the edge. It sits next to the oscilloscope in each scope row.
- Master visualization: a tachometer arc whose needle follows the output level, a redline at the limiter ceiling and a warning light when the rev limiter acts. A dyno-style curve (smoothed spectrum) behind it.
- Timing belt graph: the four cams drawn as pulleys, connections drawn as belts, with the connection type (FM, ring, AM) as a small badge on each belt.
- Materials: flat design with mechanical cues, such as machined-aluminium knobs, engraved labels, gauge typography, one warning-orange accent. No heavy skeuomorphism, which ages badly and costs GUI performance.
- Logo: a camshaft silhouette with four lobes, one per range.
- Preset categories can follow the theme: Idle (pads, drones), Torque (basses), Rev (leads), Backfire (percussive and noisy).

## Parameter IDs and state versioning

Parameter IDs are frozen before the first public build, and every saved state carries a schema version with explicit migrations.

**ID scheme**

`<section><index>_<name>`, lowercase, never renamed once released: `r1_center`, `r1_width`, `r1_f1_cutoff`, `r1_f2_vowelpos`, `mod3_rate`, `aux1_return`, `master_out`. Display names can change freely; IDs cannot.

**What is a host parameter and what is not**

| Kind | Examples | Stored as |
| --- | --- | --- |
| Automatable float/choice | Levels, tunes, cutoffs, LFO rates, mod depths | APVTS parameter |
| Structural, not automatable | Modulator type, curve breakpoints, matrix routes, cross-mod graph, MIDI learn map | ValueTree child, saved with state |
| Asset | Embedded audio | Compressed binary in state |
| Reserved for later | Sample-player markers (attack, loop, release, root key) | Present in schema v1, unused by the engine |

Estimated count: about 60 parameters per range × 4, plus about 12 per modulator × 16, plus 64 route depths and the effects: roughly 700 host parameters. Worth grouping them (`AudioProcessorParameterGroup` per range, modulator and effect slot) so the host's automation list stays navigable.

**Versioning**

- `schemaVersion` integer in the state root; loading an older state runs migrations in order (v1 → v2 → …).
- Unknown IDs from a newer version are kept untouched in the tree, so a round-trip through an older build doesn't lose data.
- Adding parameters is allowed at any time; removing or retyping one requires a migration.

## Milestones

Build the sound first, then modulation, then the polish; each milestone ends with something playable.

1. **Engine skeleton.** One range, table builder with crossfade and mipmaps, cubic interpolation, polyphony and amp envelope, minimal GUI with waveform and range handles. Done when a chord on a loaded file plays without aliasing up to C8.
2. **Four ranges and the strip.** Tune, unison, volume, pan, snap to zero crossing/period, scope rows, embedded audio in state.
3. **Filters.** Two FxmeTools filters per range, series/parallel, formant with vowel start/end/position.
4. **Modulation core.** Matrix, LFO and ADSR, MIDI sources, per-block evaluation with smoothing, modulation rings on knobs.
5. **Curves.** Breakpoint editor, sustain loop, optional release, absolute/musical duration, stick to DAW time.
6. **Moving ranges.** Block-rate rebuild with crossfade for global center/width; then the per-voice cache, or the global-only fallback.
7. **Cross-modulation.** FM, ring, AM, and the routing graph GUI.
8. **Effects and master.** FxmeFX aux and master slots, sends with tap points, safety limiter, master visualization.
9. **Product layer.** Parameter IDs frozen, preset browser, randomize, MIDI learn, mono/legato/glide.
10. **Hardening.** CPU profiling at 32 voices × 8 unison, pluginval, state round-trip tests, host tests.

Later: sample-player mode, undo, oscilloscope voice selection, MPE.

## Open questions

- [ ] Per-voice modulation of center/width: build the table cache in v1, or ship the global-only fallback first? => Build the table cache directly in v1 (decided).
- [ ] Cross-mod source with unison: first sub-voice or the sum? => Sub-voice (decided).
- [ ] Max polyphony and unison: are 32 voices and 8 unison the right ceilings for the CPU budget? => 24 voices and 4 unison (decided).
- [ ] Preset browser: merge the recommendations from the local Fx-Mechanics skills into this plan. => yes, and any other recommendation (decided).
- [ ] Master visualization: which direction (vectorscope, spectrum trail, radial waveform, other)? => Vectorscope (do a re-usable component in FxmeTools, as recommended by Fxme skills) (decided).
- [ ] Licensing: the plugin inherits the JUCE/AGPL question and the FxmeTools core/shell split; does it wait for that decision or start on the current setup? => same as FxmeTools (decided).
- [ ] Plugin name: Camshaft (decided).
