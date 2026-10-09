/*
  ------------------------------------------------------------------------------
    CamEngine.h

    Camshaft's sound engine, independent of any plugin framework: the voices,
    the voice allocation (poly / mono / legato, glide), the modulation
    matrix and the global modulators, the cross-modulation graph, and the
    per-cam table caches with their worker thread.

    The plugin shell drives it each audio block:

        message thread:  setSource(), setRoute(), setXmod(), curve(m).write(),
                         setCcNumber()            (lock-free hand-overs)
        audio thread:    beginBlock (settings)    base values, routes, graph
                         noteOn / noteOff / ...   between render() calls, at
                                                  each MIDI event's position
                         render (buses, n)        accumulates into the buses
                         globalDestOffset (g)     after render(): modulation
                                                  of the shell's parameters

    Display data (the newest voice's phases, live values, curve playheads,
    the per-cam scope taps) is published through atomics and lock-free taps.

    Author: Olivier Doaré, github.com/odoare
    Licenced under the GNU LGPL Version 3.0
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

#include "CamVoice.h"
#include "EngineState.h"
#include <FxmeTools/dsp/StereoTap.h>
#include <FxmeTools/synth/BreakpointCurve.h>
#include <FxmeTools/synth/ModulationLfo.h>
#include <FxmeTools/synth/VoiceAllocator.h>
#include <FxmeTools/synth/WavetableCache.h>
#include <array>
#include <atomic>
#include <memory>
#include <vector>

namespace cam
{

class CamEngine
{
public:
    /** Settings read by the shell from its parameters, once per block. */
    struct BlockSettings
    {
        int polyphony = 8;
        int voiceMode = 0;            // voiceModes
        float glideSeconds = 0.0f;
        bool glideLegatoOnly = false;
        float pitchBendRange = 2.0f;
        int interpolation = 1;
        double bpm = 120.0, ppq = 0.0;
        bool playing = false;
        bool auxDryMute[numAux] {};
        const float* camValues[numCams] {};   // numCamFields values each
        const float* modValues[numModulators] {};
        const float* routeDepths = nullptr;   // maxRoutes
    };

    CamEngine();
    ~CamEngine();

    /** Allocates everything for `sampleRate` and up to `maxBlock` samples,
        with `cacheSlots` table sets per cam. Stops and restarts the table
        worker. Not concurrently with render(). */
    void prepare (double sampleRate, int maxBlock, int cacheSlots);
    void stopWorker();

    //==========================================================================
    // Message thread

    /** Replaces the audio the cams are cut from. Voices crossfade to the new
        tables as they are built. */
    void setSource (std::shared_ptr<fxme::WavetableSource> source);
    std::shared_ptr<const fxme::WavetableSource> getSource() const;

    /** Route i: source, destination (flat index), via. Depth comes with each
        block (an automatable parameter). destination < 0 clears it. */
    void setRoute (int i, int source, int destination, int via) noexcept;

    /** Cross-modulation from cam `src` to cam `dst` (0 off, 1 FM, 2 ring,
        3 AM). The caller keeps the graph acyclic (fxme::ModGraph). */
    void setXmod (int src, int dst, int type) noexcept { xmod[(size_t) (src * numCams + dst)].store (type); }
    int getXmod (int src, int dst) const noexcept    { return xmod[(size_t) (src * numCams + dst)].load(); }

    fxme::SharedCurveShape& curve (int m) noexcept { return sharedCurves[(size_t) m]; }

    void setCcNumber (int i, int cc) noexcept { ccNumbers[(size_t) i].store (cc); }
    int getCcNumber (int i) const noexcept    { return ccNumbers[(size_t) i].load(); }

    /** Number of global destinations the shell resolves routes to (their
        flat index is globalDestBase + g). */
    void setNumGlobalDests (int n);

    //==========================================================================
    // Audio thread

    void beginBlock (const BlockSettings& settings);

    void noteOn (int note, float velocity);
    void noteOff (int note, float releaseVelocity);
    void pitchBend (float value) noexcept          { pitchBendTarget = value; }      // -1 .. 1
    void channelPressure (float value) noexcept    { aftertouch = value; }
    void polyPressure (int note, float value) noexcept;
    void controller (int cc, float value01);
    void allNotesOff();

    struct Buses
    {
        float* dry[2];
        float* aux[numAux][2];
    };

    /** Accumulates `n` samples into the buses, starting at `offset`. */
    void render (const Buses& buses, int offset, int n);

    /** After render(): the modulation of global destination g, in its
        normalised range, from the last control block. */
    float globalDestOffset (int g) const noexcept
    {
        return g >= 0 && g < (int) globalOffsets.size() ? globalOffsets[(size_t) g] : 0.0f;
    }
    bool isGlobalDestModulated (int g) const noexcept
    {
        return g >= 0 && g < (int) globalTouched.size() && globalTouched[(size_t) g] != 0;
    }

    //==========================================================================
    // Displays (any thread)

    struct Display
    {
        std::array<std::atomic<float>, numCams> phase {};
        std::array<std::atomic<bool>, numCams> active {};
        std::array<std::array<std::atomic<float>, numCamFields>, numCams> camValue {};
        std::array<std::atomic<float>, numSources> source {};
        std::array<std::array<std::atomic<float>, maxVoices>, numModulators> curvePos {};
        std::array<fxme::StereoTap, numCams> scopes;
        std::atomic<int> voicesActive { 0 };
    };

    Display& getDisplay() noexcept { return display; }
    const Display& getDisplay() const noexcept { return display; }

    double getSampleRate() const noexcept { return state.sampleRate; }

private:
    void controlUpdate();
    void updateGlobalModulators();
    void applyAction (const fxme::VoiceAllocator::Action& a);
    void updateRoutes (const float* depths);
    void updateGraph();
    void computeGlobalOffsets (int newest);
    void publishDisplay (int newest);

    EngineState state;
    std::array<CamVoice, maxVoices> voices;
    fxme::VoiceAllocator allocator;

    std::array<std::unique_ptr<fxme::WavetableCache>, numCams> caches;
    fxme::WavetableWorker worker;
    std::atomic<int> sourceLength { 0 };

    // Hand-overs from the message thread.
    fxme::RouteSet<maxRoutes> routes;
    std::array<std::atomic<int>, numCams * numCams> xmod {};
    std::array<fxme::SharedCurveShape, numModulators> sharedCurves;
    std::array<std::uint32_t, numModulators> curveSeen {};
    std::array<std::atomic<int>, numCcSources> ccNumbers {};

    // Global modulators (free-running LFOs, curves on the host timeline).
    std::array<fxme::ModulationLfo, numModulators> globalLfos;
    std::array<double, numModulators> globalCurvePos {};
    double ppqNow = 0.0;

    // Global destinations.
    int numGlobalDests = 0;
    std::vector<float> globalOffsets;
    std::vector<char> globalTouched;

    // MIDI state.
    float pitchBendTarget = 0.0f, pitchBendSmoothed = 0.0f;
    float aftertouch = 0.0f, modWheel = 0.0f;
    std::array<float, numCcSources> ccValues {};
    int lastNote = -1;
    int samplesToControl = 0;
    int numPlayingVoices = 0;

    std::vector<float> scopeScratch[numCams];
    std::vector<float> silence;
    std::vector<int> touchedGlobals;
    int maxBlockSize = 512;

    Display display;
};

} // namespace cam
