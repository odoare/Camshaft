/*
  ------------------------------------------------------------------------------
    Randomizer.h

    "Kick-start": new random settings for one section (the cams' profiles,
    tuning, mix and envelopes; their filters; the modulators and the depths
    of existing routes; the active effects) or for all of them.

    Each parameter is drawn inside a range that keeps the patch playable
    (a cutoff never under 200 Hz, a volume never above -3 dB, envelopes that
    finish), not over its whole range. Locked parameters (the padlock in a
    control's menu) are never touched, nor are the output level and the
    rev limiter, nor anything structural (which cams sound, the routes
    themselves, the timing belt, the effect types).

    Message thread; every change goes through the host.

    Author: Olivier Doaré, github.com/odoare
    Dual-licensed, mirroring the JUCE framework it depends on: under the GNU
    AGPL Version 3.0, or under commercial terms available from the author.
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>
#include "StructureState.h"

namespace camshaft
{

enum class RandomSection { cams, filters, modulators, effects, all };

void randomize (RandomSection section, juce::AudioProcessorValueTreeState& apvts, const StructureState& structure);

} // namespace camshaft
