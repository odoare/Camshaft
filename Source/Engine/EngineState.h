/*
  ------------------------------------------------------------------------------
    EngineState.h

    What every voice reads from the engine during a control block: the base
    (unmodulated) values of the cams' and modulators' parameters, the active
    modulation routes sorted by destination, the global source values, the
    cross-modulation graph, the curve shapes and the audio source. Written by
    CamEngine on the audio thread at the start of each block or control
    block, read by the voices right after; nothing here is shared with
    another thread.

    Author: Olivier Doaré, github.com/odoare
    Licenced under the GNU LGPL Version 3.0
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

#include "CamParams.h"
#include <FxmeTools/synth/BreakpointCurve.h>
#include <FxmeTools/synth/ModGraph.h>
#include <FxmeTools/synth/ModMatrix.h>
#include "EngineSource.h"
#include <FxmeTools/synth/WavetableReader.h>
#include <array>

namespace cam
{

/** A route with its depth resolved for this block. */
struct ActiveRoute
{
    int source = 0, via = 0;
    float depth = 0.0f;
    int destination = 0;       // flat destination index (CamParams.h)
};

/** Up to maxRoutes routes, fixed storage. */
struct RouteList
{
    std::array<ActiveRoute, maxRoutes> items {};
    int size = 0;

    void clear() noexcept { size = 0; }
    void add (const ActiveRoute& r) noexcept { if (size < maxRoutes) items[(size_t) size++] = r; }
};

struct EngineState
{
    double sampleRate = 44100.0;
    double bpm = 120.0, ppq = 0.0;
    bool playing = false;

    fxme::WavetableReader::Interpolation interpolation = fxme::WavetableReader::Interpolation::cubic;

    // Base values of every cam field, in value units and normalised.
    float camValue[numCams][numCamFields] {};
    float camNorm[numCams][numCamFields] {};

    // Base values of every modulator field.
    float modValue[numModulators][numModFields] {};
    float modNorm[numModulators][numModFields] {};
    bool modIsGlobal[numModulators] {};

    // Routes, split by destination kind.
    RouteList camRoutes;                         // destination < numCamDests
    std::array<RouteList, numModulators> modRoutes;   // to modulator m's fields
    RouteList globalRoutes;                      // to the shell's global destinations
    bool camFieldModulated[numCams][numCamFields] {};

    // Source values that are the same for every voice (pitch bend, wheel,
    // channel aftertouch, CCs, global modulators). Per-voice entries are
    // overwritten by each voice in its own copy.
    float globalSources[numSources] {};

    // Cross-modulation: type of src -> dst (0 off, 1 FM, 2 ring, 3 AM).
    int xmodType[numCams][numCams] {};
    int camOrder[numCams] { 0, 1, 2, 3 };
    bool camNeeded[numCams] {};
    bool camAudible[numCams] {};

    // Drawn curves, one per modulator.
    std::array<fxme::CurveShape, numModulators> curves;

    // Pitch and glide.
    float pitchBendRange = 2.0f;                 // semitones
    float glideSeconds = 0.0f;
    bool glideLegatoOnly = false;

    // The audio the cams read (valid for the block; see CamEngine::setSource).
    const EngineSource* source = nullptr;

    bool auxDryMute[numAux] {};
};

/** FM index (radians) at full lift. */
constexpr float fmIndexAtFullLift = 8.0f;

/** Time constant of the smoothing of a cam's range (start, cycle, overlap)
    on top of the per-sample ramps: absorbs stepped automation and the jumps
    of a snapped range. */
constexpr double rangeSmoothingSeconds = 0.004;

} // namespace cam
