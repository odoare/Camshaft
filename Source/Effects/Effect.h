/*
  ------------------------------------------------------------------------------
    Effect.h

    Uniform wrapper over the reused FxmeFX effect classes (after MechanOdd's
    Effects/Effect.h). Those classes share process / assignParameters /
    checkParameters / static addParameters but not prepare(), whose
    arguments differ: (sampleRate), (sampleRate, numChannels),
    (sampleRate, samplesPerBlock) or (sampleRate, samplesPerBlock,
    numChannels). The two two-argument forms cannot be told apart by their
    signature, so each registry entry states which one its effect has
    (PrepareArgs) rather than letting overload resolution guess.

    Author: Olivier Doaré, github.com/odoare
    Dual-licensed, mirroring the JUCE framework it depends on: under the GNU
    AGPL Version 3.0, or under commercial terms available from the author.
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>

namespace camshaft
{

class Effect
{
public:
    virtual ~Effect() = default;

    virtual void prepare (double sampleRate, int numChannels, int blockSize) = 0;
    virtual void process (juce::AudioBuffer<float>& buffer) = 0;
    virtual void assignParameters (juce::AudioProcessorValueTreeState& apvts, const juce::String& prefix) = 0;
    virtual void checkParameters() = 0;

    /** For an effect that defers costly setup until it is used (Cab and
        ConvolReverb load an IR): does it now. Never on the audio thread. */
    virtual void ensureLoaded() {}
};

enum class PrepareArgs { rate, rateChannels, rateBlock, rateBlockChannels };

template <class T, PrepareArgs Args>
class EffectAdapter : public Effect
{
public:
    T& get() { return impl; }

    void prepare (double sampleRate, int numChannels, int blockSize) override
    {
        if constexpr (Args == PrepareArgs::rate)                   impl.prepare (sampleRate);
        else if constexpr (Args == PrepareArgs::rateChannels)      impl.prepare (sampleRate, numChannels);
        else if constexpr (Args == PrepareArgs::rateBlock)         impl.prepare (sampleRate, blockSize);
        else                                                       impl.prepare (sampleRate, blockSize, numChannels);
    }

    void process (juce::AudioBuffer<float>& buffer) override { impl.process (buffer); }
    void assignParameters (juce::AudioProcessorValueTreeState& apvts, const juce::String& prefix) override { impl.assignParameters (apvts, prefix); }
    void checkParameters() override { impl.checkParameters(); }

private:
    T impl;
};

} // namespace camshaft
