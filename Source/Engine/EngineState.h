/*
  ------------------------------------------------------------------------------
    EngineState.h

    What every voice reads from the engine during a control block: the base
    (unmodulated) values of the cams' and modulators' parameters, the active
    modulation routes sorted by destination, the global source values, the
    cross-modulation graph, the curve shapes and the table caches. Written by
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
#include <FxmeTools/synth/WavetableCache.h>
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

    // Tables.
    std::array<fxme::WavetableCache*, numCams> caches {};
    int sourceGeneration = 0;
    int sourceLength = 0;

    bool auxDryMute[numAux] {};
};

/** FM index (radians) at full lift. */
constexpr float fmIndexAtFullLift = 8.0f;

/** Samples of the crossfade between two table sets. */
constexpr int tableCrossfadeSamples = 256;

/** Quantises a cam's position into a cache key (see doc/architecture.md,
    "Moving ranges"): width on a 1/96-octave grid, centre on a grid of a
    64th of the width, overlap in steps of 0.5 %. */
inline fxme::WavetableCache::Key makeTableKey (const EngineState& s, float centreNorm, float width,
                                               float overlapPercent, int overlapShape, int snap) noexcept
{
    const int n = s.sourceLength;
    if (n < 16)
        return {};
    double len = width < 16.0f ? 16.0 : (double) width;
    len = std::pow (2.0, std::round (std::log2 (len) * 96.0) / 96.0);
    int lenQ = (int) std::lround (len);
    lenQ = lenQ < 16 ? 16 : (lenQ > n ? n : lenQ);
    const int step = lenQ / 64 > 1 ? lenQ / 64 : 1;
    const double centre = (double) (centreNorm < 0.0f ? 0.0f : (centreNorm > 1.0f ? 1.0f : centreNorm)) * (double) n;
    const int centreQ = (int) std::lround (centre / (double) step) * step;
    const float overlap = std::round (overlapPercent * 0.01f * 200.0f) / 200.0f;
    return fxme::WavetableCache::makeKey (s.sourceGeneration, centreQ, lenQ, overlap, overlapShape, snap);
}

} // namespace cam
