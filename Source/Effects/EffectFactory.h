/*
  ------------------------------------------------------------------------------
    EffectFactory.h

    Registry of the FxmeFX effects a slot can hold. Slots, parameters, module
    presets and the effects tab all derive from this list. Its order is part
    of the saved state (a slot's "_type" parameter is an index into it):
    only ever append.

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

struct EffectTypeInfo
{
    using ParameterList = std::vector<std::unique_ptr<juce::RangedAudioParameter>>;

    juce::String name;          // in the slot's type box, and in the parameter IDs

    // The FxmeFX effect's module presets (FxmeFX Common/EffectPresets.h):
    // the module is the effect's class name, the tag what the effect puts
    // after its prefix in its parameter IDs ("Comp").
    juce::String moduleName;
    juce::String paramTag;

    std::function<std::unique_ptr<Effect>()> create;
    std::function<void (ParameterList&, const juce::String&)> addParameters;
    // The effect's controls, in their compact layout.
    std::function<std::unique_ptr<juce::Component> (Effect&, juce::AudioProcessorValueTreeState&, const juce::String&)> createComponent;
};

namespace EffectFactory
{
    const std::vector<EffectTypeInfo>& types();

    /** "Off" followed by every registered effect name. */
    juce::StringArray typeChoices();

    /** The ID prefix of one effect's own parameters in a slot: the scope of
        its module presets, "<perTypePrefix>_<Tag>_". */
    inline juce::String presetPrefix (const juce::String& perTypePrefix, const EffectTypeInfo& info)
    {
        return perTypePrefix + "_" + info.paramTag + "_";
    }
}

} // namespace camshaft
