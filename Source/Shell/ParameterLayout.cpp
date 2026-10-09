/*
  ------------------------------------------------------------------------------
    ParameterLayout.cpp

    See ParameterLayout.h.

    Author: Olivier Doaré, github.com/odoare
    Dual-licensed, mirroring the JUCE framework it depends on: under the GNU
    AGPL Version 3.0, or under commercial terms available from the author.
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
  ------------------------------------------------------------------------------
*/

#include "ParameterLayout.h"
#include "ParamIDs.h"
#include "Effects/EffectChain.h"

namespace camshaft
{

using namespace cam;

juce::String formatValue (const ParamSpec& spec, float v)
{
    const juce::String unit (spec.unit);

    if (unit == "Hz")
        return v >= 1000.0f ? juce::String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz"
                            : juce::String (v, v < 10.0f ? 2 : (v < 100.0f ? 1 : 0)) + " Hz";
    if (unit == "ms")
        return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " s" : juce::String (v, v < 10.0f ? 1 : 0) + " ms";
    if (unit == "s")
        return juce::String (v, v < 10.0f ? 2 : 1) + " s";
    if (unit == "dB")
        return v <= -59.9f ? juce::String ("-inf dB") : juce::String (v, 1) + " dB";
    if (unit == "%")
        return juce::String (v, 1) + " %";
    if (unit == "ct")
        return juce::String (v, 1) + " ct";
    if (unit == "st")
        return (v > 0 ? "+" : "") + juce::String (juce::roundToInt (v)) + " st";
    if (unit == "smp")
        return juce::String (juce::roundToInt (v)) + " smp";
    if (unit == "x")
        return juce::String (v, 2) + " x";
    if (unit == "beats")
        return juce::String (v, 2) + " beats";

    // Unitless 0..1 or -1..1: show as a percentage.
    if (spec.min >= -1.0f && spec.max <= 1.0f)
        return juce::String (juce::roundToInt (v * 100.0f)) + " %";
    return juce::String (v, 2);
}

std::unique_ptr<juce::RangedAudioParameter> makeParameter (const juce::String& id, const juce::String& name,
                                                           const ParamSpec& spec)
{
    const juce::ParameterID pid { id, 1 };

    switch (spec.mapping)
    {
        case Mapping::boolean:
            return std::make_unique<juce::AudioParameterBool> (pid, name, spec.def > 0.5f);

        case Mapping::choice:
        {
            juce::StringArray choices;
            for (int i = 0; i < spec.numChoices; ++i)
                choices.add (spec.choices[i]);
            return std::make_unique<juce::AudioParameterChoice> (pid, name, choices, juce::roundToInt (spec.def));
        }

        case Mapping::integer:
            return std::make_unique<juce::AudioParameterInt> (
                pid, name, juce::roundToInt (spec.min), juce::roundToInt (spec.max), juce::roundToInt (spec.def),
                juce::AudioParameterIntAttributes().withStringFromValueFunction (
                    [&spec] (int v, int) { return formatValue (spec, (float) v); }));

        case Mapping::linear:
        case Mapping::log:
        case Mapping::power:
        default:
        {
            const ParamSpec* s = &spec;
            juce::NormalisableRange<float> range (
                spec.min, spec.max,
                [s] (float, float, float n) { return s->fromNorm (n); },
                [s] (float, float, float v) { return s->toNorm (v); });
            return std::make_unique<juce::AudioParameterFloat> (
                pid, name, range, spec.def,
                juce::AudioParameterFloatAttributes()
                    .withLabel (spec.unit)
                    .withStringFromValueFunction ([s] (float v, int) { return formatValue (*s, v); }));
        }
    }
}

namespace
{
    using Params = std::vector<std::unique_ptr<juce::RangedAudioParameter>>;

    std::unique_ptr<juce::AudioProcessorParameterGroup> makeGroup (const juce::String& id, const juce::String& name, Params params)
    {
        auto group = std::make_unique<juce::AudioProcessorParameterGroup> (id, name, "|");
        for (auto& p : params)
            group->addChild (std::move (p));
        return group;
    }

    std::unique_ptr<juce::AudioParameterFloat> floatParam (const juce::String& id, const juce::String& name,
                                                           juce::NormalisableRange<float> range, float def,
                                                           const juce::String& label,
                                                           std::function<juce::String (float, int)> toText = nullptr)
    {
        auto attributes = juce::AudioParameterFloatAttributes().withLabel (label);
        if (toText)
            attributes = attributes.withStringFromValueFunction (toText);
        return std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name, range, def, attributes);
    }

    juce::String dbText (float v, int) { return v <= -59.9f ? juce::String ("-inf dB") : juce::String (v, 1) + " dB"; }
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // ---- Cams -------------------------------------------------------------
    for (int c = 0; c < numCams; ++c)
    {
        Params params;
        for (int f = 0; f < numCamFields; ++f)
        {
            const auto& spec = camSpecs[f];
            auto p = makeParameter (ids::cam (c, f), "Cam " + juce::String (c + 1) + " " + spec.name, spec);
            // Cam 1 sounds by default; the others start silent.
            if (f == cfAudible && c > 0)
                p = std::make_unique<juce::AudioParameterBool> (juce::ParameterID { ids::cam (c, f), 1 },
                                                                "Cam " + juce::String (c + 1) + " " + spec.name, false);
            params.push_back (std::move (p));
        }
        layout.add (makeGroup ("cam" + juce::String (c + 1), "Cam " + juce::String (c + 1), std::move (params)));
    }

    // ---- Modulators --------------------------------------------------------
    for (int m = 0; m < numModulators; ++m)
    {
        Params params;
        for (int f = 0; f < numModFields; ++f)
            params.push_back (makeParameter (ids::mod (m, f), "Mod " + juce::String (m + 1) + " " + modSpecs[f].name, modSpecs[f]));
        layout.add (makeGroup ("mod" + juce::String (m + 1), "Modulator " + juce::String (m + 1), std::move (params)));
    }

    // ---- Routes ------------------------------------------------------------
    {
        Params params;
        for (int i = 0; i < maxRoutes; ++i)
            params.push_back (floatParam (ids::routeDepth (i), "Route " + juce::String (i + 1) + " depth",
                                          { -1.0f, 1.0f }, 0.0f, "",
                                          [] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; }));
        layout.add (makeGroup ("routes", "Modulation routes", std::move (params)));
    }

    // ---- Voices and master -------------------------------------------------
    {
        Params params;
        params.push_back (floatParam (ids::masterOut, "Output", { -60.0f, 6.0f, 0.1f }, -3.0f, "dB", dbText));
        params.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { ids::limiter, 1 }, "Rev limiter", true));
        params.push_back (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { ids::voices, 1 }, "Voices", 1, maxVoices, 12));
        params.push_back (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { ids::polyMode, 1 }, "Voice mode",
                                                                        juce::StringArray (voiceModes, countOf (voiceModes)), 0));
        params.push_back (floatParam (ids::glideTime, "Glide", { 0.0f, 5000.0f, 0.0f, 0.35f }, 0.0f, "ms",
                                      [] (float v, int) { return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " s" : juce::String (juce::roundToInt (v)) + " ms"; }));
        params.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { ids::glideSync, 1 }, "Glide sync", false));
        params.push_back (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { ids::glideDiv, 1 }, "Glide division",
                                                                        juce::StringArray (fxme::Lfo::syncDivisionNames, fxme::Lfo::numSyncDivisions), 7));
        params.push_back (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { ids::glideMode, 1 }, "Glide mode",
                                                                        juce::StringArray (glideModes, countOf (glideModes)), 0));
        params.push_back (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { ids::pbRange, 1 }, "Pitch bend range", 0, 24, 2));
        params.push_back (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { ids::interp, 1 }, "Interpolation",
                                                                        juce::StringArray (interpolations, countOf (interpolations)), 1));
        layout.add (makeGroup ("master", "Voices and output", std::move (params)));
    }

    // ---- Effects: Turbo 1 and 2 (aux), Exhaust (master) --------------------
    for (int a = 0; a < numAux; ++a)
    {
        Params params;
        params.push_back (floatParam (ids::auxReturn (a), "Turbo " + juce::String (a + 1) + " return",
                                      { -60.0f, 6.0f, 0.1f }, 0.0f, "dB", dbText));
        params.push_back (floatParam (ids::auxPan (a), "Turbo " + juce::String (a + 1) + " pan", { -1.0f, 1.0f }, 0.0f, ""));
        params.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { ids::auxDryMute (a), 1 },
                                                                      "Turbo " + juce::String (a + 1) + " dry mute", false));
        EffectChain::addParameters (params, ids::auxPrefix (a));
        layout.add (makeGroup (ids::auxPrefix (a), "Turbo " + juce::String (a + 1), std::move (params)));
    }
    {
        Params params;
        EffectChain::addParameters (params, ids::masterPrefix);
        layout.add (makeGroup ("exhaust", "Exhaust", std::move (params)));
    }

    return layout;
}

} // namespace camshaft
