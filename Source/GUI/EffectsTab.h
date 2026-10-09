/*
  ------------------------------------------------------------------------------
    EffectsTab.h

    The Effects tab, after MechanOdd's: on the left the slots and levels,
    on the right the selected slot's effect at full size under its preset
    bar (the effect's module presets, shared with its own FxmeFX plugin).

      Turbo 1, Turbo 2   two slots each, return level and pan, "dry mute"
                         (cams sending to this Turbo lose their dry signal,
                         for a 100 % wet use)
      Exhaust            two slots on the whole mix
      Output             the output level and the rev limiter switch, and
                         a large master view (tachometer and vectorscope)

    Author: Olivier Doaré, github.com/odoare
    Dual-licensed, mirroring the JUCE framework it depends on: under the GNU
    AGPL Version 3.0, or under commercial terms available from the author.
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>
#include "CamKnob.h"
#include "MasterView.h"
#include "Effects/EffectFactory.h"

class CamshaftAudioProcessor;

namespace camshaft
{

class EffectsTab : public juce::Component
{
public:
    explicit EffectsTab (CamshaftAudioProcessor& processor);
    ~EffectsTab() override;

    std::function<void()> onRandomize;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct SlotEntry
    {
        juce::String slotPrefix;     // "aux1_fx0"
        int chain = 0;               // 0, 1: Turbo; 2: Exhaust
        int slot = 0;
        juce::ComboBox typeBox;
        juce::TextButton showButton;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
    };

    static constexpr int numEntries = 6;

    void activate (int entry);
    void refreshPanel();
    juce::Colour chainColour (int chain) const;
    juce::Rectangle<int> rightArea() const;
    juce::Rectangle<int> panelBounds (const EffectTypeInfo& info) const;

    CamshaftAudioProcessor& processor;
    std::array<std::unique_ptr<SlotEntry>, numEntries> entries;

    std::array<std::unique_ptr<CamKnob>, 2> returnKnobs, panKnobs;
    std::array<std::unique_ptr<fxme::FxmeButton>, 2> dryMutes;
    CamKnob outputKnob;
    fxme::FxmeButton limiterButton;
    juce::TextButton diceButton;
    MasterView master;
    juce::Label placeholder;

    std::unique_ptr<juce::Component> shownPanel;
    std::unique_ptr<fxme::PresetBarComponent> presetBar;
    fxme::PresetBank* presetBank = nullptr;
    int active = -1, shownEntry = -1, shownType = -1;
};

} // namespace camshaft
