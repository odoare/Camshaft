/*
  ------------------------------------------------------------------------------
    CamVoice.h

    One note of Camshaft: the four cams (each a wavetable oscillator with up
    to four cylinders, two filters, its amp envelope, its gain, pan and
    sends), their cross-modulation, and the voice's own modulators.

    Per control block (cam::controlBlock samples) control() evaluates the
    voice's modulators and the matrix and turns the result into oscillator
    increments, filter settings and gain ramps; render() then runs the
    samples, ramping every gain linearly over the block.

    The oscillator reads the cam's range straight from the source pyramid
    (fxme::RangeLoop over fxme::MipmappedBuffer): start, cycle length and
    overlap are smoothed and ramped per sample, so moving a range is a
    continuous scan, and the pyramid level follows the read speed (cycle
    length x frequency), blended between octaves, so reading stays
    band-limited at any width and pitch.

    Signal order per cam: oscillator (FM inside the read) -> ring / AM ->
    filters -> amp envelope -> volume -> pan. The send taps are pre-filter
    (oscillator x envelope), post-filter (x envelope) or post-gain; every tap
    is panned like the dry signal.

    JUCE-free. Realtime safe once prepared.

    Author: Olivier Doaré, github.com/odoare
    Licenced under the GNU LGPL Version 3.0
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

#include "EngineState.h"
#include <FxmeTools/dsp/MultiModeFilter.h>
#include <FxmeTools/synth/CurveAdsr.h>
#include <FxmeTools/synth/ModulationLfo.h>
#include <FxmeTools/util/Random.h>

namespace cam
{

class CamVoice
{
public:
    /** Output buses: dry L/R, aux 1 L/R, aux 2 L/R (accumulated into). */
    struct Buses
    {
        float* dry[2];
        float* aux[numAux][2];
    };

    void prepare (const EngineState& state, int voiceIndex);

    //==========================================================================
    // Note control (from the engine's allocator actions)

    /** Fresh start. `glideFrom` is the pitch to glide from (negative: none). */
    void start (int note, float velocity, float glideFrom);

    /** Same voice, new note: envelopes and per-voice modulators restart from
        where they are (mono), or only the pitch moves (legato glide). */
    void retrigger (int note, float velocity, bool glide);
    void moveTo (int note);                       // legato: pitch only

    void release (float releaseVelocity);

    /** Fades out over a few milliseconds, then starts the pending note. */
    void steal (int note, float velocity, float glideFrom);

    void setPolyPressure (float pressure) noexcept { polyPressure = pressure; }

    bool isActive() const noexcept { return active; }
    int getNote() const noexcept { return note; }
    float getPitch() const noexcept { return pitch; }

    /** True once silent: every amp envelope done, no curve release playing,
        no fade in progress. The engine then frees the voice. */
    bool hasFinished() const noexcept;

    /** Silences the voice at once (the engine frees it). */
    void stop() noexcept { active = false; }

    //==========================================================================
    /** Modulation and derived settings for the next control block. */
    void control();

    /** Renders `n` samples (at most controlBlock) into the buses at
        `offset`. `display`, when not null, receives this voice's per-cam
        signal for the scopes. */
    void render (const Buses& out, int offset, int n, float* const* display);

    //==========================================================================
    // For the displays (read by the engine for the newest voice).
    const float* getSourceValues() const noexcept { return src; }
    float getCamValue (int c, int field) const noexcept { return cams[c].cur[field]; }
    /** Phase of the first cylinder in the original cycle (the table's
        phase alignment undone), for the waveform's playhead. */
    double getCamPhase (int c) const noexcept;
    float getCurvePosition (int m) const noexcept;

private:
    struct Cam
    {
        fxme::CurveAdsr env;
        float cur[numCamFields] {};

        // The range read, in source samples: smoothed targets, and the
        // values ramped per sample towards them over each control block.
        double start = 0.0, cycle = 2048.0, overlap = 0.0;
        double startStep = 0.0, cycleStep = 0.0, overlapStep = 0.0;
        double smoothStart = 0.0, smoothCycle = 2048.0, smoothOverlap = 0.0;
        bool rangeValid = false, equalPower = true;
        const EngineSource* sourceSeen = nullptr;

        // Cylinders.
        int cylinders = 1;
        double phase[maxCylinders] {};
        double inc[maxCylinders] {}, incStep[maxCylinders] {};
        float level[maxCylinders] {}, levelStep[maxCylinders] {};   // pyramid level, fractional
        float dcX[2] {}, dcY[2] {};                                  // DC blocker state
        float cylGainL[maxCylinders] {}, cylGainR[maxCylinders] {};
        float unisonNorm = 1.0f;

        fxme::MultiModeFilter filters[2][2];       // [filter][channel]
        bool filterOn[2] {};
        bool parallel = false;
        float balanceA = 1.0f, balanceB = 0.0f;

        // Ramped gains: value, step per sample.
        float gain = 0.0f, gainStep = 0.0f;
        float panL = 1.0f, panLStep = 0.0f, panR = 1.0f, panRStep = 0.0f;
        float send[numAux] {}, sendStep[numAux] {};
        int tap[numAux] {};
        bool dryMuted = false;
        float lift[numCams] {}, liftStep[numCams] {};

        float out0 = 0.0f;           // first cylinder's oscillator output, for cross-modulation
        float velocityGain = 1.0f;
    };

    void startModulators (bool retrig);
    void updateModulators();
    void applyCamModulation();
    void updateCam (int c, bool snap);
    void updateRange (int c, bool snap);
    float stealGain() noexcept;

    const EngineState* state = nullptr;
    int index = 0;

    bool active = false, gated = false;
    int note = 60;
    float velocity = 0.0f, releaseVelocity = 0.0f, polyPressure = 0.0f, randomValue = 0.0f;

    // Glide.
    float pitch = 60.0f, glideFrom = 60.0f, glideTo = 60.0f, glidePos = 1.0f;

    // Stealing.
    bool stealing = false;
    float stealLevel = 1.0f, stealStep = 0.0f;
    int pendingNote = 60;
    float pendingVelocity = 0.0f, pendingGlideFrom = -1.0f;

    bool snapNext = true;            // first control block after a start: no ramps

    // Modulators.
    fxme::ModulationLfo lfos[numModulators];
    fxme::CurveAdsr adsrs[numModulators];
    fxme::CurvePlayer curves[numModulators];
    float src[numSources] {};

    Cam cams[numCams];
    fxme::Random random;
};

} // namespace cam
