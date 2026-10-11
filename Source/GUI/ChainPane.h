/*
  ------------------------------------------------------------------------------
    ChainPane.h

    One effect chain as a pane of the bottom band: Turbo 1, Turbo 2 or the
    Exhaust.

      header   the chain's name, its return level and pan and the dry mute
               (a Turbo), or the output level and the rev limiter switch
               (the Exhaust), and the effects' Kick-start dice
      slots    the chain's two slots side by side, each with its effect
               selector and the effect's preset bar (its FxmeFX module
               presets, shared with its own plugin) over the effect's
               controls in their compact layout

    A slot's controls are made when its effect is chosen (and dropped when
    it changes); "Off" shows nothing but the selector.

    Author: Olivier Doaré, github.com/odoare
    Dual-licensed, mirroring the JUCE framework it depends on: under the GNU
    AGPL Version 3.0, or under commercial terms available from the author.
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>
#include "CamKnob.h"
#include "Effects/EffectChain.h"

class CamshaftAudioProcessor;

namespace camshaft
{

/** One slot: selector and preset bar over the effect's compact controls. */
class EffectSlotView : public juce::Component
{
public:
    EffectSlotView (CamshaftAudioProcessor& processor, int chain, int slot, juce::Colour colour);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void refresh();
    juce::Rectangle<int> bodyArea() const;

    CamshaftAudioProcessor& processor;
    const int chain, slot;
    const juce::Colour colour;
    juce::String slotPrefix;

    juce::ComboBox typeBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
    std::unique_ptr<fxme::PresetBarComponent> presetBar;
    fxme::PresetBank* presetBank = nullptr;
    std::unique_ptr<juce::Component> controls;
    int shownType = -1;
};

class ChainPane : public juce::Component
{
public:
    /** chain: 0 and 1 the Turbos, 2 the Exhaust. */
    ChainPane (CamshaftAudioProcessor& processor, int chain);

    void paint (juce::Graphics&) override;
    void resized() override;

    static juce::Colour chainColour (int chain);

private:
    CamshaftAudioProcessor& processor;
    const int chain;

    std::unique_ptr<CamKnob> level, pan;
    std::unique_ptr<fxme::FxmeButton> dryMute, limiter;
    juce::TextButton dice { "Kick-start" };
    std::array<std::unique_ptr<EffectSlotView>, EffectChain::numSlots> slots;
    juce::Rectangle<int> titleArea;
};

} // namespace camshaft
