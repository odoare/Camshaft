/*
  ------------------------------------------------------------------------------
    ParamIDs.h

    Every parameter ID, and the stable keys of the modulation sources.

    The scheme is <section><index>_<name>, lowercase, frozen once released:
      r1_center, r1_width, r1_f1_cutoff ...   cam 1 (CamParams.h, camSpecs)
      mod3_rate ...                           modulator 3 (modSpecs)
      route12_depth                           modulation route 12
      aux1_return, aux1_fx0_type ...          Turbo 1 (and its effect slots)
      master_out, master_fx1_type ...         Exhaust and the output
    Display names may change freely; IDs never do.

    Author: Olivier Doaré, github.com/odoare
    Dual-licensed, mirroring the JUCE framework it depends on: under the GNU
    AGPL Version 3.0, or under commercial terms available from the author.
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>
#include "Engine/CamParams.h"

namespace camshaft::ids
{
    inline juce::String cam (int c, int field)   { return "r" + juce::String (c + 1) + "_" + cam::camSpecs[field].key; }
    inline juce::String mod (int m, int field)   { return "mod" + juce::String (m + 1) + "_" + cam::modSpecs[field].key; }
    inline juce::String routeDepth (int i)       { return "route" + juce::String (i + 1) + "_depth"; }

    inline juce::String auxPrefix (int a)        { return "aux" + juce::String (a + 1); }
    inline juce::String auxReturn (int a)        { return auxPrefix (a) + "_return"; }
    inline juce::String auxPan (int a)           { return auxPrefix (a) + "_pan"; }
    inline juce::String auxDryMute (int a)       { return auxPrefix (a) + "_drymute"; }
    inline constexpr const char* masterPrefix = "master";

    inline constexpr const char* masterOut  = "master_out";
    inline constexpr const char* limiter    = "master_limiter";
    inline constexpr const char* voices     = "voices";
    inline constexpr const char* polyMode   = "polymode";
    inline constexpr const char* glideTime  = "glide_time";
    inline constexpr const char* glideSync  = "glide_sync";
    inline constexpr const char* glideDiv   = "glide_div";
    inline constexpr const char* glideMode  = "glide_mode";
    inline constexpr const char* pbRange    = "pb_range";
    inline constexpr const char* interp     = "interp";

    /** Stable key of modulation source s (stored in states and presets). */
    inline juce::String sourceKey (int s)
    {
        using namespace cam;
        if (s <= srcNone || s >= numSources)          return "none";
        if (s < srcModFirst + numModulators)          return "mod" + juce::String (s - srcModFirst + 1);
        if (s >= srcCcFirst && s < srcCcFirst + numCcSources) return "cc" + juce::String (s - srcCcFirst + 1);
        if (s >= srcAmpEnvFirst && s < srcAmpEnvFirst + numCams) return "env" + juce::String (s - srcAmpEnvFirst + 1);
        switch (s)
        {
            case srcVelocity:        return "velocity";
            case srcReleaseVelocity: return "relvel";
            case srcKeyTrack:        return "keytrack";
            case srcAftertouch:      return "aftertouch";
            case srcPolyAftertouch:  return "polyat";
            case srcPitchBend:       return "pitchbend";
            case srcModWheel:        return "modwheel";
            case srcRandom:          return "random";
            default:                 return "none";
        }
    }

    inline int sourceFromKey (const juce::String& key)
    {
        for (int s = 1; s < cam::numSources; ++s)
            if (sourceKey (s) == key)
                return s;
        return cam::srcNone;
    }

    /** Display name of a source that is not a modulator slot. Modulator
        slots are named after their type by the caller ("LFO 2"). */
    inline juce::String fixedSourceName (int s)
    {
        using namespace cam;
        if (s >= srcCcFirst && s < srcCcFirst + numCcSources)
            return "CC " + juce::String::charToString ((juce::juce_wchar) ('A' + s - srcCcFirst));
        if (s >= srcAmpEnvFirst && s < srcAmpEnvFirst + numCams)
            return "Cam " + juce::String (s - srcAmpEnvFirst + 1) + " env";
        switch (s)
        {
            case srcVelocity:        return "Velocity";
            case srcReleaseVelocity: return "Release velocity";
            case srcKeyTrack:        return "Key track";
            case srcAftertouch:      return "Aftertouch";
            case srcPolyAftertouch:  return "Poly aftertouch";
            case srcPitchBend:       return "Pitch bend";
            case srcModWheel:        return "Mod wheel";
            case srcRandom:          return "Random";
            default:                 return "None";
        }
    }
}
