# Camshaft: to do

Numbered for reference; numbers are never reused (finished items move to
`done.md` with their number). Priority: high / medium / low. "FxmeTools" or
"FxmeFX" means the item touches the shared submodules.

## Build and verification

2. **Hardening milestone** (high, plan milestone 10). pluginval at strictness 10,
   a state round-trip test (save, load, compare every parameter and the
   `<Structure>`), CPU measured inside a host. Played in Reaper and Ardour
   (2026-10-10; no other host available for now); Bitwig, Live and Logic (AU)
   when someone can.

3. **Release workflow** (high, before the first tag; Olivier is on it). `.github/workflows/release.yml`
   is adapted from Dede's and untested: run it by hand (workflow_dispatch) once,
   check the macOS universal verification can fail, add the README links.

## Decisions to confirm

25. **Ranges by opening and duration** (decision, made 2026-10-10 on request;
   names confirmed, the IDs follow them since schema 3). The plan's centre and
   width (lobe) became `r<n>_opening` ("Opening") and `r<n>_duration`
   ("Duration"): dragging an edge moves that edge
   alone, and a duration past the end of the file is cut short there. Schema 2
   migrates older sessions and presets, approximately (the old centre becomes
   the opening, so a range moves right by half its width: the recording's
   length is not known when the state is read).

7. **Parameter count** (decision, low; question asked 2026-10-10). About 1300 host parameters, most from the
   six effect slots each holding every effect (MechanOdd's design); nine
   effects since 2026-10-11 (thirteen before), which already took a share off.
   Fewer effect types per slot, or Turbo slots limited to the time-based effects,
   would halve it. Parameter IDs freeze at the first public build.

## Shared code

8. **Effect slots into FxmeFX** (medium, FxmeFX + MechanOdd; later). `Source/Effects`
   (Effect, EffectFactory, EffectSlot, EffectChain) is MechanOdd's design copied
   and extended (explicit prepare signatures, the four newer effects). It belongs
   in FxmeFX as a shared host layer for MechanOdd, FxmeSampler and Camshaft.
   MechanOdd's tag-dispatched `prepare()` silently passes the channel count as
   the block size to two-argument (rate, block) effects; harmless for the
   effects it embeds today, wrong for Chorus / Flanger / Phaser / Freeze.

9. **GearButton in Dede and MechanOdd** (low, FxmeTools consumers; later). Both draw a
   local copy of what is now `fxme::GearButton`.

## Engine

10. **CPU** (high). Found high in Reaper (2026-10-10). A first pass took about
    30 % off at the same sound (done.md, 2026-10-10): 8 voices x 4 cams x 4
    cylinders with both filters is now 21 to 23 % of one core of this laptop
    (its clock varies with its heat), the full load (24 voices) 63 to 70 %. What is left, measured: the
    cylinder reads are 70 % of the time, and blending two octaves is a third
    of a read. Options that trade quality (to decide): read the upper octave
    of the blend with linear interpolation (22 % faster; differs from now by
    -47 to -28 dB, most on high notes: a slight dulling at the very top), or
    read one octave without blending (31 % faster; the top of the band then
    steps by up to an octave as the speed changes, and more aliasing passes).
    Eco mode not wanted (2026-10-10). Silent cams are now skipped (done.md).
    Left without a trade-off: SIMD across the cylinders, lazier control
    updates, fixed-point read positions, ending voices whose release has
    fallen below about -100 dB. Measure in a host too (the GUI timers and
    repaints were trimmed in the same pass).
11. **FM and pyramid levels** (low, confirmed 2026-10-10). The level follows the read speed without
    FM; strong FM on high notes can alias. Raise the level by the FM index when
    a cam is an FM target.

12. **Velocity curve** (low, confirmed 2026-10-10). Velocity is linear everywhere.

13. **Live value for modulator destinations** (low, confirmed 2026-10-10). Knobs show the live value of
    cam fields and global destinations, not of modulator fields (rate, level,
    speed), whose modulated value stays inside the voices.

## GUI

14. **Snap points on the waveform** (low, confirmed 2026-10-10). The plan draws the snap points
    (zero crossings, detected periods) when zoomed in; only the overlap zone is
    drawn.

16. **Dyno curve** (low, later: the visualisations will be reworked together;
    the dashboard's circular spectrum may take its place). The plan's visual design puts a smoothed spectrum behind
    the tachometer.

## Content

18. **Factory presets by ear** (medium; with the new formant filter plugin for
    FxmeFX, worked on at the same time). The nine presets were written without
    listening (`tools/make_factory_presets.py`); tune them and add more per
    category.

19. **More formant voices** (low, FxmeTools; after the items above, with the
    formant filter plugin). `MultiModeFilter`'s formants are a
    tenor's; add bass, alto, soprano tables.

## Deferred by the plan

20. **Sample-player mode**: attack / loop / release markers, root key. The schema
    reserves them (`<Structure><Cams><Cam mode= attack= loopStart= loopEnd=
    release= rootKey=>`).

21. **Undo / redo**.

22. **Oscilloscope voice selection rules** (which voice a scope row shows; now the
    newest).

23. **MPE** (per-note pitch, pressure, slide).
