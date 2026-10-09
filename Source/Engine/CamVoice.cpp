/*
  ------------------------------------------------------------------------------
    CamVoice.cpp

    See CamVoice.h.

    Author: Olivier Doaré, github.com/odoare
    Licenced under the GNU LGPL Version 3.0
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#include "CamVoice.h"
#include <FxmeTools/dsp/Lfo.h>
#include <FxmeTools/dsp/UnisonSpread.h>
#include <FxmeTools/util/Math.h>
#include <cmath>
#include <cstring>

namespace cam
{

namespace
{
    constexpr double twoPi = 6.283185307179586;

    /** Equal-power pan / balance, unity in the centre: p in [-1, 1]. */
    inline void panGains (float p, float& l, float& r) noexcept
    {
        p = fxme::jlimit (-1.0f, 1.0f, p);
        const float theta = (p + 1.0f) * 0.25f * fxme::MathConstants<float>::pi;
        l = 1.41421356f * std::cos (theta);
        r = 1.41421356f * std::sin (theta);
    }

    inline float dbToGainOrZero (float db) noexcept
    {
        return db <= -59.9f ? 0.0f : std::pow (10.0f, db * 0.05f);
    }
}

//==============================================================================
void CamVoice::prepare (const EngineState& s, int voiceIndex)
{
    state = &s;
    index = voiceIndex;
    active = gated = stealing = false;

    for (auto& k : cams)
    {
        k.env.setSampleRate (s.sampleRate);
        k.env.reset();
        for (auto& f : k.filters)
            for (auto& ch : f)
                ch.prepare (s.sampleRate);
        k.slot = k.oldSlot = -1;
        k.key = k.wanted = {};
        k.setFade = 1.0f;
        k.out0 = 0.0f;
    }

    for (int m = 0; m < numModulators; ++m)
    {
        lfos[m].setSampleRate (s.sampleRate);
        adsrs[m].setSampleRate (s.sampleRate);
        adsrs[m].reset();
    }
}

//==============================================================================
void CamVoice::start (int newNote, float newVelocity, float glideFromPitch)
{
    const bool wasActive = active;
    active = true;
    gated = true;
    stealing = false;
    note = newNote;
    velocity = newVelocity;
    releaseVelocity = 0.0f;
    polyPressure = 0.0f;
    randomValue = random.nextBipolar();

    glideTo = (float) newNote;
    if (glideFromPitch >= 0.0f && state->glideSeconds > 0.0f)
    {
        glideFrom = glideFromPitch;
        glidePos = 0.0f;
        pitch = glideFrom;
    }
    else
    {
        pitch = glideTo;
        glidePos = 1.0f;
    }

    for (int c = 0; c < numCams; ++c)
    {
        auto& k = cams[c];
        k.env.reset();
        k.env.noteOn();
        const bool randomPhase = state->camValue[c][cfRandomPhase] > 0.5f;
        for (int u = 0; u < maxCylinders; ++u)
            k.phase[u] = randomPhase ? (double) random.nextFloat() : 0.0;
        for (auto& f : k.filters)
            for (auto& ch : f)
                ch.reset();
        k.out0 = 0.0f;
        k.cylinders = 0;    // re-initialised by updateCam
        if (! wasActive)
        {
            k.slot = k.oldSlot = -1;
            k.setFade = 1.0f;
            k.key = {};
        }
    }

    for (auto& a : adsrs)
        a.reset();
    startModulators (false);

    snapNext = true;
    control();
}

void CamVoice::retrigger (int newNote, float newVelocity, bool glide)
{
    note = newNote;
    velocity = newVelocity;
    gated = true;
    glideTo = (float) newNote;
    if (glide && state->glideSeconds > 0.0f)
    {
        glideFrom = pitch;
        glidePos = 0.0f;
    }
    else
    {
        pitch = glideTo;
        glidePos = 1.0f;
    }

    for (auto& k : cams)
        k.env.noteOn();
    startModulators (true);
}

void CamVoice::moveTo (int newNote)
{
    note = newNote;
    glideTo = (float) newNote;
    if (state->glideSeconds > 0.0f)
    {
        glideFrom = pitch;
        glidePos = 0.0f;
    }
    else
    {
        pitch = glideTo;
        glidePos = 1.0f;
    }
}

void CamVoice::release (float relVelocity)
{
    if (! active)
        return;
    gated = false;
    releaseVelocity = relVelocity;
    for (auto& k : cams)
        k.env.noteOff();
    for (int m = 0; m < numModulators; ++m)
    {
        adsrs[m].noteOff();
        curves[m].noteOff (state->curves[(size_t) m]);
    }
}

void CamVoice::steal (int newNote, float newVelocity, float glideFromPitch)
{
    if (! active)
    {
        start (newNote, newVelocity, glideFromPitch);
        return;
    }
    stealing = true;
    stealLevel = 1.0f;
    stealStep = (float) (1.0 / (0.005 * state->sampleRate));
    pendingNote = newNote;
    pendingVelocity = newVelocity;
    pendingGlideFrom = glideFromPitch;
}

bool CamVoice::hasFinished() const noexcept
{
    if (! active || stealing)
        return false;
    for (const auto& k : cams)
        if (k.env.isActive())
            return false;
    for (int m = 0; m < numModulators; ++m)
        if ((int) state->modValue[m][mfType] == 2 && ! state->modIsGlobal[m] && curves[m].isReleasing())
            return false;
    return true;
}

void CamVoice::releaseTables() noexcept
{
    for (int c = 0; c < numCams; ++c)
    {
        auto& k = cams[c];
        auto* cache = state->caches[(size_t) c];
        if (cache != nullptr)
        {
            if (k.slot >= 0)    cache->release (k.slot);
            if (k.oldSlot >= 0) cache->release (k.oldSlot);
        }
        k.slot = k.oldSlot = -1;
        k.key = {};
        k.setFade = 1.0f;
    }
    active = false;
}

float CamVoice::getCurvePosition (int m) const noexcept
{
    if (! active || (int) state->modValue[m][mfType] != 2 || state->modIsGlobal[m])
        return -1.0f;
    return (float) curves[m].getPosition();
}

//==============================================================================
void CamVoice::startModulators (bool retrig)
{
    for (int m = 0; m < numModulators; ++m)
    {
        if (state->modIsGlobal[m])
            continue;
        lfos[m].noteOn();
        adsrs[m].noteOn();
        curves[m].noteOn();
        (void) retrig;
    }
}

void CamVoice::updateModulators()
{
    std::memcpy (src, state->globalSources, sizeof (src));
    src[srcVelocity] = velocity;
    src[srcReleaseVelocity] = releaseVelocity;
    src[srcKeyTrack] = ((float) note - 60.0f) / 60.0f;
    src[srcPolyAftertouch] = polyPressure;
    for (int c = 0; c < numCams; ++c)
        src[srcAmpEnvFirst + c] = cams[c].env.getValue();
    src[srcRandom] = randomValue;

    const double sr = state->sampleRate;
    for (int m = 0; m < numModulators; ++m)
    {
        if (state->modIsGlobal[m])
            continue;   // the engine evaluated it; its value is already in src

        const float* mv = state->modValue[m];
        float off[numModFields] {};
        bool touched = false;
        const auto& routes = state->modRoutes[(size_t) m];
        for (int i = 0; i < routes.size; ++i)
        {
            const auto& r = routes.items[(size_t) i];
            float v = src[r.source] * r.depth;
            if (r.via > 0)
                v *= src[r.via];
            off[r.destination - modDest (m, 0)] += v;
            touched = true;
        }
        auto value = [&] (int f)
        {
            return (! touched || isZero (off[f])) ? mv[f]
                                                 : modSpecs[f].fromNorm (state->modNorm[m][f] + off[f]);
        };

        float out = 0.0f;
        const int type = (int) mv[mfType];
        if (type == 0)
        {
            fxme::ModulationLfo::Parameters p;
            p.shape = (int) mv[mfShape];
            p.rateHz = value (mfRate);
            p.synced = mv[mfSync] > 0.5f;
            p.syncBeats = fxme::Lfo::syncDivisionBeats ((int) mv[mfDivision]);
            p.startPhase = value (mfPhase);
            p.delaySeconds = mv[mfDelay] * 0.001f;
            p.fadeSeconds = mv[mfFade] * 0.001f;
            p.bipolar = mv[mfBipolar] > 0.5f;
            lfos[m].setBpm (state->bpm);
            lfos[m].setParameters (p);
            out = lfos[m].advance (controlBlock);
        }
        else if (type == 1)
        {
            fxme::CurveAdsr::Parameters p;
            p.attack = value (mfAttack) * 0.001f;
            p.decay = value (mfDecay) * 0.001f;
            p.sustain = value (mfSustain);
            p.release = value (mfRelease) * 0.001f;
            p.attackCurve = mv[mfAttackCurve];
            p.decayCurve = mv[mfDecayCurve];
            p.releaseCurve = mv[mfReleaseCurve];
            adsrs[m].setParameters (p);
            const float velAmount = mv[mfVelocity];
            out = adsrs[m].advance (controlBlock) * (1.0f - velAmount + velAmount * velocity);
        }
        else
        {
            const double duration = mv[mfDurationMode] > 0.5f ? (double) mv[mfBeats] * 60.0 / state->bpm
                                                              : (double) mv[mfDuration];
            const double dt = (double) controlBlock / sr / fxme::jmax (0.001, duration) * (double) value (mfSpeed);
            out = curves[m].advance (state->curves[(size_t) m], dt);
            if (mv[mfBipolar] > 0.5f)
                out = 2.0f * out - 1.0f;
        }

        src[srcModFirst + m] = out * value (mfLevel);
    }
}

void CamVoice::applyCamModulation()
{
    for (int c = 0; c < numCams; ++c)
        std::memcpy (cams[c].cur, state->camValue[c], sizeof (cams[c].cur));

    if (state->camRoutes.size == 0)
        return;

    float off[numCamDests] {};
    for (int i = 0; i < state->camRoutes.size; ++i)
    {
        const auto& r = state->camRoutes.items[(size_t) i];
        float v = src[r.source] * r.depth;
        if (r.via > 0)
            v *= src[r.via];
        off[r.destination] += v;
    }

    for (int c = 0; c < numCams; ++c)
        for (int f = 0; f < numCamFields; ++f)
            if (state->camFieldModulated[c][f])
                cams[c].cur[f] = camSpecs[f].fromNorm (state->camNorm[c][f] + off[camDest (c, f)]);
}

//==============================================================================
void CamVoice::control()
{
    if (! active)
        return;

    if (glidePos < 1.0f)
    {
        glidePos += (float) controlBlock / (float) (fxme::jmax (0.001f, state->glideSeconds) * state->sampleRate);
        if (glidePos >= 1.0f)
            glidePos = 1.0f;
        pitch = glideFrom + (glideTo - glideFrom) * glidePos;
    }

    updateModulators();
    applyCamModulation();
    for (int c = 0; c < numCams; ++c)
        updateCam (c, snapNext);
    snapNext = false;
}

void CamVoice::updateCam (int c, bool snap)
{
    auto& k = cams[c];
    const float* v = k.cur;
    const double sr = state->sampleRate;
    const float ramp = 1.0f / (float) controlBlock;

    fxme::CurveAdsr::Parameters ep;
    ep.attack = v[cfAttack] * 0.001f;
    ep.decay = v[cfDecay] * 0.001f;
    ep.sustain = v[cfSustain];
    ep.release = v[cfRelease] * 0.001f;
    ep.attackCurve = v[cfAttackCurve];
    ep.decayCurve = v[cfDecayCurve];
    ep.releaseCurve = v[cfReleaseCurve];
    k.env.setParameters (ep);
    k.velocityGain = 1.0f - v[cfVelocity] + v[cfVelocity] * velocity;

    if (! state->camNeeded[c])
        return;

    // Pitch and cylinders.
    const float semis = pitch + v[cfCoarse] + v[cfFine] * 0.01f + src[srcPitchBend] * state->pitchBendRange;
    const double baseFreq = 440.0 * std::pow (2.0, ((double) semis - 69.0) / 12.0);
    const int n = fxme::jlimit (1, maxCylinders, (int) std::lround (v[cfCylinders]));
    if (n != k.cylinders)
    {
        const bool randomPhase = v[cfRandomPhase] > 0.5f;
        for (int u = fxme::jmax (0, k.cylinders); u < n; ++u)
        {
            if (k.cylinders > 0)
                k.phase[u] = randomPhase ? (double) random.nextFloat() : k.phase[0];
            k.levelFade[u] = 1.0f;
        }
        if (k.cylinders <= 0)
            snap = true;
        k.cylinders = n;
        k.unisonNorm = 1.0f / std::sqrt ((float) n);
    }

    for (int u = 0; u < n; ++u)
    {
        const float cents = n > 1 ? fxme::UnisonSpread::detuneCents (u, n, v[cfDetune]) : 0.0f;
        const double f = baseFreq * std::pow (2.0, (double) cents / 1200.0);
        const double target = f / sr;
        if (snap)
        {
            k.inc[u] = target;
            k.incStep[u] = 0.0;
        }
        else
            k.incStep[u] = (target - k.inc[u]) / (double) controlBlock;

        const int newLevel = (int) fxme::WavetableSet::levelFor (f, sr);
        if (snap)
        {
            k.level[u] = newLevel;
            k.levelFade[u] = 1.0f;
        }
        else if (newLevel != k.level[u] && k.levelFade[u] >= 1.0f)
        {
            k.prevLevel[u] = k.level[u];
            k.level[u] = newLevel;
            k.levelFade[u] = 0.0f;
        }

        const float position = n > 1 ? fxme::UnisonSpread::detuneFraction (u, n) * v[cfSpread] : 0.0f;
        panGains (position, k.cylGainL[u], k.cylGainR[u]);
    }

    // Gains.
    auto setRamp = [snap, ramp] (float& value, float& step, float target)
    {
        if (snap) { value = target; step = 0.0f; }
        else      step = (target - value) * ramp;
    };

    setRamp (k.gain, k.gainStep, dbToGainOrZero (v[cfVolume]));
    float pl, pr;
    panGains (v[cfPan], pl, pr);
    setRamp (k.panL, k.panLStep, pl);
    setRamp (k.panR, k.panRStep, pr);
    const float sends[numAux] { v[cfSend1], v[cfSend2] };
    k.dryMuted = false;
    for (int a = 0; a < numAux; ++a)
    {
        setRamp (k.send[a], k.sendStep[a], sends[a]);
        k.tap[a] = (int) v[cfTap1 + a];
        if (state->auxDryMute[a] && state->camValue[c][cfSend1 + a] > 0.0f)
            k.dryMuted = true;
    }
    for (int s = 0; s < numCams; ++s)
        setRamp (k.lift[s], k.liftStep[s], v[cfLift1 + s]);

    // Filters.
    k.parallel = v[cfFilterRouting] > 0.5f;
    const float bal = v[cfFilterBalance];
    k.balanceA = 0.5f * (1.0f - bal);
    k.balanceB = 0.5f * (1.0f + bal);
    const float envValue = k.env.getValue();
    for (int fi = 0; fi < 2; ++fi)
    {
        const int o = fi * filterFieldStride;
        k.filterOn[fi] = v[cfF1On + o] > 0.5f;
        if (! k.filterOn[fi])
            continue;

        fxme::MultiModeFilter::Parameters p;
        p.type = (fxme::MultiModeFilter::Type) fxme::jlimit (0, fxme::MultiModeFilter::numTypes - 1, (int) v[cfF1Type + o]);
        const float octaves = v[cfF1KeyTrack + o] * ((float) note - 60.0f) / 12.0f
                            + v[cfF1EnvAmount + o] * 6.0f * envValue;
        p.cutoffHz = fxme::jlimit (20.0f, 20000.0f, v[cfF1Cutoff + o] * std::pow (2.0f, octaves));
        p.resonance = v[cfF1Res + o];
        p.gainDb = v[cfF1Gain + o];
        p.vowelA = (int) v[cfF1VowelA + o];
        p.vowelB = (int) v[cfF1VowelB + o];
        p.vowelPosition = v[cfF1VowelPos + o];
        k.filters[fi][0].setParameters (p);
        k.filters[fi][1].setParameters (p);
    }

    updateTables (c);
}

void CamVoice::updateTables (int c)
{
    auto& k = cams[c];
    auto* cache = state->caches[(size_t) c];
    if (cache == nullptr)
        return;

    const float* v = k.cur;
    const auto key = makeTableKey (*state, v[cfCentre], v[cfWidth], v[cfOverlap],
                                   (int) v[cfOverlapShape], (int) v[cfSnap]);
    if (! key.isValid())
        return;

    const int stream = numCams + index * numCams + c;

    if (k.slot < 0)
    {
        int s = cache->tryAcquire (key);
        if (s < 0)
        {
            cache->request (key, stream);
            s = cache->acquireNearest (key);
        }
        if (s >= 0)
        {
            k.slot = s;
            k.key = cache->getKey (s);
            k.setFade = 1.0f;
        }
        return;
    }

    if (key == k.key || k.setFade < 1.0f)
        return;   // nothing new, or finish the crossfade in progress first

    const int s = cache->tryAcquire (key);
    if (s < 0)
    {
        cache->request (key, stream);
        return;
    }

    k.oldSlot = k.slot;
    k.slot = s;
    k.key = key;
    k.setFade = 0.0f;
}

float CamVoice::stealGain() noexcept
{
    if (! stealing)
        return 1.0f;
    stealLevel -= stealStep;
    if (stealLevel <= 0.0f)
    {
        stealing = false;
        start (pendingNote, pendingVelocity, pendingGlideFrom);
        return 0.0f;
    }
    return stealLevel;
}

//==============================================================================
void CamVoice::render (const Buses& out, int offset, int n, float* const* display)
{
    if (! active)
        return;

    const auto interp = state->interpolation;
    const float setFadeStep = 1.0f / (float) tableCrossfadeSamples;
    const float levelFadeStep = 1.0f / (float) controlBlock;
    constexpr double toCycles = 1.0 / twoPi;

    for (int i = 0; i < n; ++i)
    {
        const float sg = stealGain();
        const int pos = offset + i;

        for (int oi = 0; oi < numCams; ++oi)
        {
            const int c = state->camOrder[oi];
            auto& k = cams[c];
            const float env = k.env.getNextSample();

            if (! state->camNeeded[c] || k.slot < 0 || k.cylinders <= 0)
            {
                k.out0 = 0.0f;
                if (display != nullptr)
                    display[c][pos] = 0.0f;
                continue;
            }

            auto* cache = state->caches[(size_t) c];

            // Frequency modulation: a phase offset from the sources' first
            // cylinders (this sample) or from itself (previous sample).
            float fm = 0.0f;
            for (int s = 0; s < numCams; ++s)
                if (state->xmodType[s][c] == 1)
                    fm += k.lift[s] * cams[s].out0;
            const double fmCycles = (double) (fm * fmIndexAtFullLift) * toCycles;

            const auto& set = cache->getSet (k.slot);
            const fxme::WavetableSet* old = (k.setFade < 1.0f && k.oldSlot >= 0) ? &cache->getSet (k.oldSlot) : nullptr;

            float l = 0.0f, r = 0.0f, o0 = 0.0f;
            for (int u = 0; u < k.cylinders; ++u)
            {
                double ph = k.phase[u] + fmCycles;
                ph -= std::floor (ph);

                float a, b;
                fxme::WavetableReader::readStereo (set, k.level[u], ph, interp, a, b);
                if (k.levelFade[u] < 1.0f)
                {
                    float pa, pb;
                    fxme::WavetableReader::readStereo (set, k.prevLevel[u], ph, interp, pa, pb);
                    const float t = k.levelFade[u];
                    a = pa + (a - pa) * t;
                    b = pb + (b - pb) * t;
                    k.levelFade[u] = fxme::jmin (1.0f, t + levelFadeStep);
                }
                if (old != nullptr)
                {
                    float oa, ob;
                    fxme::WavetableReader::readStereo (*old, k.level[u], ph, interp, oa, ob);
                    a = oa + (a - oa) * k.setFade;
                    b = ob + (b - ob) * k.setFade;
                }

                if (u == 0)
                    o0 = 0.5f * (a + b);
                l += a * k.cylGainL[u];
                r += b * k.cylGainR[u];

                k.phase[u] += k.inc[u];
                if (k.phase[u] >= 1.0)
                    k.phase[u] -= std::floor (k.phase[u]);
                k.inc[u] += k.incStep[u];
            }

            if (old != nullptr)
            {
                k.setFade += setFadeStep;
                if (k.setFade >= 1.0f)
                {
                    k.setFade = 1.0f;
                    cache->release (k.oldSlot);
                    k.oldSlot = -1;
                }
            }

            l *= k.unisonNorm;
            r *= k.unisonNorm;

            // Ring and amplitude modulation from the other cams.
            for (int s = 0; s < numCams; ++s)
            {
                if (s == c)
                    continue;
                const int t = state->xmodType[s][c];
                if (t == 2)
                {
                    const float g = 1.0f - k.lift[s] + k.lift[s] * cams[s].out0;
                    l *= g;
                    r *= g;
                }
                else if (t == 3)
                {
                    const float g = 1.0f + k.lift[s] * cams[s].out0;
                    l *= g;
                    r *= g;
                }
            }
            k.out0 = o0;

            // A cam that only modulates others is not heard.
            if (! state->camAudible[c])
            {
                if (display != nullptr)
                    display[c][pos] = 0.0f;
                continue;
            }

            // Filters.
            float fl = l, fr = r;
            if (k.filterOn[0] || k.filterOn[1])
            {
                if (! k.parallel)
                {
                    for (int fi = 0; fi < 2; ++fi)
                        if (k.filterOn[fi])
                        {
                            fl = k.filters[fi][0].processSample (fl);
                            fr = k.filters[fi][1].processSample (fr);
                        }
                }
                else
                {
                    const float al = k.filterOn[0] ? k.filters[0][0].processSample (l) : l;
                    const float ar = k.filterOn[0] ? k.filters[0][1].processSample (r) : r;
                    const float bl = k.filterOn[1] ? k.filters[1][0].processSample (l) : l;
                    const float br = k.filterOn[1] ? k.filters[1][1].processSample (r) : r;
                    fl = k.balanceA * al + k.balanceB * bl;
                    fr = k.balanceA * ar + k.balanceB * br;
                }
            }

            const float e = env * k.velocityGain * sg;
            const float postL = fl * e, postR = fr * e;
            const float finalL = postL * k.gain, finalR = postR * k.gain;
            const float pl = k.panL, pr = k.panR;

            if (! k.dryMuted)
            {
                out.dry[0][pos] += finalL * pl;
                out.dry[1][pos] += finalR * pr;
            }

            for (int a = 0; a < numAux; ++a)
            {
                if (k.send[a] <= 0.0f && isZero (k.sendStep[a]))
                    continue;
                float tl, tr;
                switch (k.tap[a])
                {
                    case 0:  tl = l * e;  tr = r * e;  break;
                    case 1:  tl = postL;  tr = postR;  break;
                    default: tl = finalL; tr = finalR; break;
                }
                out.aux[a][0][pos] += tl * pl * k.send[a];
                out.aux[a][1][pos] += tr * pr * k.send[a];
            }

            if (display != nullptr)
                display[c][pos] = 0.5f * (postL + postR);

            k.gain += k.gainStep;
            k.panL += k.panLStep;
            k.panR += k.panRStep;
            for (int a = 0; a < numAux; ++a)
                k.send[a] += k.sendStep[a];
            for (int s = 0; s < numCams; ++s)
                k.lift[s] += k.liftStep[s];
        }
    }
}

} // namespace cam
