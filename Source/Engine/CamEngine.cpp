/*
  ------------------------------------------------------------------------------
    CamEngine.cpp

    See CamEngine.h.

    Author: Olivier Doaré, github.com/odoare
    Licenced under the GNU LGPL Version 3.0
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#include "CamEngine.h"
#include <FxmeTools/dsp/Lfo.h>
#include <FxmeTools/synth/ModGraph.h>
#include <FxmeTools/util/Math.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace cam
{

CamEngine::CamEngine()
{
    for (auto& c : caches)
        c = std::make_unique<fxme::WavetableCache>();
    for (int c = 0; c < numCams; ++c)
        state.caches[(size_t) c] = caches[(size_t) c].get();

    // Fixed capacity, made once: the editor may be reading them at any time.
    for (auto& s : display.scopes)
        s.prepare (8192);

    for (int i = 0; i < numCcSources; ++i)
        ccNumbers[(size_t) i].store (20 + i);   // CC 20 .. 23 by default

    for (auto& m : display.curvePos)
        for (auto& v : m)
            v.store (-1.0f);

    fxme::WavetableReader::warmUp();
}

CamEngine::~CamEngine()
{
    stopWorker();
}

void CamEngine::stopWorker()
{
    worker.stop();
}

void CamEngine::prepare (double sampleRate, int maxBlock, int cacheSlots)
{
    stopWorker();

    state.sampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    maxBlockSize = std::max (1, maxBlock);

    for (auto& c : caches)
        c->prepare (cacheSlots);

    for (int v = 0; v < maxVoices; ++v)
        voices[(size_t) v].prepare (state, v);
    allocator.reset();

    for (auto& lfo : globalLfos)
        lfo.setSampleRate (state.sampleRate);

    for (auto& s : scopeScratch)
        s.assign ((size_t) maxBlockSize, 0.0f);
    silence.assign ((size_t) maxBlockSize, 0.0f);

    samplesToControl = 0;
    numPlayingVoices = 0;

    std::vector<fxme::WavetableCache*> list;
    for (auto& c : caches)
        list.push_back (c.get());
    worker.start (list);
}

void CamEngine::setSource (std::shared_ptr<fxme::WavetableSource> source)
{
    const int length = source != nullptr ? source->numSamples : 0;
    for (auto& c : caches)
        c->setSource (source);
    sourceLength.store (length, std::memory_order_release);
}

std::shared_ptr<const fxme::WavetableSource> CamEngine::getSource() const
{
    return caches[0]->getSource();
}

void CamEngine::setRoute (int i, int source, int destination, int via) noexcept
{
    if (i < 0 || i >= maxRoutes)
        return;
    if (destination < 0 || source <= 0)
        routes.clear (i);
    else
        routes.set (i, source, destination, via);
}

void CamEngine::setNumGlobalDests (int n)
{
    numGlobalDests = std::max (0, n);
    globalOffsets.assign ((size_t) numGlobalDests, 0.0f);
    globalTouched.assign ((size_t) numGlobalDests, 0);
    touchedGlobals.clear();
    touchedGlobals.reserve ((size_t) maxRoutes);
}

//==============================================================================
void CamEngine::beginBlock (const BlockSettings& s)
{
    allocator.setMode ((fxme::VoiceAllocator::Mode) fxme::jlimit (0, 2, s.voiceMode));
    allocator.setPolyphony (fxme::jlimit (1, maxVoices, s.polyphony));

    state.bpm = s.bpm > 1.0 ? s.bpm : 120.0;
    state.ppq = s.ppq;
    state.playing = s.playing;
    ppqNow = s.ppq;
    state.glideSeconds = s.glideSeconds;
    state.glideLegatoOnly = s.glideLegatoOnly;
    state.pitchBendRange = s.pitchBendRange;
    state.interpolation = (fxme::WavetableReader::Interpolation) fxme::jlimit (0, 2, s.interpolation);
    for (int a = 0; a < numAux; ++a)
        state.auxDryMute[a] = s.auxDryMute[a];

    for (int c = 0; c < numCams; ++c)
    {
        if (s.camValues[c] == nullptr)
            continue;
        for (int f = 0; f < numCamFields; ++f)
        {
            state.camValue[c][f] = s.camValues[c][f];
            state.camNorm[c][f] = camSpecs[f].toNorm (s.camValues[c][f]);
        }
        state.camAudible[c] = state.camValue[c][cfAudible] > 0.5f;
    }

    for (int m = 0; m < numModulators; ++m)
    {
        if (s.modValues[m] == nullptr)
            continue;
        for (int f = 0; f < numModFields; ++f)
        {
            state.modValue[m][f] = s.modValues[m][f];
            state.modNorm[m][f] = modSpecs[f].toNorm (s.modValues[m][f]);
        }
        const int type = (int) state.modValue[m][mfType];
        const bool wasGlobal = state.modIsGlobal[m];
        state.modIsGlobal[m] = (type == 0 && state.modValue[m][mfMode] > 0.5f)
                            || (type == 2 && state.modValue[m][mfDawSync] > 0.5f);
        if (state.modIsGlobal[m] && ! wasGlobal)
            globalLfos[(size_t) m].noteOn();
    }

    for (int m = 0; m < numModulators; ++m)
        sharedCurves[(size_t) m].readIfChanged (state.curves[(size_t) m], curveSeen[(size_t) m]);

    if (s.routeDepths != nullptr)
        updateRoutes (s.routeDepths);
    updateGraph();

    state.sourceGeneration = caches[0]->getGeneration();
    state.sourceLength = sourceLength.load (std::memory_order_acquire);

    // Keep the tables for the unmodulated positions built, so a note starts
    // on the right table and a knob move is picked up with no note held.
    for (int c = 0; c < numCams; ++c)
    {
        caches[(size_t) c]->beginBlock();
        if (! state.camNeeded[c])
            continue;
        const float* v = state.camValue[c];
        const auto key = makeTableKey (state, v[cfCentre], v[cfWidth], v[cfOverlap],
                                       (int) v[cfOverlapShape], (int) v[cfSnap]);
        caches[(size_t) c]->request (key, c);
    }
}

void CamEngine::updateRoutes (const float* depths)
{
    state.camRoutes.clear();
    state.globalRoutes.clear();
    for (auto& r : state.modRoutes)
        r.clear();
    std::memset (state.camFieldModulated, 0, sizeof (state.camFieldModulated));

    for (int i = 0; i < maxRoutes; ++i)
    {
        const auto r = routes.get (i);
        const float depth = depths[i];
        if (r.source <= 0 || r.source >= numSources || r.destination < 0 || isZero (depth))
            continue;

        ActiveRoute a;
        a.source = r.source;
        a.via = (r.via > 0 && r.via < numSources) ? r.via : 0;
        a.depth = depth;
        a.destination = r.destination;

        if (r.destination < numCamDests)
        {
            const int c = r.destination / numCamFields, f = r.destination % numCamFields;
            if (! camSpecs[f].modulatable)
                continue;
            state.camRoutes.add (a);
            state.camFieldModulated[c][f] = true;
        }
        else if (r.destination < globalDestBase)
        {
            const int m = (r.destination - modDestBase) / numModFields;
            const int f = (r.destination - modDestBase) % numModFields;
            if (! modSpecs[f].modulatable)
                continue;
            state.modRoutes[(size_t) m].add (a);
        }
        else if (r.destination - globalDestBase < numGlobalDests)
        {
            state.globalRoutes.add (a);
        }
    }
}

void CamEngine::updateGraph()
{
    fxme::ModGraph graph (numCams);
    for (int s = 0; s < numCams; ++s)
        for (int d = 0; d < numCams; ++d)
        {
            int t = xmod[(size_t) (s * numCams + d)].load (std::memory_order_relaxed);
            if (s == d && t != 1)
                t = 0;               // only FM can feed back on itself
            state.xmodType[s][d] = t;
            if (t != 0)
                graph.connect (s, d);
        }

    if (! graph.topologicalOrder (state.camOrder))
    {
        // A cycle slipped through (the editor refuses them): drop every
        // connection rather than read samples in the wrong order.
        for (int s = 0; s < numCams; ++s)
            for (int d = 0; d < numCams; ++d)
                if (s != d)
                    state.xmodType[s][d] = 0;
        for (int c = 0; c < numCams; ++c)
            state.camOrder[c] = c;
    }

    // A cam is computed when it is heard or feeds (directly or not) one that is.
    for (int c = 0; c < numCams; ++c)
        state.camNeeded[c] = state.camAudible[c];
    for (int pass = 0; pass < numCams; ++pass)
        for (int s = 0; s < numCams; ++s)
            for (int d = 0; d < numCams; ++d)
                if (s != d && state.xmodType[s][d] != 0 && state.camNeeded[d])
                    state.camNeeded[s] = true;
}

//==============================================================================
void CamEngine::applyAction (const fxme::VoiceAllocator::Action& a)
{
    using T = fxme::VoiceAllocator::Action::Type;
    if (a.voice < 0 || a.voice >= maxVoices)
        return;
    auto& v = voices[(size_t) a.voice];

    const bool glideAllowed = state.glideSeconds > 0.0f && lastNote >= 0
                              && (! state.glideLegatoOnly || a.legato);
    const float glideFrom = glideAllowed ? (float) lastNote : -1.0f;

    switch (a.type)
    {
        case T::start:     v.start (a.note, a.velocity, glideFrom); lastNote = a.note; break;
        case T::steal:     v.steal (a.note, a.velocity, glideFrom); lastNote = a.note; break;
        case T::retrigger: v.retrigger (a.note, a.velocity, glideAllowed); lastNote = a.note; break;
        case T::glide:     v.moveTo (a.note); lastNote = a.note; break;
        case T::release:   v.release (a.velocity); break;
    }
}

void CamEngine::noteOn (int note, float velocity)
{
    if (velocity <= 0.0f)
    {
        noteOff (note, 0.0f);
        return;
    }
    fxme::VoiceAllocator::Action actions[fxme::VoiceAllocator::maxActions];
    const int n = allocator.noteOn (note, velocity, actions);
    for (int i = 0; i < n; ++i)
        applyAction (actions[i]);
}

void CamEngine::noteOff (int note, float releaseVelocity)
{
    fxme::VoiceAllocator::Action actions[fxme::VoiceAllocator::maxActions];
    const int n = allocator.noteOff (note, releaseVelocity, actions);
    for (int i = 0; i < n; ++i)
        applyAction (actions[i]);
}

void CamEngine::polyPressure (int note, float value) noexcept
{
    for (auto& v : voices)
        if (v.isActive() && v.getNote() == note)
            v.setPolyPressure (value);
}

void CamEngine::controller (int cc, float value01)
{
    if (cc == 1)
        modWheel = value01;

    if (cc == 64)
    {
        fxme::VoiceAllocator::Action actions[fxme::VoiceAllocator::maxActions];
        const int n = allocator.sustainPedal (value01 >= 0.5f, actions);
        for (int i = 0; i < n; ++i)
            applyAction (actions[i]);
    }

    if (cc == 120 || cc == 123)
        allNotesOff();

    for (int i = 0; i < numCcSources; ++i)
        if (ccNumbers[(size_t) i].load (std::memory_order_relaxed) == cc)
            ccValues[(size_t) i] = value01;
}

void CamEngine::allNotesOff()
{
    fxme::VoiceAllocator::Action actions[fxme::VoiceAllocator::maxActions];
    const int n = allocator.allNotesOff (actions);
    for (int i = 0; i < n; ++i)
        applyAction (actions[i]);
}

//==============================================================================
void CamEngine::updateGlobalModulators()
{
    // Per-voice sources feeding a global modulator come from the newest voice.
    const int newest = allocator.newestVoice();
    const float* sources = (newest >= 0 && voices[(size_t) newest].isActive())
                               ? voices[(size_t) newest].getSourceValues() : state.globalSources;

    const double sr = state.sampleRate;
    for (int m = 0; m < numModulators; ++m)
    {
        if (! state.modIsGlobal[m])
            continue;

        const float* mv = state.modValue[m];
        float off[numModFields] {};
        const auto& rl = state.modRoutes[(size_t) m];
        for (int i = 0; i < rl.size; ++i)
        {
            const auto& r = rl.items[(size_t) i];
            float v = sources[r.source] * r.depth;
            if (r.via > 0)
                v *= sources[r.via];
            off[r.destination - modDest (m, 0)] += v;
        }
        auto value = [&] (int f)
        {
            return isZero (off[f]) ? mv[f] : modSpecs[f].fromNorm (state.modNorm[m][f] + off[f]);
        };

        float out = 0.0f;
        if ((int) mv[mfType] == 0)
        {
            fxme::ModulationLfo::Parameters p;
            p.shape = (int) mv[mfShape];
            p.rateHz = value (mfRate);
            p.synced = mv[mfSync] > 0.5f;
            p.syncBeats = fxme::Lfo::syncDivisionBeats ((int) mv[mfDivision]);
            p.startPhase = value (mfPhase);
            p.bipolar = mv[mfBipolar] > 0.5f;
            auto& lfo = globalLfos[(size_t) m];
            lfo.setBpm (state.bpm);
            lfo.setParameters (p);
            if (state.playing)
            {
                lfo.syncToPpq (ppqNow);
                out = lfo.output();
            }
            else
                out = lfo.advance (controlBlock);
        }
        else
        {
            const double beats = mv[mfDurationMode] > 0.5f ? (double) mv[mfBeats]
                                                           : (double) mv[mfDuration] * state.bpm / 60.0;
            const double speed = (double) value (mfSpeed);
            double& pos = globalCurvePos[(size_t) m];
            if (state.playing)
                pos = ppqNow / std::max (0.01, beats) * speed;
            else
                pos += (double) controlBlock / sr * state.bpm / 60.0 / std::max (0.01, beats) * speed;
            pos -= std::floor (pos);
            out = state.curves[(size_t) m].valueAt ((float) pos);
            if (mv[mfBipolar] > 0.5f)
                out = 2.0f * out - 1.0f;
        }

        state.globalSources[srcModFirst + m] = out * value (mfLevel);
    }
}

void CamEngine::controlUpdate()
{
    pitchBendSmoothed += (pitchBendTarget - pitchBendSmoothed) * 0.35f;

    auto& g = state.globalSources;
    g[srcPitchBend] = pitchBendSmoothed;
    g[srcModWheel] = modWheel;
    g[srcAftertouch] = aftertouch;
    for (int i = 0; i < numCcSources; ++i)
        g[srcCcFirst + i] = ccValues[(size_t) i];

    updateGlobalModulators();

    for (auto& v : voices)
        if (v.isActive())
            v.control();

    ppqNow += (double) controlBlock / state.sampleRate * state.bpm / 60.0;
}

//==============================================================================
void CamEngine::render (const Buses& buses, int offset, int n)
{
    int newest = -1;

    while (n > 0)
    {
        if (samplesToControl <= 0)
        {
            controlUpdate();
            samplesToControl = controlBlock;
        }

        const int k = std::min (n, samplesToControl);

        newest = allocator.newestVoice();
        if (newest >= 0 && ! voices[(size_t) newest].isActive())
            newest = -1;

        const bool canDisplay = offset + k <= maxBlockSize;
        float* disp[numCams];
        for (int c = 0; c < numCams; ++c)
            disp[c] = scopeScratch[c].data();

        CamVoice::Buses vb;
        vb.dry[0] = buses.dry[0];
        vb.dry[1] = buses.dry[1];
        for (int a = 0; a < numAux; ++a)
        {
            vb.aux[a][0] = buses.aux[a][0];
            vb.aux[a][1] = buses.aux[a][1];
        }

        for (int v = 0; v < maxVoices; ++v)
            if (voices[(size_t) v].isActive())
                voices[(size_t) v].render (vb, offset, k, (v == newest && canDisplay) ? disp : nullptr);

        if (canDisplay)
            for (int c = 0; c < numCams; ++c)
            {
                const float* x = newest >= 0 ? scopeScratch[c].data() + offset : silence.data();
                display.scopes[(size_t) c].push (x, x, k);
            }

        // Free the voices that have fallen silent.
        for (int v = 0; v < maxVoices; ++v)
        {
            auto& voice = voices[(size_t) v];
            if (voice.isActive() && voice.hasFinished())
            {
                voice.releaseTables();
                allocator.voiceStopped (v);
            }
        }

        offset += k;
        n -= k;
        samplesToControl -= k;
    }

    if (newest < 0)
    {
        newest = allocator.newestVoice();
        if (newest >= 0 && ! voices[(size_t) newest].isActive())
            newest = -1;
    }
    computeGlobalOffsets (newest);
    publishDisplay (newest);
}

void CamEngine::computeGlobalOffsets (int newest)
{
    for (int g : touchedGlobals)
    {
        globalOffsets[(size_t) g] = 0.0f;
        globalTouched[(size_t) g] = 0;
    }
    touchedGlobals.clear();

    if (state.globalRoutes.size == 0)
        return;

    const float* sources = newest >= 0 ? voices[(size_t) newest].getSourceValues() : state.globalSources;
    for (int i = 0; i < state.globalRoutes.size; ++i)
    {
        const auto& r = state.globalRoutes.items[(size_t) i];
        const int g = r.destination - globalDestBase;
        float v = sources[r.source] * r.depth;
        if (r.via > 0)
            v *= sources[r.via];
        globalOffsets[(size_t) g] += v;
        if (! globalTouched[(size_t) g])
        {
            globalTouched[(size_t) g] = 1;
            touchedGlobals.push_back (g);   // capacity reserved for maxRoutes
        }
    }
}

void CamEngine::publishDisplay (int newest)
{
    int count = 0;
    for (const auto& v : voices)
        count += v.isActive() ? 1 : 0;
    display.voicesActive.store (count, std::memory_order_relaxed);

    if (newest >= 0)
    {
        const auto& v = voices[(size_t) newest];
        for (int c = 0; c < numCams; ++c)
        {
            display.phase[(size_t) c].store ((float) v.getCamPhase (c), std::memory_order_relaxed);
            display.active[(size_t) c].store (state.camNeeded[c], std::memory_order_relaxed);
            for (int f = 0; f < numCamFields; ++f)
                display.camValue[(size_t) c][(size_t) f].store (camSpecs[f].toNorm (v.getCamValue (c, f)),
                                                                std::memory_order_relaxed);
        }
        const float* s = v.getSourceValues();
        for (int i = 0; i < numSources; ++i)
            display.source[(size_t) i].store (s[i], std::memory_order_relaxed);
    }
    else
    {
        for (int c = 0; c < numCams; ++c)
        {
            display.active[(size_t) c].store (false, std::memory_order_relaxed);
            for (int f = 0; f < numCamFields; ++f)
                display.camValue[(size_t) c][(size_t) f].store (state.camNorm[c][f], std::memory_order_relaxed);
        }
        for (int i = 0; i < numSources; ++i)
            display.source[(size_t) i].store (state.globalSources[i], std::memory_order_relaxed);
    }

    for (int m = 0; m < numModulators; ++m)
    {
        auto& row = display.curvePos[(size_t) m];
        if (state.modIsGlobal[m] && (int) state.modValue[m][mfType] == 2)
        {
            row[0].store ((float) globalCurvePos[(size_t) m], std::memory_order_relaxed);
            for (int v = 1; v < maxVoices; ++v)
                row[(size_t) v].store (-1.0f, std::memory_order_relaxed);
        }
        else
            for (int v = 0; v < maxVoices; ++v)
                row[(size_t) v].store (voices[(size_t) v].getCurvePosition (m), std::memory_order_relaxed);
    }
}

} // namespace cam
