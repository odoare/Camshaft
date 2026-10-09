/*
  ------------------------------------------------------------------------------
    Destinations.h

    Every parameter the modulation matrix can target, with its flat index in
    the engine's destination space (Engine/CamParams.h):

      - cam fields and modulator fields marked modulatable in the spec
        tables: the engine applies these itself, per voice;
      - every other continuous parameter (output, aux returns, glide, the
        effects' parameters): "global" destinations, modulated by the shell
        once per block through the parameter's raw value (the value the
        DSP reads), recomputed each block from the parameter's own value so
        host automation and the GUI are never fought (MechanOdd's method).

    Built once from the APVTS, read-only afterwards.

    Author: Olivier Doaré, github.com/odoare
    Dual-licensed, mirroring the JUCE framework it depends on: under the GNU
    AGPL Version 3.0, or under commercial terms available from the author.
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>
#include "Engine/CamParams.h"

namespace camshaft
{

class Destinations
{
public:
    struct Entry
    {
        juce::String paramID;
        juce::String name;
        juce::String group;                    // menu section ("Cam 2", "Turbo 1", ...)
        int flat = -1;
        juce::RangedAudioParameter* param = nullptr;
        std::atomic<float>* raw = nullptr;
        bool perVoice = false;
    };

    explicit Destinations (juce::AudioProcessorValueTreeState& apvts);

    /** Flat index for a parameter ID, -1 when it cannot be modulated. */
    int flatFor (const juce::String& paramID) const;

    /** The entry of a flat index (nullptr when there is none). */
    const Entry* entryFor (int flat) const;

    const std::vector<Entry>& all() const noexcept { return entries; }

    /** Global destinations, by global index g (flat = globalDestBase + g). */
    int numGlobal() const noexcept { return (int) globals.size(); }
    const Entry& global (int g) const { return entries[(size_t) globals[(size_t) g]]; }

private:
    std::vector<Entry> entries;
    std::vector<int> globals;                  // entry indices of the global ones
    std::unordered_map<std::string, int> byId; // param ID -> entry index
    std::unordered_map<int, int> byFlat;       // flat -> entry index
};

} // namespace camshaft
