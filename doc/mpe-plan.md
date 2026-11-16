# Plan: MPE in Camshaft

> 2026-11-09. Written for a separate working session. Status: implemented 2026-11-16 (done.md), to test by ear (todo 26). Departures: the switch is a session property, not a parameter (a preset load would reset it); voice modes forced to Poly, lower zone only, a fixed 5 ms bend smoothing (the open questions, answered with the proposals).

## Goal

An **MPE option, off by default**: Camshaft receives MPE. Each note arrives on its own channel with its own pitch bend, pressure and timbre (CC 74), and each voice follows its own. Off: nothing changes.

**Why:** Otomatotamoto's sources (`steps`, `player`, `tear`, `comp`, `arp`), TeAr with its own MPE option, and MPE keyboards send exact per-note pitch: microtonal scales, glides, chords gliding voice by voice. Today Camshaft has one global pitch bend (`CamEngine::pitchBend`), so a bend on one note would move them all.

## What FxmeTools gives

`fxme::MpeIn` (`core/FxmeTools/midi/Mpe.h`, JUCE-free, since 2026-11-09):
- `readPitchBend (channel, value)`: `channel` 0-based, `value` -8192..8191;
- `readController (channel, number, value)`: the RPN 0 that sets a channel's bend range; true if the controller belonged to it;
- `own (channel)`: that channel's bend, in semitones;
- `bend (channel)`: the same plus the master's.

Default ranges are the MPE ones: ±48 on channels 2–16, ±2 on channel 1.

**Pin:** `cmake/Dependencies.cmake`, FxmeTools (now at `0b46947`) to a commit that has it. Check the pinned FxmeFX still agrees with that FxmeTools.

## Design

### Which channel does what (with the option on)

| | Channel 1 (master) | Channels 2–16 (members) |
|---|---|---|
| Notes | as today, with no per-voice bend | the voice remembers its channel |
| Pitch bend | the global `srcPitchBend`, with `pb_range`, as today | that channel's voices, in semitones, range from `MpeIn` (not `pb_range`) |
| Channel pressure | global `srcAftertouch`, as today | that channel's voices: the per-voice `srcPolyAftertouch` (a source that already exists) |
| CC 74 | as today (a CC source, MIDI learn) | that channel's voices: a new per-voice source (below) |
| Other CCs | as today | ignored (MPE keeps sustain, mod wheel… on the master) |
| CCs 100, 101, 6, 38 | go to `MpeIn` **before** MIDI learn, so the zone's announcement is never learned nor treated as a CC source | the same |

With the option off, `handleMidi` is what it is today, whatever the channel.

### Shell (`PluginProcessor`)

- A parameter `ids::mpe` = `"mpe"`, an `AudioParameterBool`, default **false**, not modulation-routable. Old sessions read it as off.
- `handleMidi` splits on it; `m.getChannel()` is 1-based, `MpeIn` is 0-based.
- A `fxme::MpeIn` lives in the engine (below) or the shell; the shell must be able to give `CamEngine` semitones per channel.
- Turning it off: `engine.allNotesOff()`, and clear `MpeIn`.

### Engine (`Source/Engine`, JUCE-free)

- `CamEngine::noteOn (note, velocity, channel)` and `noteOff (note, releaseVelocity, channel)`, with the channel defaulting to 0, so the existing calls and tests don't move.
- **The allocator's key.** `fxme::VoiceAllocator` matches notes by number, and with MPE two channels can hold the same note (two microtonal pitches rounding to one key, or a retrigger during a release). Pass it a key, `note + 128 * channel`, in MPE mode, and decode it in `applyAction`. It only compares the number and hands it back, so nothing in FxmeTools should have to change. **Check** `pushNote` / `removeNote` and any use of the number as a pitch inside the allocator first.
- **Voices:** `CamVoice` gets `channel`, and setters like `setPolyPressure`:
  - `setMpeBend (semitones)`: **smoothed per voice** like `pitchBendSmoothed` in `CamEngine` (a glide arrives as a point every 1/64 beat, so without smoothing the pitch steps). Added in `CamVoice.cpp:407` to `semis`, beside `src[srcPitchBend] * state->pitchBendRange`, and independent of it.
  - `setMpeTimbre (value01)`: the new source.
- **Note order:** a source sends the pitch bend **just before** the note-on, so a new voice takes its channel's current bend at note-on, without smoothing from 0 (no slide into every note). Keep the last value per channel: bend (`MpeIn`), pressure, timbre.
- **Voice mode:** mono and legato make no sense when each note has its own pitch. Proposed: with MPE on, the allocator runs as Poly and the voice-mode control is disabled. A question for the session (below).
- **Glide** (`glideSeconds`) stays between notes in Mono/Legato, so it is off too in that case.

### A new modulation source

`srcMpeTimbre`: per voice, CC 74 on the note's channel, 0..1, starting at 0.5 (64, the MPE default).
- `CamParams.h`: add it to the sources **at the end of the enum** (its value may be stored in routes), to the per-voice test (`isPerVoiceSource`), and fill `src[srcMpeTimbre]` in `CamVoice.cpp` line ~260 beside `srcPolyAftertouch`.
- `ParamIDs.h`: a stable id (`"mpetimbre"`) and name (`"MPE timbre"`), next to `"polyat"`. Routes are saved by id, so old patches stay as they are; check that nothing loops on `srcRandom + 1` as the count.

### The window

An **MPE** toggle near the pitch-bend range (`fxme::FxmeButton`), with a tooltip. With it on: disable the voice mode and glide controls (polled in the timer, house style), and note that `pb_range` then applies to channel 1 only.

## Tests (`tests/EngineTest.cpp`, target `CamshaftEngineTest`)

- A note with a member channel's bend of +12 sounds an octave up; a second note on another channel stays. Measure as the other tests do.
- A bend arriving before the note-on: the note starts at that pitch (no slide up from 0).
- A bend during a note moves only that channel's voice.
- Two channels, the same note number: two voices; a note-off ends only its own.
- Per-channel pressure and CC 74 reach only their voices (through a route).
- Channel 1's bend still moves everything by `pb_range`.
- MPE off: every existing test passes untouched.
- **Shell:** the zone's announcement (96 CCs) with MIDI learn armed learns nothing.

## To test by ear

From Otomatotamoto (`steps` or `tear`, MPE on, through `midi-out` or as a plugin), or TeAr with MPE on, or an MPE keyboard:
1. A microtonal scale plays exact.
2. `1~5 . .`: a smooth glide, no zipper noise (tune the smoothing).
3. `{135}~{246}`: voices glide independently.
4. A long glide's reach: ±48 semitones of table reading, check for aliasing with the band-limited wavetable.

## Open questions for that session

- Voice modes with MPE on: force Poly (proposed), or keep Mono/Legato following the last note's channel?
- Should the option also read the **upper zone** (master on channel 16)? Proposed: lower zone only, as our sources send.
- The bend's smoothing time: fixed, or tied to `pb_range`'s control rate?
