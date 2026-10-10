/*
  ------------------------------------------------------------------------------
    CamParams.h

    The single description of Camshaft's per-cam and per-modulator parameters:
    key (the part of the parameter ID after "r1_" or "mod1_"), display name,
    range, mapping between value and normalised position, default, and
    whether the modulation matrix may target it. The plugin shell builds the
    host parameters from these tables and the engine uses the very same
    mappings to apply modulation, so the two can never disagree.

    Also: the engine's sizes, the modulation sources, and the choice lists.

    JUCE-free on purpose (see doc/architecture.md): the engine under
    Source/Engine builds against FxmeTools' core only.

    Parameter IDs are frozen once released: never rename a key, never reorder
    a choice list (only append), never change a range without a state
    migration.

    Author: Olivier Doaré, github.com/odoare
    Licenced under the GNU LGPL Version 3.0
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

#include <FxmeTools/dsp/Lfo.h>
#include <FxmeTools/dsp/MultiModeFilter.h>
#include <FxmeTools/synth/ModulationLfo.h>
#include <cmath>
#include <cstddef>

namespace cam
{

//==============================================================================
// Sizes

constexpr int numCams       = 4;
constexpr int numModulators = 16;
constexpr int maxVoices     = 24;
constexpr int maxCylinders  = 4;     // unison sub-voices per cam
constexpr int maxRoutes     = 64;
constexpr int numCcSources  = 4;     // assignable MIDI CC sources
constexpr int numAux        = 2;
constexpr int controlBlock  = 32;    // samples between modulation updates

//==============================================================================
// Value <-> normalised mappings

enum class Mapping { linear, log, power, integer, boolean, choice };

struct ParamSpec
{
    const char* key;
    const char* name;
    Mapping mapping;
    float min, max, def;
    float skew;                     // power mapping: n = p^skew (as JUCE's skew factor)
    bool modulatable;
    const char* unit;
    const char* const* choices = nullptr;
    int numChoices = 0;

    float toNorm (float v) const noexcept
    {
        if (max <= min)
            return 0.0f;
        v = v < min ? min : (v > max ? max : v);
        switch (mapping)
        {
            case Mapping::log:   return std::log (v / min) / std::log (max / min);
            case Mapping::power: return std::pow ((v - min) / (max - min), skew);
            case Mapping::linear:
            case Mapping::integer:
            case Mapping::boolean:
            case Mapping::choice:
            default:             return (v - min) / (max - min);
        }
    }

    float fromNorm (float n) const noexcept
    {
        n = n < 0.0f ? 0.0f : (n > 1.0f ? 1.0f : n);
        float v;
        switch (mapping)
        {
            case Mapping::log:   v = min * std::pow (max / min, n); break;
            case Mapping::power: v = min + (max - min) * std::pow (n, 1.0f / skew); break;
            case Mapping::integer:
            case Mapping::boolean:
            case Mapping::choice: v = std::round (min + (max - min) * n); break;
            case Mapping::linear:
            default:             v = min + (max - min) * n; break;
        }
        return v < min ? min : (v > max ? max : v);
    }
};

//==============================================================================
// Choice lists (append only)

inline constexpr const char* const offOn[]          = { "Off", "On" };
inline constexpr const char* const overlapShapes[]  = { "Linear", "Equal power" };
inline constexpr const char* const snapModes[]      = { "Off", "Zero crossing", "Period" };
inline constexpr const char* const tapPoints[]      = { "Pre-filter", "Post-filter", "Post-gain" };
inline constexpr const char* const filterRoutings[] = { "Series", "Parallel" };
inline constexpr const char* const modTypes[]       = { "LFO", "ADSR", "Curve", "Off" };
inline constexpr const char* const lfoModes[]       = { "Per voice", "Global" };
inline constexpr const char* const durationModes[]  = { "Seconds", "Beats" };
inline constexpr const char* const voiceModes[]     = { "Poly", "Mono", "Legato" };
inline constexpr const char* const glideModes[]     = { "Always", "Legato only" };
inline constexpr const char* const interpolations[] = { "Linear", "Cubic", "Sinc" };
inline constexpr const char* const xmodTypes[]      = { "Off", "FM", "Ring", "AM" };

template <typename T, std::size_t N> constexpr int countOf (T (&)[N]) { return (int) N; }

/** Exactly zero, without a floating-point equality (-Wfloat-equal). */
inline constexpr bool isZero (float v) noexcept { return ! (v > 0.0f || v < 0.0f); }

//==============================================================================
// Per-cam fields: parameter "r<n>_<key>"

enum CamField
{
    cfAudible = 0, cfOpening, cfDuration, cfOverlap, cfOverlapShape, cfSnap,
    cfCoarse, cfFine, cfCylinders, cfDetune, cfSpread, cfRandomPhase,
    cfVolume, cfPan, cfSend1, cfSend2, cfTap1, cfTap2,
    cfAttack, cfDecay, cfSustain, cfRelease, cfAttackCurve, cfDecayCurve, cfReleaseCurve, cfVelocity,
    cfFilterRouting, cfFilterBalance,
    cfF1On, cfF1Type, cfF1Cutoff, cfF1Res, cfF1Gain, cfF1KeyTrack, cfF1EnvAmount, cfF1VowelA, cfF1VowelB, cfF1VowelPos,
    cfF2On, cfF2Type, cfF2Cutoff, cfF2Res, cfF2Gain, cfF2KeyTrack, cfF2EnvAmount, cfF2VowelA, cfF2VowelB, cfF2VowelPos,
    cfLift1, cfLift2, cfLift3, cfLift4,
    numCamFields
};

constexpr int filterFieldStride = cfF2On - cfF1On;   // offset from filter 1's fields to filter 2's

inline constexpr ParamSpec camSpecs[numCamFields] = {
    { "audible",     "Audible",        Mapping::boolean, 0, 1, 1, 1, false, "", offOn, 2 },
    { "opening",     "Opening",        Mapping::linear,  0, 1, 0.5f, 1, true, "" },
    { "duration",    "Duration",       Mapping::log,     16, 1048576, 2048, 1, true, "smp" },
    { "overlap",     "Overlap",        Mapping::linear,  0, 50, 10, 1, true, "%" },
    { "xfshape",     "Overlap shape",  Mapping::choice,  0, 1, 1, 1, false, "", overlapShapes, 2 },
    { "snap",        "Snap",           Mapping::choice,  0, 2, 0, 1, false, "", snapModes, 3 },
    { "coarse",      "Coarse",         Mapping::integer, -48, 48, 0, 1, true, "st" },
    { "fine",        "Fine",           Mapping::linear,  -100, 100, 0, 1, true, "ct" },
    { "cyl",         "Cylinders",      Mapping::integer, 1, (float) maxCylinders, 1, 1, false, "" },
    { "detune",      "Detune",         Mapping::linear,  0, 100, 12, 1, true, "ct" },
    { "spread",      "Spread",         Mapping::linear,  0, 1, 0.5f, 1, true, "" },
    { "randphase",   "Random phase",   Mapping::boolean, 0, 1, 1, 1, false, "", offOn, 2 },
    { "volume",      "Volume",         Mapping::linear,  -60, 6, -6, 1, true, "dB" },
    { "pan",         "Pan",            Mapping::linear,  -1, 1, 0, 1, true, "" },
    { "send1",       "Turbo 1",        Mapping::linear,  0, 1, 0, 1, true, "" },
    { "send2",       "Turbo 2",        Mapping::linear,  0, 1, 0, 1, true, "" },
    { "send1_tap",   "Turbo 1 tap",    Mapping::choice,  0, 2, 2, 1, false, "", tapPoints, 3 },
    { "send2_tap",   "Turbo 2 tap",    Mapping::choice,  0, 2, 2, 1, false, "", tapPoints, 3 },
    { "att",         "Attack",         Mapping::power,   0, 10000, 5, 0.3f, true, "ms" },
    { "dec",         "Decay",          Mapping::power,   0, 10000, 300, 0.3f, true, "ms" },
    { "sus",         "Sustain",        Mapping::linear,  0, 1, 0.8f, 1, true, "" },
    { "rel",         "Release",        Mapping::power,   0, 20000, 400, 0.3f, true, "ms" },
    { "acurve",      "Attack curve",   Mapping::linear,  -1, 1, -0.3f, 1, false, "" },
    { "dcurve",      "Decay curve",    Mapping::linear,  -1, 1, -0.5f, 1, false, "" },
    { "rcurve",      "Release curve",  Mapping::linear,  -1, 1, -0.5f, 1, false, "" },
    { "velamt",      "Velocity",       Mapping::linear,  0, 1, 0.7f, 1, true, "" },
    { "froute",      "Filter routing", Mapping::choice,  0, 1, 0, 1, false, "", filterRoutings, 2 },
    { "fbal",        "Filter balance", Mapping::linear,  -1, 1, 0, 1, true, "" },

    { "f1_on",       "F1 on",          Mapping::boolean, 0, 1, 0, 1, false, "", offOn, 2 },
    { "f1_type",     "F1 type",        Mapping::choice,  0, (float) fxme::MultiModeFilter::numTypes - 1, 0, 1, false, "",
                                       fxme::MultiModeFilter::typeNames, fxme::MultiModeFilter::numTypes },
    { "f1_cutoff",   "F1 cutoff",      Mapping::log,     20, 20000, 6000, 1, true, "Hz" },
    { "f1_res",      "F1 resonance",   Mapping::linear,  0, 1, 0.1f, 1, true, "" },
    { "f1_gain",     "F1 gain",        Mapping::linear,  -24, 24, 0, 1, true, "dB" },
    { "f1_keytrk",   "F1 key track",   Mapping::linear,  -1, 1, 0, 1, true, "" },
    { "f1_envamt",   "F1 env amount",  Mapping::linear,  -1, 1, 0, 1, true, "" },
    { "f1_vowela",   "F1 vowel start", Mapping::choice,  0, 4, 0, 1, false, "", fxme::MultiModeFilter::vowelNames, 5 },
    { "f1_vowelb",   "F1 vowel end",   Mapping::choice,  0, 4, 3, 1, false, "", fxme::MultiModeFilter::vowelNames, 5 },
    { "f1_vowelpos", "F1 vowel pos",   Mapping::linear,  0, 1, 0, 1, true, "" },

    { "f2_on",       "F2 on",          Mapping::boolean, 0, 1, 0, 1, false, "", offOn, 2 },
    { "f2_type",     "F2 type",        Mapping::choice,  0, (float) fxme::MultiModeFilter::numTypes - 1, 2, 1, false, "",
                                       fxme::MultiModeFilter::typeNames, fxme::MultiModeFilter::numTypes },
    { "f2_cutoff",   "F2 cutoff",      Mapping::log,     20, 20000, 120, 1, true, "Hz" },
    { "f2_res",      "F2 resonance",   Mapping::linear,  0, 1, 0.1f, 1, true, "" },
    { "f2_gain",     "F2 gain",        Mapping::linear,  -24, 24, 0, 1, true, "dB" },
    { "f2_keytrk",   "F2 key track",   Mapping::linear,  -1, 1, 0, 1, true, "" },
    { "f2_envamt",   "F2 env amount",  Mapping::linear,  -1, 1, 0, 1, true, "" },
    { "f2_vowela",   "F2 vowel start", Mapping::choice,  0, 4, 1, 1, false, "", fxme::MultiModeFilter::vowelNames, 5 },
    { "f2_vowelb",   "F2 vowel end",   Mapping::choice,  0, 4, 4, 1, false, "", fxme::MultiModeFilter::vowelNames, 5 },
    { "f2_vowelpos", "F2 vowel pos",   Mapping::linear,  0, 1, 0, 1, true, "" },

    { "xm1_lift",    "Lift from Cam 1", Mapping::linear, 0, 1, 0.3f, 1, true, "" },
    { "xm2_lift",    "Lift from Cam 2", Mapping::linear, 0, 1, 0.3f, 1, true, "" },
    { "xm3_lift",    "Lift from Cam 3", Mapping::linear, 0, 1, 0.3f, 1, true, "" },
    { "xm4_lift",    "Lift from Cam 4", Mapping::linear, 0, 1, 0.3f, 1, true, "" },
};

/** A cam's range in a recording of `numSamples` samples, from its opening
    (start, 0 to 1 of the recording) and duration (length, samples): the
    range starts where it opens and runs for its duration, cut short at the
    end of the recording; at least 16 samples. Before any snapping. The
    engine and the editor's displays both use it. */
inline void camRange (float startParam, float lengthParam, double numSamples, double& start, double& length) noexcept
{
    constexpr double minLength = 16.0;
    const double n = numSamples > minLength ? numSamples : minLength;
    const double s = (double) (startParam < 0.0f ? 0.0f : (startParam > 1.0f ? 1.0f : startParam)) * n;
    start = s < n - minLength ? s : n - minLength;
    const double room = n - start;
    length = (double) lengthParam < minLength ? minLength : ((double) lengthParam > room ? room : (double) lengthParam);
}

//==============================================================================
// Per-modulator fields: parameter "mod<n>_<key>"

enum ModField
{
    mfType = 0, mfShape, mfRate, mfSync, mfDivision, mfPhase, mfDelay, mfFade, mfBipolar, mfMode,
    mfAttack, mfDecay, mfSustain, mfRelease, mfAttackCurve, mfDecayCurve, mfReleaseCurve, mfVelocity,
    mfLevel, mfDuration, mfDurationMode, mfBeats, mfDawSync, mfSpeed,
    numModFields
};

/** Values of the "type" field (modTypes). */
enum ModType { mtLfo = 0, mtAdsr, mtCurve, mtOff };

inline constexpr ParamSpec modSpecs[numModFields] = {
    { "type",    "Type",          Mapping::choice,  0, 3, 3, 1, false, "", modTypes, 4 },
    { "shape",   "Shape",         Mapping::choice,  0, (float) fxme::ModulationLfo::numShapes - 1, 0, 1, false, "",
                                  fxme::ModulationLfo::shapeNames, fxme::ModulationLfo::numShapes },
    { "rate",    "Rate",          Mapping::log,     0.01f, 50, 2, 1, true, "Hz" },
    { "sync",    "Sync",          Mapping::boolean, 0, 1, 0, 1, false, "", offOn, 2 },
    { "div",     "Division",      Mapping::choice,  0, (float) fxme::Lfo::numSyncDivisions - 1, 5, 1, false, "",
                                  fxme::Lfo::syncDivisionNames, fxme::Lfo::numSyncDivisions },
    { "phase",   "Phase",         Mapping::linear,  0, 1, 0, 1, true, "" },
    { "delay",   "Delay",         Mapping::power,   0, 10000, 0, 0.4f, false, "ms" },
    { "fade",    "Fade in",       Mapping::power,   0, 10000, 0, 0.4f, false, "ms" },
    { "bipolar", "Bipolar",       Mapping::boolean, 0, 1, 1, 1, false, "", offOn, 2 },
    { "mode",    "Mode",          Mapping::choice,  0, 1, 0, 1, false, "", lfoModes, 2 },
    { "att",     "Attack",        Mapping::power,   0, 10000, 10, 0.3f, true, "ms" },
    { "dec",     "Decay",         Mapping::power,   0, 10000, 400, 0.3f, true, "ms" },
    { "sus",     "Sustain",       Mapping::linear,  0, 1, 0.5f, 1, true, "" },
    { "rel",     "Release",       Mapping::power,   0, 20000, 500, 0.3f, true, "ms" },
    { "acurve",  "Attack curve",  Mapping::linear,  -1, 1, -0.3f, 1, false, "" },
    { "dcurve",  "Decay curve",   Mapping::linear,  -1, 1, -0.5f, 1, false, "" },
    { "rcurve",  "Release curve", Mapping::linear,  -1, 1, -0.5f, 1, false, "" },
    { "velamt",  "Velocity",      Mapping::linear,  0, 1, 0, 1, false, "" },
    { "level",   "Level",         Mapping::linear,  0, 1, 1, 1, true, "" },
    { "dur",     "Duration",      Mapping::power,   0.01f, 60, 2, 0.4f, false, "s" },
    { "durmode", "Duration in",   Mapping::choice,  0, 1, 0, 1, false, "", durationModes, 2 },
    { "beats",   "Beats",         Mapping::power,   0.25f, 64, 4, 0.5f, false, "beats" },
    { "daw",     "Stick to DAW",  Mapping::boolean, 0, 1, 0, 1, false, "", offOn, 2 },
    { "speed",   "Speed",         Mapping::log,     0.25f, 4, 1, 1, true, "x" },
};

//==============================================================================
// Modulation sources

enum ModSource
{
    srcNone = 0,
    srcModFirst = 1,                                   // mod1 .. mod16
    srcVelocity = srcModFirst + numModulators,
    srcReleaseVelocity, srcKeyTrack, srcAftertouch, srcPolyAftertouch, srcPitchBend, srcModWheel,
    srcCcFirst,                                        // CC A .. D
    srcAmpEnvFirst = srcCcFirst + numCcSources,        // amp envelopes of cams 1 .. 4
    srcRandom = srcAmpEnvFirst + numCams,
    numSources
};

inline bool isPerVoiceSource (int s, const bool* globalModulator) noexcept
{
    if (s >= srcModFirst && s < srcModFirst + numModulators)
        return ! globalModulator[s - srcModFirst];
    return s == srcVelocity || s == srcReleaseVelocity || s == srcKeyTrack || s == srcPolyAftertouch
        || (s >= srcAmpEnvFirst && s < srcAmpEnvFirst + numCams) || s == srcRandom;
}

//==============================================================================
// Destinations: one flat index space.
//   [0, numCamDests)                         cam c, field f   -> c * numCamFields + f
//   [modDestBase, modDestBase + numModDests) modulator m, field f
//   [globalDestBase, ...)                    the shell's list of other parameters

constexpr int numCamDests    = numCams * numCamFields;
constexpr int modDestBase    = numCamDests;
constexpr int numModDests    = numModulators * numModFields;
constexpr int globalDestBase = modDestBase + numModDests;

constexpr int camDest (int cam, int field) noexcept { return cam * numCamFields + field; }
constexpr int modDest (int mod, int field) noexcept { return modDestBase + mod * numModFields + field; }

} // namespace cam
