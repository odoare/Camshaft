/*
  ------------------------------------------------------------------------------
    Destinations.cpp

    See Destinations.h.

    Author: Olivier Doaré, github.com/odoare
    Dual-licensed, mirroring the JUCE framework it depends on: under the GNU
    AGPL Version 3.0, or under commercial terms available from the author.
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
  ------------------------------------------------------------------------------
*/

#include "Destinations.h"
#include "ParamIDs.h"
#include <unordered_set>

namespace camshaft
{

using namespace cam;

Destinations::Destinations (juce::AudioProcessorValueTreeState& apvts)
{
    auto add = [&] (const juce::String& id, const juce::String& name, const juce::String& group, int flat, bool perVoice)
    {
        Entry e;
        e.paramID = id;
        e.name = name;
        e.group = group;
        e.flat = flat;
        e.param = apvts.getParameter (id);
        e.raw = apvts.getRawParameterValue (id);
        e.perVoice = perVoice;
        if (e.param == nullptr || e.raw == nullptr)
            return;
        byId[id.toStdString()] = (int) entries.size();
        byFlat[flat] = (int) entries.size();
        entries.push_back (e);
    };

    std::unordered_set<std::string> engineIds;   // parameters the engine reads itself

    for (int c = 0; c < numCams; ++c)
        for (int f = 0; f < numCamFields; ++f)
        {
            engineIds.insert (ids::cam (c, f).toStdString());
            if (camSpecs[f].modulatable)
                add (ids::cam (c, f), camSpecs[f].name, "Cam " + juce::String (c + 1), camDest (c, f), true);
        }

    for (int m = 0; m < numModulators; ++m)
        for (int f = 0; f < numModFields; ++f)
        {
            engineIds.insert (ids::mod (m, f).toStdString());
            if (modSpecs[f].modulatable)
                add (ids::mod (m, f), modSpecs[f].name, "Modulator " + juce::String (m + 1), modDest (m, f), true);
        }

    for (int i = 0; i < maxRoutes; ++i)
        engineIds.insert (ids::routeDepth (i).toStdString());

    // Everything else that is continuous: a global destination.
    int g = 0;
    for (auto* p : apvts.processor.getParameters())
    {
        auto* fp = dynamic_cast<juce::AudioParameterFloat*> (p);
        if (fp == nullptr || engineIds.count (fp->paramID.toStdString()) > 0)
            continue;

        juce::String group = "Output";
        const auto id = fp->paramID;
        if (id.startsWith ("aux1"))        group = "Turbo 1";
        else if (id.startsWith ("aux2"))   group = "Turbo 2";
        else if (id.startsWith ("master_fx")) group = "Exhaust";
        else if (id.startsWith ("glide"))  group = "Voices";

        const int before = (int) entries.size();
        add (id, fp->getName (64), group, globalDestBase + g, false);
        if ((int) entries.size() > before)
        {
            globals.push_back (before);
            ++g;
        }
    }
}

int Destinations::flatFor (const juce::String& paramID) const
{
    const auto it = byId.find (paramID.toStdString());
    return it != byId.end() ? entries[(size_t) it->second].flat : -1;
}

const Destinations::Entry* Destinations::entryFor (int flat) const
{
    const auto it = byFlat.find (flat);
    return it != byFlat.end() ? &entries[(size_t) it->second] : nullptr;
}

} // namespace camshaft
