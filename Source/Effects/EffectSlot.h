/*
  ------------------------------------------------------------------------------
    EffectSlot.h

    One effect slot (after MechanOdd's): an instance of every registered
    effect, the active one chosen by a "<slot>_type" choice ("Off" first).
    Only the active effect processes; Off passes the signal through.

    Author: Olivier Doaré, github.com/odoare
    Dual-licensed, mirroring the JUCE framework it depends on: under the GNU
    AGPL Version 3.0, or under commercial terms available from the author.
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>
#include "Effect.h"

namespace camshaft
{

class EffectSlot
{
public:
    EffectSlot();

    void prepare (double sampleRate, int numChannels, int blockSize);
    void assignParameters (juce::AudioProcessorValueTreeState& apvts, const juce::String& slotPrefix);
    void checkParameters();
    void process (juce::AudioBuffer<float>& buffer);

    /** Completes the deferred setup of the effect the slot is set to (an IR
        effect loads its IR the first time). Never on the audio thread. */
    void loadActiveEffect();

    /** The slot's instance of effect `type` (index in EffectFactory::types()). */
    Effect& getEffect (int type) { return *effects[(size_t) type]; }

    /** 0 = Off, else 1 + index in EffectFactory::types(). */
    int getActiveType() const noexcept { return activeType; }

    static void addParameters (std::vector<std::unique_ptr<juce::RangedAudioParameter>>& params, const juce::String& slotPrefix);

    static juce::String typeParamId (const juce::String& slotPrefix) { return slotPrefix + "_type"; }
    static juce::String perTypePrefix (const juce::String& slotPrefix, const juce::String& typeName) { return slotPrefix + "_" + typeName; }

private:
    std::vector<std::unique_ptr<Effect>> effects;
    std::atomic<float>* typeParam { nullptr };
    int activeType { 0 };
};

} // namespace camshaft
