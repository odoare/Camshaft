/*
  ------------------------------------------------------------------------------
    ParameterLayout.h

    Builds Camshaft's host parameters, grouped so the host's automation list
    stays navigable (one group per cam, per modulator, for the routes, the
    master section, and each effect chain): the cam and modulator ones from
    the engine's spec tables (Engine/CamParams.h), so their ranges and
    mappings are the engine's own.

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
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    /** A host parameter made from one engine spec. */
    std::unique_ptr<juce::RangedAudioParameter> makeParameter (const juce::String& id, const juce::String& name,
                                                               const cam::ParamSpec& spec);

    /** Text for a value of a spec'd parameter ("1.2 kHz", "350 ms"). */
    juce::String formatValue (const cam::ParamSpec& spec, float value);
}
