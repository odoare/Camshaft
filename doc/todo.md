# Camshaft: to do

Numbered for reference; numbers are never reused (finished items move to
`done.md` with their number). Priority: high / medium / low. "FxmeTools" or
"FxmeFX" means the item touches the shared submodules.

## Build and verification

1. **First build and run** (high). The plugin has passed a CMake configure and a
   syntax-only check of every translation unit, nothing more: it has never been
   linked, loaded or heard. Build the VST3 and the standalone, then check in a
   host: sound on the first note (built-in source), import a file, drag the
   ranges, per-voice modulation of a centre (velocity -> centre), the timing
   belt, the effects tab, preset save / load, a session round trip with
   embedded audio, MIDI learn. Expect a round of fixes.

2. **Hardening milestone** (high, plan milestone 10). pluginval at strictness 10,
   a state round-trip test (save, load, compare every parameter and the
   `<Structure>`), CPU profiling at 24 voices x 4 cams x 4 cylinders inside a
   host, tests in Reaper, Bitwig, Live and Logic (AU).

3. **Release workflow** (high, before the first tag). `.github/workflows/release.yml`
   is adapted from Dede's and untested: run it by hand (workflow_dispatch) once,
   check the macOS universal verification can fail, add the README links.

## Decisions to confirm

4. **Source rate** (decision). The plan resamples the file to the session rate on
   import; Camshaft keeps the original rate so that a width in samples means the
   same sound at any session rate (see architecture.md, "Wavetables"). Confirm,
   or switch to resampling and store the width in milliseconds instead.

5. **Dry mute** (decision). Implemented per Turbo: cams sending to that Turbo
   lose their dry signal. The plan lists it under the aux chains without saying
   whose dry path it mutes.

24. **Live reading instead of the table cache** (decision, made 2026-10-09 to
   stop the glitches; confirm). The plan's "When the range moves" section (table
   rebuilds, crossfades, a per-voice cache) no longer applies: see
   architecture.md, "Reading the ranges".

6. **Engine location** (decision). The voice and engine are JUCE-free but live in
   `Source/Engine`, not FxmeTools' `core/synth` as the plan said: they are
   Camshaft-specific, and every reusable piece went into FxmeTools. Confirm.

7. **Parameter count** (decision, low). About 1300 host parameters, most from the
   six effect slots each holding all 13 FxmeFX effects (MechanOdd's design).
   Fewer effect types per slot, or Turbo slots limited to the time-based effects,
   would halve it. Parameter IDs freeze at the first public build.

## Shared code

8. **Effect slots into FxmeFX** (medium, FxmeFX + MechanOdd). `Source/Effects`
   (Effect, EffectFactory, EffectSlot, EffectChain) is MechanOdd's design copied
   and extended (explicit prepare signatures, the four newer effects). It belongs
   in FxmeFX as a shared host layer for MechanOdd, FxmeSampler and Camshaft.
   MechanOdd's tag-dispatched `prepare()` silently passes the channel count as
   the block size to two-argument (rate, block) effects; harmless for the
   effects it embeds today, wrong for Chorus / Flanger / Phaser / Freeze.

9. **GearButton in Dede and MechanOdd** (low, FxmeTools consumers). Both draw a
   local copy of what is now `fxme::GearButton`.

## Engine

10. **CPU** (medium). About 90 % of one core at full load (24 voices x 4 cams x
    4 cylinders, this machine), since ranges are read live from the pyramid,
    blending two octaves. Candidates: read one level when the speed is steady
    and blend only while it moves, linear reads on the upper levels (heavily
    oversampled), a cheaper segment curve than two `exp` per sample in
    `CurveAdsr`, vectorising the cylinder loop, processing cams outside the
    sample loop when no belt link exists.

11. **FM and pyramid levels** (low). The level follows the read speed without
    FM; strong FM on high notes can alias. Raise the level by the FM index when
    a cam is an FM target.

12. **Velocity curve** (low). Velocity is linear everywhere.

13. **Live value for modulator destinations** (low). Knobs show the live value of
    cam fields and global destinations, not of modulator fields (rate, level,
    speed), whose modulated value stays inside the voices.

## GUI

14. **Snap points on the waveform** (medium). The plan draws the snap points
    (zero crossings, detected periods) when zoomed in; only the overlap zone is
    drawn.

15. **Modulated range on the waveform** (medium). Show the newest voice's
    modulated range (dashed) next to the base one.

16. **Dyno curve** (low). The plan's visual design puts a smoothed spectrum behind
    the tachometer.

17. **On-screen keyboard** (low) in the standalone.

## Content

18. **Factory presets by ear** (medium). The nine presets were written without
    listening (`tools/make_factory_presets.py`); tune them and add more per
    category.

19. **More formant voices** (low, FxmeTools). `MultiModeFilter`'s formants are a
    tenor's; add bass, alto, soprano tables.

## Deferred by the plan

20. **Sample-player mode**: attack / loop / release markers, root key. The schema
    reserves them (`<Structure><Cams><Cam mode= attack= loopStart= loopEnd=
    release= rootKey=>`).

21. **Undo / redo**.

22. **Oscilloscope voice selection rules** (which voice a scope row shows; now the
    newest).

23. **MPE** (per-note pitch, pressure, slide).
