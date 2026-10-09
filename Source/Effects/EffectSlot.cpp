/*
  ------------------------------------------------------------------------------
    EffectSlot.cpp

    See EffectSlot.h.

    Author: Olivier Doaré, github.com/odoare
    Dual-licensed, mirroring the JUCE framework it depends on: under the GNU
    AGPL Version 3.0, or under commercial terms available from the author.
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
  ------------------------------------------------------------------------------
*/

#include "EffectSlot.h"
#include "EffectFactory.h"

namespace camshaft
{

EffectSlot::EffectSlot()
{
    for (const auto& info : EffectFactory::types())
        effects.push_back (info.create());
}

void EffectSlot::prepare (double sampleRate, int numChannels, int blockSize)
{
    for (auto& e : effects)
        e->prepare (sampleRate, numChannels, blockSize);
}

void EffectSlot::assignParameters (juce::AudioProcessorValueTreeState& apvts, const juce::String& slotPrefix)
{
    typeParam = apvts.getRawParameterValue (typeParamId (slotPrefix));

    const auto& infos = EffectFactory::types();
    for (size_t i = 0; i < effects.size(); ++i)
        effects[i]->assignParameters (apvts, perTypePrefix (slotPrefix, infos[i].name));
}

void EffectSlot::checkParameters()
{
    if (typeParam == nullptr)
        return;

    activeType = (int) typeParam->load();
    if (activeType > 0 && activeType <= (int) effects.size())
        effects[(size_t) (activeType - 1)]->checkParameters();
}

void EffectSlot::loadActiveEffect()
{
    if (typeParam == nullptr)
        return;

    const int type = (int) typeParam->load();
    if (type > 0 && type <= (int) effects.size())
        effects[(size_t) (type - 1)]->ensureLoaded();
}

void EffectSlot::process (juce::AudioBuffer<float>& buffer)
{
    if (activeType > 0 && activeType <= (int) effects.size())
        effects[(size_t) (activeType - 1)]->process (buffer);
}

void EffectSlot::addParameters (std::vector<std::unique_ptr<juce::RangedAudioParameter>>& params, const juce::String& slotPrefix)
{
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { typeParamId (slotPrefix), 1 }, slotPrefix + " type", EffectFactory::typeChoices(), 0));

    for (const auto& info : EffectFactory::types())
        info.addParameters (params, perTypePrefix (slotPrefix, info.name));
}

} // namespace camshaft
