/*
  ------------------------------------------------------------------------------
    Tooltips.h

    Every hover-help string of Camshaft, one namespace per pane. No tooltip
    literal anywhere else. The engine names (Opening, Duration, Cylinders...)
    come with their standard term, so the metaphor never gets in the way.

    Shown only while the "?" switch in the settings is on (AppSettings.h).

    Author: Olivier Doaré, github.com/odoare
    Dual-licensed, mirroring the JUCE framework it depends on: under the GNU
    AGPL Version 3.0, or under commercial terms available from the author.
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
  ------------------------------------------------------------------------------
*/

#pragma once

namespace camshaft::tips::bar
{
    inline constexpr auto kickStart = "Kick-start: randomize everything (cams, filters, modulators, effects). Locked knobs, the output and the rev limiter are left alone.";
    inline constexpr auto gear      = "Settings: voices, glide, interpolation, MIDI, hover help.";
}

namespace camshaft::tips::file
{
    inline constexpr auto import      = "Import an audio file (WAV, AIFF, FLAC, Ogg...). It is stored inside the session and the presets.";
    inline constexpr auto exportAudio = "Export the imported audio as a WAV file, as it was imported.";
    inline constexpr auto builtIn     = "Back to the built-in morph of waveforms (removes the imported audio).";
    inline constexpr auto channels    = "Which channels of a stereo file the cams read: both, left, right, or their mid.";
    inline constexpr auto normalise   = "Normalise the file's peak to -1 dBFS (not destructive: off restores its level).";
    inline constexpr auto waveform    = "Drag inside a cam's range, or its numbered flag, to move it; drag an edge to move that edge only. The coloured bars along the bottom are the ranges every playing voice reads (modulation included), the bright lines where they read. Wheel: zoom; shift+wheel or drag on empty space: scroll; double-click: whole file (or zoom on a range). Drop a file here to import it.";
}

namespace camshaft::tips::scope
{
    inline constexpr auto profile = "The cam's wave as a cam profile (one cycle around the circle). It turns while the cam plays.";
    inline constexpr auto scope   = "The cam's output in the newest voice, after its filters and envelope.";
}

namespace camshaft::tips::cam
{
    inline constexpr auto audible      = "Audible: off, the cam is not heard and only modulates the cams it drives on the timing belt.";
    inline constexpr auto pageCam      = "The cam's profile, tuning and cylinders (unison).";
    inline constexpr auto pageFilters  = "The cam's two filters.";
    inline constexpr auto pageAmp      = "The cam's amp envelope.";
    inline constexpr auto pageMix      = "Volume, pan, the Turbo sends, and the lift of the belts driving this cam.";

    inline constexpr auto start        = "Opening (range start): where in the file the range begins (0 = start of the file, 100 % = end).";
    inline constexpr auto length       = "Duration (range length): how long the range lasts, in samples of the file, from its opening (cut short at the end of the file). The whole range is one cycle of the wave.";
    inline constexpr auto overlap      = "Overlap: loop crossfade length, as a share of the duration. Blends the end of the range into its start so the cycle wraps smoothly.";
    inline constexpr auto overlapShape = "Overlap shape: linear or equal-power crossfade.";
    inline constexpr auto snap         = "Snap: move the edges to zero crossings, or make the duration a whole number of periods of the sound.";
    inline constexpr auto coarse       = "Coarse tune, in semitones.";
    inline constexpr auto fine         = "Fine tune, in cents.";
    inline constexpr auto cylinders    = "Cylinders: unison voices (1 to 4) playing the same wave, detuned and spread.";
    inline constexpr auto detune       = "Detune: how far the cylinders spread in pitch, in cents (shared symmetrically).";
    inline constexpr auto spread       = "Spread: how wide the cylinders spread across the stereo field.";
    inline constexpr auto randomPhase  = "Random phase: each cylinder starts at a random point of the cycle, so stacked cylinders never lock together.";

    inline constexpr auto routing      = "Filter routing: F1 then F2 (series), or both side by side (parallel).";
    inline constexpr auto balance      = "Parallel balance between F1 (left) and F2 (right).";
    inline constexpr auto filterOn     = "This filter on or off.";
    inline constexpr auto filterType   = "Filter type: low / high pass (12 or 24 dB per octave), band pass, notch, peak, shelves, or formant (vowels).";
    inline constexpr auto cutoff       = "Cutoff (or centre) frequency.";
    inline constexpr auto resonance    = "Resonance (for the formant: how narrow the formants are).";
    inline constexpr auto gain         = "Gain of the peak and shelf types.";
    inline constexpr auto keyTrack     = "Key tracking: 100 % moves the cutoff an octave per octave played (from middle C).";
    inline constexpr auto envAmount    = "Envelope amount: the cam's amp envelope moves the cutoff up to 6 octaves (negative: down).";
    inline constexpr auto vowelA       = "Formant: the vowel at position 0.";
    inline constexpr auto vowelB       = "Formant: the vowel at position 100 %.";
    inline constexpr auto vowelPos     = "Formant: position between the two vowels. The natural target for a modulator.";

    inline constexpr auto attack       = "Amp envelope attack time.";
    inline constexpr auto decay        = "Amp envelope decay time.";
    inline constexpr auto sustain      = "Amp envelope sustain level.";
    inline constexpr auto release      = "Amp envelope release time.";
    inline constexpr auto attackCurve  = "Attack curve: negative rises fast then slows, positive starts slowly.";
    inline constexpr auto decayCurve   = "Decay curve: negative falls fast then slows, positive starts slowly.";
    inline constexpr auto releaseCurve = "Release curve: negative falls fast then slows, positive starts slowly.";
    inline constexpr auto velocity     = "How much the note's velocity scales the cam's level.";

    inline constexpr auto volume       = "The cam's volume.";
    inline constexpr auto pan          = "The cam's pan.";
    inline constexpr auto send1        = "Send to Turbo 1 (aux effect chain).";
    inline constexpr auto send2        = "Send to Turbo 2 (aux effect chain).";
    inline constexpr auto tap1         = "Where the Turbo 1 send is taken: before the filters, after them, or after the volume.";
    inline constexpr auto tap2         = "Where the Turbo 2 send is taken: before the filters, after them, or after the volume.";
    inline constexpr auto lift         = "Lift: how strongly the cam of this colour drives this one through the timing belt (only with a belt between them).";
}

namespace camshaft::tips::mods
{
    inline constexpr auto slotChip      = "A modulator slot: click to edit it, drag it onto a knob to modulate that knob. The bar shows its live output; dimmed, the slot is off or modulates nothing, and is not computed.";
    inline constexpr auto fixedChip     = "A MIDI or voice source: drag it onto a knob to modulate that knob.";
    inline constexpr auto type          = "LFO, ADSR envelope, a drawn curve, or Off (the slot is not used).";
    inline constexpr auto shapeView     = "The modulator's shape (one LFO cycle, or the envelope), the dot where the newest note is on it. Random shapes are drawn as an example; the dot shows the real value.";
    inline constexpr auto level         = "Output level of this modulator (a modulation destination itself).";
    inline constexpr auto bipolar       = "Bipolar: swings both ways around the knob's value; off, only upwards.";
    inline constexpr auto shape         = "LFO shape. S&H holds a new random value each cycle; smooth random glides between them.";
    inline constexpr auto rate          = "LFO rate, in Hz.";
    inline constexpr auto sync          = "Sync the rate to the host tempo.";
    inline constexpr auto division      = "LFO cycle length, in notes at the host tempo.";
    inline constexpr auto phase         = "Start phase of the LFO (per voice: at each note).";
    inline constexpr auto delay         = "Delay after the note before the LFO starts.";
    inline constexpr auto fade          = "Fade-in time of the LFO after its delay.";
    inline constexpr auto mode          = "Per voice: each note restarts the LFO. Global: one free-running LFO for every voice, locked to the host while it plays.";
    inline constexpr auto attack        = "Envelope attack time.";
    inline constexpr auto decay         = "Envelope decay time.";
    inline constexpr auto sustain       = "Envelope sustain level.";
    inline constexpr auto release       = "Envelope release time.";
    inline constexpr auto curve         = "Segment curve: negative moves fast then slows, positive starts slowly.";
    inline constexpr auto velocity      = "How much the note's velocity scales the envelope.";
    inline constexpr auto curveEditor   = "Drag points; double-click to add or remove one; drag a segment's small circle to bend it; shift snaps to a grid. Timed in beats, the beat grid is drawn, and points snap to it when the snap is on in the settings (alt: free). With the sustain on, drag its markers at the top. While a point is held, the waveform marks where its value puts the opening or the end of each range this curve modulates.";
    inline constexpr auto duration      = "Length of the whole curve, in seconds.";
    inline constexpr auto beats         = "Length of the whole curve, in beats at the host tempo.";
    inline constexpr auto durationMode  = "Curve length in seconds or in beats.";
    inline constexpr auto speed         = "Playback speed of the curve (a modulation destination).";
    inline constexpr auto daw           = "Stick to DAW time: the curve follows the host's position, the same point for every voice; free running when the host stops. With a sustain region, it plays its start once, then stays in the region (as a note held forever); without one, the whole curve repeats.";
    inline constexpr auto sustainRegion = "Sustain region: while the note is held, the curve stays between the two markers.";
    inline constexpr auto loop          = "What the sustain region does while the note is held: hold the value at its end, loop it, or go back and forth between its markers. With Stick to DAW, the curve plays its start once from the song's start, then stays in the sustain region.";
    inline constexpr auto releaseRegion = "Release region: on note-off, the curve plays from the sustain end to its end, and the voice waits for it.";
}

namespace camshaft::tips::matrix
{
    inline constexpr auto source      = "The modulation source.";
    inline constexpr auto destination = "The parameter it modulates.";
    inline constexpr auto via         = "Optional second source scaling the depth (a mod wheel opening the modulation, say).";
    inline constexpr auto depth       = "Depth, as a share of the destination's whole range; negative inverts.";
    inline constexpr auto clear       = "Remove this route.";
}

namespace camshaft::tips::belt
{
    inline constexpr auto view = "Timing belt: cams modulating cams. Drag from one pulley to another to connect them (FM); click a belt's badge to change it (FM, ring, AM, sync, opening, off); double-click a pulley for feedback FM. Loops are refused. Sync restarts the driven cam's cycle at each cycle of the driving one (its lift sweeps the driven cam up to three octaves); Opening moves the driven cam's read position with the driving cam's output.";
    inline constexpr auto lift = "Lift: the amount of this connection.";
}

namespace camshaft::tips::bottom
{
    inline constexpr auto modulators = "The 16 modulators and the MIDI sources.";
    inline constexpr auto matrix     = "Every modulation route.";
    inline constexpr auto belt       = "The timing belt (cross-modulation between the cams) and the dashboard: output gauges, warning lights, voice mode, panic.";
}

namespace camshaft::tips::dice
{
    inline constexpr auto cams       = "Kick-start the cams: new profiles, tuning, mix and envelopes (locked knobs kept).";
    inline constexpr auto filters    = "Kick-start the filters (locked knobs kept).";
    inline constexpr auto modulators = "Kick-start the modulators and the depths of the existing routes (locked knobs kept).";
}

namespace camshaft::tips::effects
{
    inline constexpr auto turboSlot   = "A Turbo slot: the effect it holds. Turbos are fed by the cams' sends.";
    inline constexpr auto exhaustSlot = "An Exhaust slot: an effect on the whole mix.";
    inline constexpr auto show        = "Show this slot's effect.";
    inline constexpr auto returnLevel = "Level of this Turbo in the mix.";
    inline constexpr auto returnPan   = "Pan of this Turbo in the mix.";
    inline constexpr auto dryMute     = "Dry mute: cams sending to this Turbo lose their dry signal (100 % wet).";
    inline constexpr auto output      = "Output level, before the rev limiter.";
    inline constexpr auto limiter     = "Rev limiter: a safety limiter at -0.3 dBFS, always last.";
    inline constexpr auto dice        = "Kick-start the loaded effects' settings (types and levels kept).";
}

namespace camshaft::tips::master
{
    inline constexpr auto tach  = "Output peak level. The red arc is the rev limiter's ceiling; the light shows it cutting in.";
    inline constexpr auto scope = "Vectorscope of the output: vertical is mono, wide is stereo, horizontal leans out of phase.";
    inline constexpr auto meter = "Output level, left and right (RMS bars, peaks held a moment; red above 0 dBFS).";
}

namespace camshaft::tips::dash
{
    inline constexpr auto spectrum = "Spectrum of the output, 20 Hz at the lower left to 20 kHz at the lower right (tilted 3 dB per octave so music reads level).";
    inline constexpr auto rev      = "REV: the rev limiter is reducing the level.";
    inline constexpr auto check    = "CHECK: the audio processing is near or over its time budget (DSP load above 90 %). Fewer voices, cams or cylinders will help.";
    inline constexpr auto steal    = "STEAL: a voice was stolen for a new note; the polyphony is used up.";
    inline constexpr auto midi     = "MIDI: notes are coming in.";
    inline constexpr auto hold     = "HOLD: the sustain pedal is down.";
    inline constexpr auto sync     = "SYNC: the host is playing; synced LFOs and curves follow its position.";
    inline constexpr auto turbo    = "Turbo chain carrying signal.";
    inline constexpr auto fuel     = "FUEL: the voices still free.";
    inline constexpr auto temp     = "TEMP: the DSP load (time spent processing over the time available).";
    inline constexpr auto gears    = "Voice mode: P poly, M mono (each note restarts the envelopes), L legato (notes played legato only move the pitch).";
    inline constexpr auto limiter  = "Rev limiter on or off (a safety limiter at -0.3 dBFS, always last).";
    inline constexpr auto hazard   = "Hazard: silences every voice at once (panic).";
}

namespace camshaft::tips::global
{
    inline constexpr auto voices        = "Polyphony: how many notes at once.";
    inline constexpr auto voiceMode     = "Poly, mono (each note restarts the envelopes) or legato (notes played legato only move the pitch).";
    inline constexpr auto glide         = "Glide time from one note to the next.";
    inline constexpr auto glideSync     = "Glide time in a tempo division instead of milliseconds.";
    inline constexpr auto glideDivision = "Glide time as a note length at the host tempo.";
    inline constexpr auto glideMode     = "Glide always, or only between notes played legato.";
    inline constexpr auto pitchBend     = "Pitch bend range in semitones.";
    inline constexpr auto interpolation = "How the audio is read between its samples: linear (cheapest), cubic, or sinc (flattest).";
    inline constexpr auto ccNumber      = "Controller number of this CC source.";
    inline constexpr auto saveLearn     = "New instances will start with the current MIDI learn map.";
    inline constexpr auto clearLearn    = "New instances will start with no MIDI learn map.";
    inline constexpr auto keepAudio     = "Keep the current audio when a preset is loaded (this instance only).";
    inline constexpr auto curveSnap     = "Curve modulators timed in beats: the points you drag or add land on the beat grid (hold alt to move one freely). For every instance.";
    inline constexpr auto curveGrid     = "The beat grid's division in the curve editor (drawn whenever a curve's duration is in beats).";
    inline constexpr auto tooltips      = "Hover help on the controls: on or off (for every instance).";
}
