/*
  ------------------------------------------------------------------------------
    CamEngine.h

    Camshaft's sound engine, independent of any plugin framework: the voices,
    the voice allocation (poly / mono / legato, glide), the modulation
    matrix and the global modulators, the cross-modulation graph, and the
    hand-over of the audio the cams read (EngineSource).

    The plugin shell drives it each audio block:

        message thread:  setSource(), setRoute(), setXmod(), curve(m).write(),
                         setCcNumber()            (lock-free hand-overs)
        audio thread:    beginBlock (settings)    base values, routes, graph
                         noteOn / noteOff / ...   between render() calls, at
                                                  each MIDI event's position
                         render (buses, n)        accumulates into the buses
                         globalDestOffset (g)     after render(): modulation
                                                  of the shell's parameters

    Display data (every voice's live ranges, the newest voice's values, the
    modulators in use, curve playheads, the per-cam scope taps) is published
    through atomics and lock-free taps.

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
#include <mutex>
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

    /** Allocates everything for `sampleRate` and up to `maxBlock` samples.
        Not concurrently with render(). */
    void prepare (double sampleRate, int maxBlock);

    //==========================================================================
    // Any thread but the audio thread

    /** Replaces the audio the cams read. The audio thread picks it up at its
        next block; the previous source is freed once it no longer reads it. */
    void setSource (std::shared_ptr<const EngineSource> source);

    /** The recording currently handed to the engine (for displays). */
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

    /** Silences every voice at once (no release) and forgets the held
        notes and the pedal: the dashboard's hazard button. */
    void panic();

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

    /** One voice's range on one cam, as it is read now (modulation and
        snapping applied, smoothing included), in source samples; `phase`
        is the first cylinder's position in the cycle, negative when the
        voice is not playing this cam. The fields are stored one by one: a
        reader may see them from two neighbouring blocks, harmless for a
        display. */
    struct VoiceRange
    {
        std::atomic<float> start { 0.0f }, cycle { 0.0f }, overlap { 0.0f }, phase { -1.0f };
    };

    struct Display
    {
        std::array<std::atomic<bool>, numCams> active {};
        std::array<std::array<VoiceRange, numCams>, maxVoices> ranges;
        std::atomic<int> newestVoice { -1 };
        std::array<std::atomic<bool>, numModulators> modUsed {};
        std::array<std::array<std::atomic<float>, numCamFields>, numCams> camValue {};
        std::array<std::atomic<float>, numSources> source {};
        std::array<std::array<std::atomic<float>, maxVoices>, numModulators> curvePos {};
        std::array<fxme::StereoTap, numCams> scopes;
        std::atomic<int> voicesActive { 0 };
        std::atomic<int> polyphony { 8 };
        std::atomic<int> steals { 0 };          // counts up at each stolen voice
        std::atomic<int> noteOns { 0 };         // counts up at each note received
        std::atomic<bool> sustainDown { false };
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
    void updateUsage();
    void computeGlobalOffsets (int newest);
    void publishDisplay (int newest);
    double beatsPerSample() const noexcept { return state.bpm / 60.0 / state.sampleRate; }

    EngineState state;
    std::array<CamVoice, maxVoices> voices;
    fxme::VoiceAllocator allocator;

    // The source: owned here, published to the audio thread through an
    // atomic pointer; `inUse` is the audio thread's hazard pointer, so a
    // replaced source is only freed once the audio thread has moved on.
    void freeRetiredSources();
    mutable std::mutex sourceLock;                         // owners only, never the audio thread
    std::vector<std::shared_ptr<const EngineSource>> ownedSources;
    std::atomic<const EngineSource*> published { nullptr };
    std::atomic<const EngineSource*> inUse { nullptr };

    // Hand-overs from the message thread.
    fxme::RouteSet<maxRoutes> routes;
    std::array<std::atomic<int>, numCams * numCams> xmod {};
    std::array<fxme::SharedCurveShape, numModulators> sharedCurves;
    std::array<std::uint32_t, numModulators> curveSeen {};
    std::array<std::atomic<int>, numCcSources> ccNumbers {};

    // Global modulators (free-running LFOs, curves on the host timeline).
    std::array<fxme::ModulationLfo, numModulators> globalLfos;
    std::array<double, numModulators> globalCurveElapsed {};    // in curve lengths
    std::array<fxme::CurvePlayer, numModulators> globalCurves;
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
