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
    for (int m = 0; m < numModulators; ++m)
        if ((int) state->modValue[m][mfType] == 2 && ! state->modIsGlobal[m] && curves[m].isReleasing())
            return false;
    return true;
}

double CamVoice::getCamPhase (int c) const noexcept
{
    return cams[c].phase[0];
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

        // Pyramid level for the read speed (source samples per output
        // sample), ramped like everything else so it never jumps.
        const float newLevel = source != nullptr ? source->buffer.levelForSpeed (k.smoothCycle * f / sr) : 0.0f;
        if (snap)
        {
            k.level[u] = newLevel;
            k.levelStep[u] = 0.0f;
        }
        else
            k.levelStep[u] = (newLevel - k.level[u]) / (float) controlBlock;

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
    const double centre = (double) fxme::jlimit (0.0f, 1.0f, v[cfCentre]) * n;
    double len = fxme::jlimit (16.0, n, (double) v[cfWidth]);
    double start = fxme::jlimit (0.0, n - len, centre - 0.5 * len);

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
        // A whole number of the periods detected around the centre, starting
        // on a rising zero crossing.
        const double period = source->analysis.periodAt (centre);
        if (period > 0.0)
        {
            len = fxme::jlimit (16.0, n, fxme::jmax (1.0, std::round (len / period)) * period);
            start = fxme::jlimit (0.0, n - len, centre - 0.5 * len);
            start = fxme::jlimit (0.0, n - len, source->analysis.nearestRisingZeroCrossing (start, 0.5 * period));
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
    constexpr double toCycles = 1.0 / twoPi;
    const auto* source = state->source;
    if (source == nullptr)
        return;
    const auto& buffer = source->buffer;
    const bool stereo = buffer.getNumChannels() > 1;
    // DC blocker, about 8 Hz: a range of real audio rarely averages to zero.
    const float dcPole = 1.0f - (float) (twoPi * 8.0 / state->sampleRate);

    for (int i = 0; i < n; ++i)
    {
        const float sg = stealGain();
        const int pos = offset + i;

        for (int oi = 0; oi < numCams; ++oi)
        {
            const int c = state->camOrder[oi];
            auto& k = cams[c];
            const float env = k.env.getNextSample();

            if (! state->camNeeded[c] || ! k.rangeValid || k.cylinders <= 0)
            {
                k.out0 = 0.0f;
                if (display != nullptr)
                    display[c][pos] = 0.0f;
                continue;
            }

            // Frequency modulation: a phase offset from the sources' first
            // cylinders (this sample) or from itself (previous sample).
            float fm = 0.0f;
            for (int s = 0; s < numCams; ++s)
                if (state->xmodType[s][c] == 1)
                    fm += k.lift[s] * cams[s].out0;
            const double fmCycles = (double) (fm * fmIndexAtFullLift) * toCycles;

            const fxme::RangeLoop::Range range { k.start, k.cycle, k.overlap, k.equalPower };
            const bool wrapStep = k.overlap < 1.0;   // no overlap: band-limit the step at the wrap

            float l = 0.0f, r = 0.0f, o0 = 0.0f;
            for (int u = 0; u < k.cylinders; ++u)
            {
                double ph = k.phase[u] + fmCycles;
                ph -= std::floor (ph);

                const float lv = k.level[u];
                float a = fxme::RangeLoop::read (buffer, 0, lv, range, ph, interp);
                float b = stereo ? fxme::RangeLoop::read (buffer, 1, lv, range, ph, interp) : a;

                if (wrapStep)
                {
                    const float residual = fxme::RangeLoop::polyBlep (ph, k.inc[u]);
                    if (residual > 0.0f || residual < 0.0f)
                    {
                        a += 0.5f * residual * fxme::RangeLoop::wrapStep (buffer, 0, lv, range, interp);
                        b = stereo ? b + 0.5f * residual * fxme::RangeLoop::wrapStep (buffer, 1, lv, range, interp) : a;
                    }
                }

                if (u == 0)
                    o0 = 0.5f * (a + b);
                l += a * k.cylGainL[u];
                r += b * k.cylGainR[u];

                k.phase[u] += k.inc[u];
                if (k.phase[u] >= 1.0)
                    k.phase[u] -= std::floor (k.phase[u]);
                k.inc[u] += k.incStep[u];
                k.level[u] += k.levelStep[u];
            }

            // The range moves on, one sample of its ramp.
            k.start += k.startStep;
            k.cycle += k.cycleStep;
            k.overlap += k.overlapStep;

            // DC blocker.
            {
                const float yl = l - k.dcX[0] + dcPole * k.dcY[0];
                const float yr = r - k.dcX[1] + dcPole * k.dcY[1];
                k.dcX[0] = l;  k.dcY[0] = yl;  l = yl;
                k.dcX[1] = r;  k.dcY[1] = yr;  r = yr;
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
