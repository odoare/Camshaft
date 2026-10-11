/*
  ------------------------------------------------------------------------------
    ChainPane.cpp

    See ChainPane.h.

    Author: Olivier Doaré, github.com/odoare
    Dual-licensed, mirroring the JUCE framework it depends on: under the GNU
    AGPL Version 3.0, or under commercial terms available from the author.
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
  ------------------------------------------------------------------------------
*/

#include "ChainPane.h"
#include "PluginProcessor.h"
#include "ParamIDs.h"
#include "Theme.h"
#include "Tooltips.h"
#include "Effects/EffectFactory.h"

namespace camshaft
{

using namespace cam;

namespace
{
    constexpr int headerH = 26;
    const char* const chainNames[] = { "Turbo 1", "Turbo 2", "Exhaust" };
}

//==============================================================================
EffectSlotView::EffectSlotView (CamshaftAudioProcessor& p, int c, int s, juce::Colour col)
    : processor (p), chain (c), slot (s), colour (col)
{
    const juce::String chainPrefix = chain < numAux ? ids::auxPrefix (chain) : juce::String (ids::masterPrefix);
    slotPrefix = EffectChain::slotPrefix (chainPrefix, slot);

    typeBox.addItemList (EffectFactory::typeChoices(), 1);
    theme::accentCombo (typeBox, colour);
    typeBox.setTooltip (chain < numAux ? tips::effects::turboSlot : tips::effects::exhaustSlot);
    addAndMakeVisible (typeBox);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.getAPVTS(), EffectSlot::typeParamId (slotPrefix), typeBox);
    typeBox.onChange = [this] { refresh(); };
    refresh();
}

void EffectSlotView::refresh()
{
    const auto& types = EffectFactory::types();
    const int type = typeBox.getSelectedItemIndex() - 1;
    if (type == shownType)
        return;
    shownType = type;

    controls.reset();
    presetBar.reset();
    presetBank = nullptr;

    if (type >= 0 && type < (int) types.size())
    {
        const auto& info = types[(size_t) type];
        const auto perTypePrefix = EffectSlot::perTypePrefix (slotPrefix, info.name);
        auto& effectChain = chain < numAux ? processor.getAuxChain (chain) : processor.getMasterChain();
        auto& effect = effectChain.getSlot (slot).getEffect (type);
        effect.ensureLoaded();   // the controls read the IR list as they are made
        controls = info.createComponent (effect, processor.getAPVTS(), perTypePrefix);
        addAndMakeVisible (*controls);

        if (auto* bank = processor.getEffectPresets (perTypePrefix))
        {
            presetBank = bank;
            presetBar = std::make_unique<fxme::PresetBarComponent> (*bank);
            presetBar->setAccentColour (colour);
            presetBar->setBrowserButtonVisible (true);
            presetBar->setBrowserSize (320, 380);
            addAndMakeVisible (*presetBar);
        }
    }
    resized();
    repaint();
}

juce::Rectangle<int> EffectSlotView::bodyArea() const
{
    return getLocalBounds().withTrimmedTop (headerH + 4);
}

void EffectSlotView::paint (juce::Graphics& g)
{
    // The effect sits in a recessed frame in the chain's colour.
    const auto body = bodyArea().toFloat();
    g.setColour (theme::plotBack);
    g.fillRoundedRectangle (body, 6.0f);
    g.setColour (colour.withAlpha (0.45f));
    g.drawRoundedRectangle (body.reduced (0.5f), 6.0f, 1.0f);
    if (controls == nullptr)
        theme::paintEngraved (g, "Off", body.toNearestInt(), juce::Justification::centred, 12.0f);
}

void EffectSlotView::resized()
{
    auto header = getLocalBounds().removeFromTop (headerH);
    typeBox.setBounds (header.removeFromLeft (juce::jmin (130, header.getWidth() / 3)).reduced (0, 1));
    header.removeFromLeft (8);
    if (presetBar != nullptr)
        presetBar->setBounds (header.removeFromLeft (juce::jmin (300, header.getWidth())));
    if (controls != nullptr)
        controls->setBounds (bodyArea().reduced (4));
}

//==============================================================================
juce::Colour ChainPane::chainColour (int c)
{
    return c < numAux ? theme::auxColour (c) : theme::exhaustColour;
}

ChainPane::ChainPane (CamshaftAudioProcessor& p, int c) : processor (p), chain (c)
{
    auto& apvts = processor.getAPVTS();
    const auto colour = chainColour (chain);

    if (chain < numAux)
    {
        level = std::make_unique<CamKnob> (processor, ids::auxReturn (chain), "Return", colour);
        level->setTooltip (tips::effects::returnLevel);
        pan = std::make_unique<CamKnob> (processor, ids::auxPan (chain), "Pan", colour, true);
        pan->setTooltip (tips::effects::returnPan);
        pan->setBarStyle();
        addAndMakeVisible (*pan);
        dryMute = std::make_unique<fxme::FxmeButton> (apvts, ids::auxDryMute (chain), "Dry mute", colour);
        dryMute->button.setTooltip (tips::effects::dryMute);
        addAndMakeVisible (*dryMute);
    }
    else
    {
        level = std::make_unique<CamKnob> (processor, ids::masterOut, "Output", theme::accent);
        level->setTooltip (tips::effects::output);
        limiter = std::make_unique<fxme::FxmeButton> (apvts, ids::limiter, "Rev limiter", theme::redline);
        limiter->button.setTooltip (tips::effects::limiter);
        addAndMakeVisible (*limiter);
    }
    level->setBarStyle();
    addAndMakeVisible (*level);

    dice.setTooltip (tips::effects::dice);
    dice.setMouseClickGrabsKeyboardFocus (false);
    dice.onClick = [this] { processor.randomize (RandomSection::effects); };
    addAndMakeVisible (dice);

    for (int s = 0; s < EffectChain::numSlots; ++s)
    {
        slots[(size_t) s] = std::make_unique<EffectSlotView> (processor, chain, s, colour);
        addAndMakeVisible (*slots[(size_t) s]);
    }
}

void ChainPane::paint (juce::Graphics& g)
{
    theme::paintPanel (g, getLocalBounds().toFloat(), chainColour (chain));
    auto title = titleArea;
    g.setColour (chainColour (chain));
    g.fillRoundedRectangle (title.removeFromLeft (4).toFloat(), 2.0f);
    g.setColour (theme::text);
    g.setFont (theme::labelFont (13.0f));
    g.drawText (chainNames[chain], title.withTrimmedLeft (8), juce::Justification::centredLeft);
}

void ChainPane::resized()
{
    auto area = getLocalBounds().reduced (8, 6);

    auto header = area.removeFromTop (24);
    titleArea = header.removeFromLeft (90);
    dice.setBounds (header.removeFromRight (90).reduced (0, 1));
    header.removeFromRight (10);
    level->setBounds (header.removeFromLeft (juce::jmin (220, header.getWidth() / 3)).reduced (0, 1));
    header.removeFromLeft (10);
    if (pan != nullptr)
    {
        pan->setBounds (header.removeFromLeft (juce::jmin (160, header.getWidth() / 3)).reduced (0, 1));
        header.removeFromLeft (10);
    }
    if (dryMute != nullptr)
        dryMute->setBounds (header.removeFromLeft (100));
    if (limiter != nullptr)
        limiter->setBounds (header.removeFromLeft (110));
    area.removeFromTop (6);

    const int w = (area.getWidth() - 10) / EffectChain::numSlots;
    for (int s = 0; s < EffectChain::numSlots; ++s)
    {
        slots[(size_t) s]->setBounds (area.removeFromLeft (w));
        area.removeFromLeft (10);
    }
}

} // namespace camshaft
