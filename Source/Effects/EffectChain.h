/*
  ------------------------------------------------------------------------------
    EffectChain.h

    Two effect slots in series. Camshaft has three chains: the two aux
    chains ("Turbo 1", "Turbo 2", prefixes aux1 / aux2) and the master one
    ("Exhaust", prefix master). Slot parameter IDs are "<chain>_fx<n>_...".

    Author: Olivier Doaré, github.com/odoare
    Dual-licensed, mirroring the JUCE framework it depends on: under the GNU
    AGPL Version 3.0, or under commercial terms available from the author.
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>
#include "EffectSlot.h"

namespace camshaft
{

class EffectChain
{
public:
    static constexpr int numSlots = 2;

    void prepare (double sampleRate, int numChannels, int blockSize)
    {
        for (auto& s : slots)
            s.prepare (sampleRate, numChannels, blockSize);
    }

    void assignParameters (juce::AudioProcessorValueTreeState& apvts, const juce::String& chainPrefix)
    {
        for (int i = 0; i < numSlots; ++i)
            slots[(size_t) i].assignParameters (apvts, slotPrefix (chainPrefix, i));
    }

    void checkParameters()
    {
        for (auto& s : slots)
            s.checkParameters();
    }

    void process (juce::AudioBuffer<float>& buffer)
    {
        for (auto& s : slots)
            s.process (buffer);
    }

    /** See EffectSlot::loadActiveEffect; never on the audio thread. */
    void loadActiveEffects()
    {
        for (auto& s : slots)
            s.loadActiveEffect();
    }

    EffectSlot& getSlot (int slot) { return slots[(size_t) slot]; }

    static void addParameters (std::vector<std::unique_ptr<juce::RangedAudioParameter>>& params, const juce::String& chainPrefix)
    {
        for (int i = 0; i < numSlots; ++i)
            EffectSlot::addParameters (params, slotPrefix (chainPrefix, i));
    }

    static juce::String slotPrefix (const juce::String& chainPrefix, int slot) { return chainPrefix + "_fx" + juce::String (slot); }

private:
    std::array<EffectSlot, numSlots> slots;
};

} // namespace camshaft
