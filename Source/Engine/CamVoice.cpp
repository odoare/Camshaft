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
        k.rangeValid = false;
        k.out0 = 0.0f;
        for (int ch = 0; ch < 2; ++ch)
            k.dcX[ch] = k.dcY[ch] = 0.0f;
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
        k.cylinders = 0;        // re-initialised by updateCam
        k.rangeValid = false;   // a new note starts on its range, no glide to it
        for (int ch = 0; ch < 2; ++ch)
            k.dcX[ch] = k.dcY[ch] = 0.0f;
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
    // A curve still playing its release region keeps the voice, if it
    // modulates something.
    for (int m = 0; m < numModulators; ++m)
        if ((int) state->modValue[m][mfType] == mtCurve && state->modUsed[m] && ! state->modIsGlobal[m]
            && curves[m].isReleasing())
            return false;
    return true;
}

bool CamVoice::getCamRange (int c, double& start, double& cycle, double& overlap, double& phase) const noexcept
{
    const auto& k = cams[c];
    if (! k.rangeValid || k.cylinders <= 0)
        return false;
    start = k.start;
    cycle = k.cycle;
    overlap = k.overlap;
    phase = k.phase[0];
    return true;
}

void CamVoice::getModulatorProgress (int m, float& phase, int& stage) const noexcept
{
    phase = -1.0f;
    stage = 0;
    if (! active || ! state->modUsed[m])
        return;
    const int type = (int) state->modValue[m][mfType];
    if (type == mtLfo)
        phase = (float) lfos[m].getPhase();
    else if (type == mtAdsr && adsrs[m].isActive())
    {
        stage = (int) adsrs[m].getState();
        phase = (float) adsrs[m].getSegmentPosition();
    }
}

float CamVoice::getCurvePosition (int m) const noexcept
{
    if (! active || (int) state->modValue[m][mfType] != mtCurve || state->modIsGlobal[m] || ! state->modUsed[m])
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
        if (! state->modUsed[m])
        {
            src[srcModFirst + m] = 0.0f;   // feeds nothing (or is off): not computed
            continue;
        }

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
        if (type == mtLfo)
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
        else if (type == mtAdsr)
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
        else if (type == mtCurve)
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
    // Synced by another cam: the belt's lift sweeps this one up to three
    // octaves (the classic sync sweep: the pitch stays the driver's, the
    // timbre moves).
    double syncOctaves = 0.0;
    for (int s = 0; s < numCams; ++s)
        if (s != c && state->xmodType[s][c] == xmSync)
            syncOctaves = fxme::jmax (syncOctaves, 3.0 * (double) v[cfLift1 + s]);
    const double baseOctaves = ((double) semis - 69.0) / 12.0 + syncOctaves;     // relative to 440 Hz
    const int n = fxme::jlimit (1, maxCylinders, (int) std::lround (v[cfCylinders]));
    if (n != k.cylinders)
    {
        const bool randomPhase = v[cfRandomPhase] > 0.5f;
        for (int u = fxme::jmax (0, k.cylinders); u < n; ++u)
        {
            if (k.cylinders > 0)
            {
                k.phase[u] = randomPhase ? (double) random.nextFloat() : k.phase[0];
                k.level[u] = k.level[0];
                k.inc[u] = k.inc[0];
            }
        }
        if (k.cylinders <= 0)
            snap = true;
        k.cylinders = n;
        k.unisonNorm = 1.0f / std::sqrt ((float) n);
    }

    // The range first: the read speed, so the pyramid level, depends on it.
    updateRange (c, snap);
    const auto* source = state->source;

    // The read speed's logarithm (source samples per output sample) at
    // 440 Hz: each cylinder's pyramid level follows from its pitch in
    // octaves, without a logarithm per cylinder.
    const double log2SpeedAt440 = std::log2 (fxme::jmax (1.0, k.smoothCycle) * 440.0 / sr);

    for (int u = 0; u < n; ++u)
    {
        const float cents = n > 1 ? fxme::UnisonSpread::detuneCents (u, n, v[cfDetune]) : 0.0f;
        const double octaves = baseOctaves + (double) cents / 1200.0;
        const double target = 440.0 * std::exp2 (octaves) / sr;
        if (snap)
        {
            k.inc[u] = target;
            k.incStep[u] = 0.0;
        }
        else
            k.incStep[u] = (target - k.inc[u]) / (double) controlBlock;

        // Pyramid level for the read speed (source samples per output
        // sample), ramped like everything else so it never jumps.
        const float newLevel = source != nullptr ? source->buffer.levelForLog2Speed (log2SpeedAt440 + octaves) : 0.0f;
        if (snap)
        {
            k.level[u] = newLevel;
            k.levelStep[u] = 0.0f;
        }
        else
            k.levelStep[u] = (newLevel - k.level[u]) / (float) controlBlock;

        if (n > 1)
            panGains (fxme::UnisonSpread::detuneFraction (u, n) * v[cfSpread], k.cylGainL[u], k.cylGainR[u]);
        else
            k.cylGainL[u] = k.cylGainR[u] = 1.0f;     // panGains (0): unity in the centre
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

    // Silent: no path to the outputs carries anything, ramps included (a
    // fade to zero is played to its end). A send tapped before the volume
    // carries the cam whatever its volume.
    auto off = [] (float value, float step) { return isZero (value) && isZero (step); };
    const bool gainOff = off (k.gain, k.gainStep);
    bool quiet = k.dryMuted || gainOff;
    for (int a = 0; a < numAux; ++a)
        quiet = quiet && (off (k.send[a], k.sendStep[a]) || (k.tap[a] == 2 && gainOff));
    k.silent = quiet;

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
        // Computed once for the pair, and only when something changed.
        if (! k.filters[fi][0].hasParameters (p))
        {
            k.filters[fi][0].setParameters (p);
            k.filters[fi][1].copySettingsFrom (k.filters[fi][0]);
        }
    }
}

void CamVoice::updateRange (int c, bool snap)
{
    auto& k = cams[c];
    const auto* source = state->source;
    if (source == nullptr || source->numSamples() < 16)
    {
        k.rangeValid = false;
        return;
    }
    if (source != k.sourceSeen)
    {
        // Another recording: start on it, no glide from positions in the old one.
        k.sourceSeen = source;
        k.rangeValid = false;
    }

    const float* v = k.cur;
    const double n = (double) source->numSamples();
    double start, len;
    camRange (v[cfOpening], v[cfDuration], n, start, len);

    const int snapMode = (int) v[cfSnap];
    if (snapMode == 1)
    {
        // Both edges on the nearest rising zero crossings.
        const double radius = fxme::jmin (2048.0, fxme::jmax (1.0, len * 0.25));
        const double a = source->analysis.nearestRisingZeroCrossing (start, radius);
        const double b = source->analysis.nearestRisingZeroCrossing (start + len, radius);
        if (b - a >= 16.0 && b <= n)
        {
            start = a;
            len = b - a;
        }
    }
    else if (snapMode == 2)
    {
        // Opening on a rising zero crossing, for a whole number of the
        // periods detected around the middle of the range.
        const double period = source->analysis.periodAt (start + 0.5 * len);
        if (period > 0.0)
        {
            start = fxme::jlimit (0.0, n - 16.0, source->analysis.nearestRisingZeroCrossing (start, 0.5 * period));
            len = fxme::jmax (1.0, std::round (len / period)) * period;
            while (len > n - start && len - period >= 16.0)
                len -= period;
            len = fxme::jlimit (16.0, n - start, len);
        }
    }

    const double overlap = fxme::jlimit (0.0, 0.5 * len, (double) v[cfOverlap] * 0.01 * len);
    const double cycle = len - overlap;
    k.equalPower = (int) v[cfOverlapShape] == 1;

    if (snap || ! k.rangeValid)
    {
        k.start = k.smoothStart = start;
        k.cycle = k.smoothCycle = cycle;
        k.overlap = k.smoothOverlap = overlap;
        k.startStep = k.cycleStep = k.overlapStep = 0.0;
        k.rangeValid = true;
        return;
    }

    // Smoothed (one pole per control block), then ramped per sample.
    const double a = 1.0 - std::exp (-(double) controlBlock / (rangeSmoothingSeconds * state->sampleRate));
    k.smoothStart += (start - k.smoothStart) * a;
    k.smoothCycle += (cycle - k.smoothCycle) * a;
    k.smoothOverlap += (overlap - k.smoothOverlap) * a;
    k.startStep = (k.smoothStart - k.start) / (double) controlBlock;
    k.cycleStep = (k.smoothCycle - k.cycle) / (double) controlBlock;
    k.overlapStep = (k.smoothOverlap - k.overlap) / (double) controlBlock;
}

//==============================================================================
void CamVoice::render (const Buses& out, int offset, int n, float* const* display)
{
    if (! active || state->source == nullptr || n <= 0)
        return;

    if (! stealing)
    {
        renderSpan (out, offset, n, display, nullptr);
        return;
    }

    // Stealing: fade out, then start the pending note on the rest of the span.
    float steal[controlBlock];
    int faded = 0;
    bool done = false;
    while (faded < n)
    {
        stealLevel -= stealStep;
        if (stealLevel <= 0.0f)
        {
            steal[faded++] = 0.0f;
            done = true;
            break;
        }
        steal[faded++] = stealLevel;
    }
    if (! done)
    {
        renderSpan (out, offset, faded, display, steal);
        return;
    }

    // The fade reached zero on sample `faded - 1`: the old note plays up to
    // it, the new one starts on it (silent there, the fade's last sample).
    renderSpan (out, offset, faded - 1, display, steal);
    stealing = false;
    start (pendingNote, pendingVelocity, pendingGlideFrom);
    float gate[controlBlock];
    const int rest = n - (faded - 1);
    gate[0] = 0.0f;
    for (int i = 1; i < rest; ++i)
        gate[i] = 1.0f;
    renderSpan (out, offset + faded - 1, rest, display, gate);
}

void CamVoice::renderSpan (const Buses& out, int offset, int n, float* const* display, const float* steal)
{
    if (n <= 0)
        return;
    for (int oi = 0; oi < numCams; ++oi)
        renderCam (state->camOrder[oi], out, offset, n, display, steal);
}

void CamVoice::renderCam (int c, const Buses& out, int offset, int n, float* const* display, const float* steal)
{
    auto& k = cams[c];
    const bool feeds = state->camFeeds[c];

    // Its amp envelope finished (cams release at their own pace): nothing to
    // hear, unless it modulates another cam.
    if (! feeds && ! k.env.isActive())
    {
        skipCam (k, c, offset, n, display);
        return;
    }

    float env[controlBlock];
    for (int i = 0; i < n; ++i)
        env[i] = k.env.getNextSample();

    if (! feeds && k.silent && state->camNeeded[c])
    {
        skipCam (k, c, offset, n, display);
        return;
    }

    if (! state->camNeeded[c] || ! k.rangeValid || k.cylinders <= 0)
    {
        k.out0 = 0.0f;
        std::memset (k.outBuf, 0, sizeof (float) * (size_t) n);
        std::fill (k.syncBuf, k.syncBuf + n, -1.0f);
        k.syncCarry = -1.0f;
        if (display != nullptr)
            std::memset (display[c] + offset, 0, sizeof (float) * (size_t) n);
        return;
    }

    // Frequency modulation (a phase offset, in cycles) from the cams before
    // this one in the belt's order (their output over this span is complete)
    // and from itself (its previous sample, inside the cylinder loop).
    constexpr float toCycles = (float) (fmIndexAtFullLift / twoPi);
    float fm[controlBlock], selfLift[controlBlock];
    bool anyFm = false;
    for (int s = 0; s < numCams; ++s)
    {
        if (s == c || state->xmodType[s][c] != 1)
            continue;
        if (! anyFm)
            std::memset (fm, 0, sizeof (float) * (size_t) n);
        anyFm = true;
        const float* from = cams[s].outBuf;
        const float l0 = k.lift[s], dl = k.liftStep[s];
        for (int i = 0; i < n; ++i)
            fm[i] += (l0 + dl * (float) i) * from[i] * toCycles;
    }
    const bool selfFm = state->xmodType[c][c] == 1;
    if (selfFm)
    {
        if (! anyFm)
            std::memset (fm, 0, sizeof (float) * (size_t) n);
        anyFm = true;
        for (int i = 0; i < n; ++i)
            selfLift[i] = (k.lift[c] + k.liftStep[c] * (float) i) * toCycles;
    }

    // Sync from a cam before this one: its cycle starts restart this one's.
    const float* sync = nullptr;
    for (int s = 0; s < numCams && sync == nullptr; ++s)
        if (s != c && state->xmodType[s][c] == xmSync)
            sync = cams[s].syncBuf;

    // Opening modulation: the read position moves with the drivers' output,
    // up to a cycle either way at full lift.
    float opening[controlBlock];
    bool anyOpening = false;
    for (int s = 0; s < numCams; ++s)
    {
        if (s == c || state->xmodType[s][c] != xmOpening)
            continue;
        if (! anyOpening)
            std::memset (opening, 0, sizeof (float) * (size_t) n);
        anyOpening = true;
        const float* from = cams[s].outBuf;
        const float l0 = k.lift[s], dl = k.liftStep[s];
        const float cycleSamples = (float) k.cycle;
        for (int i = 0; i < n; ++i)
            opening[i] += (l0 + dl * (float) i) * from[i] * cycleSamples;
    }
    const float* openingPtr = anyOpening ? opening : nullptr;

    // The cylinders, summed.
    float l[controlBlock] {}, r[controlBlock] {};
    const bool stereo = state->source->buffer.getNumChannels() > 1;
    float* fmp = anyFm ? fm : nullptr;
    const float* slp = selfFm ? selfLift : nullptr;
    using I = fxme::WavetableReader::Interpolation;
    switch (state->interpolation)
    {
        case I::linear:
            if (stereo) renderCylinders<(int) I::linear, true> (k, n, fmp, slp, sync, openingPtr, l, r);
            else        renderCylinders<(int) I::linear, false> (k, n, fmp, slp, sync, openingPtr, l, r);
            break;
        case I::sinc:
            if (stereo) renderCylinders<(int) I::sinc, true> (k, n, fmp, slp, sync, openingPtr, l, r);
            else        renderCylinders<(int) I::sinc, false> (k, n, fmp, slp, sync, openingPtr, l, r);
            break;
        case I::cubic:
        default:
            if (stereo) renderCylinders<(int) I::cubic, true> (k, n, fmp, slp, sync, openingPtr, l, r);
            else        renderCylinders<(int) I::cubic, false> (k, n, fmp, slp, sync, openingPtr, l, r);
            break;
    }

    // The range moves on, the span's worth of its ramp.
    k.start += k.startStep * (double) n;
    k.cycle += k.cycleStep * (double) n;
    k.overlap += k.overlapStep * (double) n;

    // DC blocker (about 8 Hz: a range of real audio rarely averages to
    // zero), then the unison normalisation.
    {
        const float pole = 1.0f - (float) (twoPi * 8.0 / state->sampleRate);
        const float g = k.unisonNorm;
        float xl = k.dcX[0], yl = k.dcY[0], xr = k.dcX[1], yr = k.dcY[1];
        for (int i = 0; i < n; ++i)
        {
            yl = l[i] - xl + pole * yl;  xl = l[i];  l[i] = yl * g;
            yr = r[i] - xr + pole * yr;  xr = r[i];  r[i] = yr * g;
        }
        k.dcX[0] = xl;  k.dcY[0] = yl;  k.dcX[1] = xr;  k.dcY[1] = yr;
    }

    // Ring and amplitude modulation from the cams before this one.
    for (int s = 0; s < numCams; ++s)
    {
        const int t = s == c ? 0 : state->xmodType[s][c];
        if (t != 2 && t != 3)
            continue;
        const float* from = cams[s].outBuf;
        const float l0 = k.lift[s], dl = k.liftStep[s];
        for (int i = 0; i < n; ++i)
        {
            const float lift = l0 + dl * (float) i;
            const float g = t == 2 ? 1.0f - lift + lift * from[i] : 1.0f + lift * from[i];
            l[i] *= g;
            r[i] *= g;
        }
    }
    for (int s = 0; s < numCams; ++s)
        k.lift[s] += k.liftStep[s] * (float) n;

    // A cam that only modulates others is not heard.
    if (! state->camAudible[c])
    {
        if (display != nullptr)
            std::memset (display[c] + offset, 0, sizeof (float) * (size_t) n);
        k.gain += k.gainStep * (float) n;
        k.panL += k.panLStep * (float) n;
        k.panR += k.panRStep * (float) n;
        for (int a = 0; a < numAux; ++a)
            k.send[a] += k.sendStep[a] * (float) n;
        return;
    }

    // A send tapped before the filters needs the signal as it is now.
    bool sending[numAux] {}, preTap = false;
    for (int a = 0; a < numAux; ++a)
    {
        sending[a] = k.send[a] > 0.0f || k.sendStep[a] > 0.0f || k.sendStep[a] < 0.0f;
        preTap = preTap || (sending[a] && k.tap[a] == 0);
    }
    float preL[controlBlock], preR[controlBlock];
    if (preTap)
    {
        std::memcpy (preL, l, sizeof (float) * (size_t) n);
        std::memcpy (preR, r, sizeof (float) * (size_t) n);
    }

    // Filters.
    if (k.filterOn[0] || k.filterOn[1])
    {
        if (! k.parallel)
        {
            for (int fi = 0; fi < 2; ++fi)
                if (k.filterOn[fi])
                {
                    auto& fl = k.filters[fi][0];
                    auto& fr = k.filters[fi][1];
                    for (int i = 0; i < n; ++i)
                    {
                        l[i] = fl.processSample (l[i]);
                        r[i] = fr.processSample (r[i]);
                    }
                }
        }
        else
            for (int i = 0; i < n; ++i)
            {
                const float al = k.filterOn[0] ? k.filters[0][0].processSample (l[i]) : l[i];
                const float ar = k.filterOn[0] ? k.filters[0][1].processSample (r[i]) : r[i];
                const float bl = k.filterOn[1] ? k.filters[1][0].processSample (l[i]) : l[i];
                const float br = k.filterOn[1] ? k.filters[1][1].processSample (r[i]) : r[i];
                l[i] = k.balanceA * al + k.balanceB * bl;
                r[i] = k.balanceA * ar + k.balanceB * br;
            }
    }

    // Envelope, gain, pan, sends, outputs.
    float* dryL = out.dry[0] + offset;
    float* dryR = out.dry[1] + offset;
    float* disp = display != nullptr ? display[c] + offset : nullptr;
    for (int i = 0; i < n; ++i)
    {
        const float e = env[i] * k.velocityGain * (steal != nullptr ? steal[i] : 1.0f);
        const float postL = l[i] * e, postR = r[i] * e;
        const float finalL = postL * k.gain, finalR = postR * k.gain;
        const float pl = k.panL, pr = k.panR;

        if (! k.dryMuted)
        {
            dryL[i] += finalL * pl;
            dryR[i] += finalR * pr;
        }
        for (int a = 0; a < numAux; ++a)
        {
            if (! sending[a])
                continue;
            float tl, tr;
            switch (k.tap[a])
            {
                case 0:  tl = preL[i] * e;  tr = preR[i] * e;  break;
                case 1:  tl = postL;  tr = postR;  break;
                default: tl = finalL; tr = finalR; break;
            }
            out.aux[a][0][offset + i] += tl * pl * k.send[a];
            out.aux[a][1][offset + i] += tr * pr * k.send[a];
        }
        if (disp != nullptr)
            disp[i] = 0.5f * (postL + postR);

        k.gain += k.gainStep;
        k.panL += k.panLStep;
        k.panR += k.panRStep;
        for (int a = 0; a < numAux; ++a)
            k.send[a] += k.sendStep[a];
    }
}

void CamVoice::skipCam (Cam& k, int c, int offset, int n, float* const* display)
{
    // Nothing rendered, nothing heard; the ramps still move on, so the cam
    // comes back where its settings are. Its phases wait (a cycle's phase is
    // arbitrary), and its first-cylinder output is silence for anyone reading
    // it (no one does: it feeds no cam).
    k.out0 = 0.0f;
    std::memset (k.outBuf, 0, sizeof (float) * (size_t) n);
    std::fill (k.syncBuf, k.syncBuf + n, -1.0f);
    k.syncCarry = -1.0f;
    if (display != nullptr)
        std::memset (display[c] + offset, 0, sizeof (float) * (size_t) n);

    const float fn = (float) n;
    k.start += k.startStep * (double) n;
    k.cycle += k.cycleStep * (double) n;
    k.overlap += k.overlapStep * (double) n;
    for (int u = 0; u < k.cylinders; ++u)
    {
        k.inc[u] += k.incStep[u] * (double) n;
        k.level[u] += k.levelStep[u] * fn;
    }
    k.gain += k.gainStep * fn;
    k.panL += k.panLStep * fn;
    k.panR += k.panRStep * fn;
    for (int a = 0; a < numAux; ++a)
        k.send[a] += k.sendStep[a] * fn;
    for (int s = 0; s < numCams; ++s)
        k.lift[s] += k.liftStep[s] * fn;
}

template <int InterpolationMode, bool Stereo>
void CamVoice::renderCylinders (Cam& k, int n, float* fm, const float* selfLift, const float* sync,
                                const float* openingOffset, float* left, float* right)
{
    constexpr auto mode = (fxme::WavetableReader::Interpolation) InterpolationMode;
    const auto& buffer = state->source->buffer;
    const bool equalPower = k.equalPower;

    // This cam's own cycle starts, for the cams it syncs.
    std::fill (k.syncBuf, k.syncBuf + n, -1.0f);
    if (k.syncCarry >= 0.0f)
        k.syncBuf[0] = k.syncCarry;
    k.syncCarry = -1.0f;

    for (int u = 0; u < k.cylinders; ++u)
    {
        double phase = k.phase[u], inc = k.inc[u];
        const double incStep = k.incStep[u];
        float level = k.level[u];
        const float levelStep = k.levelStep[u];
        const float gl = k.cylGainL[u], gr = k.cylGainR[u];
        double start = k.start, cycle = k.cycle, overlap = k.overlap;
        const double startStep = k.startStep, cycleStep = k.cycleStep, overlapStep = k.overlapStep;
        const bool first = u == 0;
        // Self FM: a phase offset from the first cylinder's previous sample,
        // added to the FM in its loop; the other cylinders read the total.
        const bool addSelf = first && selfLift != nullptr;
        float prev = k.out0;
        float pendingStepA = 0.0f, pendingStepB = 0.0f;   // a sync jump announced on the previous sample

        for (int i = 0; i < n; ++i)
        {
            const double readStart = openingOffset != nullptr ? start + (double) openingOffset[i] : start;
            const fxme::RangeLoop::Range range { readStart, cycle, overlap, equalPower };
            auto valueAt = [&] (double p, float& va, float& vb)
            {
                if constexpr (Stereo)
                    fxme::RangeLoop::readPair (buffer, level, range, p, mode, va, vb);
                else
                    vb = va = fxme::RangeLoop::read (buffer, 0, level, range, p, mode);
            };

            // Synced: this sample comes `since` samples after the driver's
            // cycle began, so the cycle restarts from there. The jump in the
            // waveform is band-limited like the loop's wrap: half of a
            // polyBLEP before it (added on the previous sample, from the
            // look-ahead below), half after.
            float afterA = 0.0f, afterB = 0.0f;
            if (sync != nullptr && sync[i] >= 0.0f)
            {
                const float since = sync[i];
                phase = (double) since * inc;
                const float r = -(1.0f - since) * (1.0f - since);
                afterA = 0.5f * r * pendingStepA;
                afterB = 0.5f * r * pendingStepB;
            }
            pendingStepA = pendingStepB = 0.0f;

            double ph = phase;
            if (fm != nullptr)
            {
                if (addSelf)
                    fm[i] += selfLift[i] * prev;
                ph += (double) fm[i];
                ph -= std::floor (ph);
            }

            float a, b;
            valueAt (ph, a, b);
            a += afterA;
            b += afterB;

            if (sync != nullptr && i + 1 < n && sync[i + 1] >= 0.0f)
            {
                // The next sample restarts the cycle: its jump is the
                // restarted value minus the value it would have had.
                const double next = phase + inc;
                float ra, rb, ca, cb;
                valueAt ((double) sync[i + 1] * inc, ra, rb);
                valueAt (next - std::floor (next), ca, cb);
                pendingStepA = ra - ca;
                pendingStepB = rb - cb;
                const float d2 = sync[i + 1] * sync[i + 1];
                a += 0.5f * d2 * pendingStepA;
                b += 0.5f * d2 * pendingStepB;
            }

            if (overlap < 1.0)
            {
                // No overlap: band-limit the step at the wrap.
                const float residual = fxme::RangeLoop::polyBlep (ph, inc);
                if (residual > 0.0f || residual < 0.0f)
                {
                    float sa, sb;
                    if constexpr (Stereo)
                        fxme::RangeLoop::wrapStepPair (buffer, level, range, mode, sa, sb);
                    else
                        sb = sa = fxme::RangeLoop::wrapStep (buffer, 0, level, range, mode);
                    a += 0.5f * residual * sa;
                    b += 0.5f * residual * sb;
                }
            }

            if (first)
            {
                prev = 0.5f * (a + b);
                k.outBuf[i] = prev;
            }
            left[i] += a * gl;
            right[i] += b * gr;

            phase += inc;
            if (phase >= 1.0)
            {
                phase -= std::floor (phase);
                if (first)
                {
                    // A new cycle began between this sample and the next.
                    const float since = (float) fxme::jmin (0.999, phase / fxme::jmax (1.0e-12, inc));
                    if (i + 1 < n)
                        k.syncBuf[i + 1] = since;
                    else
                        k.syncCarry = since;
                }
            }
            inc += incStep;
            level += levelStep;
            start += startStep;
            cycle += cycleStep;
            overlap += overlapStep;
        }

        k.phase[u] = phase;
        k.inc[u] = inc;
        k.level[u] = level;
        if (first)
            k.out0 = prev;
    }
}

} // namespace cam
