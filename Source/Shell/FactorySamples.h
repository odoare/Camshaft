/*
  ------------------------------------------------------------------------------
    FactorySamples.h

    The audio files bundled with Camshaft (the WAV files of Source/Assets,
    in the binary data), which a patch can use without embedding them: its
    state names the sample by its ID (SourceOptions factory="<id>") and the
    file is read from the plugin itself. The factory presets use them this
    way, and a user preset made on one stays small.

    IDs are stored in sessions and presets: never change or remove one (a
    missing sample falls back to the built-in morph, with a message). Display
    names may change freely. Append only.

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

struct FactorySample
{
    const char* id;          // stored in states: permanent
    const char* name;        // shown in the menu and over the waveform
    const char* resource;    // the binary-data resource (file name, '.' -> '_')
    const char* fileName;    // the file it came from, for exporting it again
};

inline constexpr FactorySample factorySamples[] = {
    { "camshaft", "Camshaft", "Camshaft_wav", "Camshaft.wav" },
    { "absurde",  "Absurde",  "absurde_wav",  "absurde.wav"  },
};

inline const FactorySample* findFactorySample (const juce::String& id)
{
    for (const auto& s : factorySamples)
        if (id == s.id)
            return &s;
    return nullptr;
}

} // namespace camshaft
