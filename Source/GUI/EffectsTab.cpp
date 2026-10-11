/*
  ------------------------------------------------------------------------------
    EffectsTab.cpp

    See EffectsTab.h.

    Author: Olivier Doaré, github.com/odoare
    Dual-licensed, mirroring the JUCE framework it depends on: under the GNU
    AGPL Version 3.0, or under commercial terms available from the author.
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
  ------------------------------------------------------------------------------
*/

#include "EffectsTab.h"
#include "PluginProcessor.h"
#include "ParamIDs.h"
#include "Theme.h"
#include "Tooltips.h"
#include "Effects/EffectChain.h"

namespace camshaft
{

using namespace cam;

namespace
{
    constexpr int leftWidth = 270;
    constexpr int pad = 8;
    constexpr int rowH = 24;
    constexpr int labelH = 18;
    constexpr int presetBarW = 300;
    constexpr int presetBarH = 26;
    constexpr int panelPad = 12;
    const char* const chainNames[] = { "Turbo 1", "Turbo 2", "Exhaust" };
}

EffectsTab::EffectsTab (CamshaftAudioProcessor& p)
    : processor (p),
      outputKnob (p, ids::masterOut, "Output", theme::accent),
      limiterButton (p.getAPVTS(), ids::limiter, "Rev limiter", theme::redline),
      master (p)
{
    auto& apvts = processor.getAPVTS();

    for (int e = 0; e < numEntries; ++e)
    {
        auto entry = std::make_unique<SlotEntry>();
        entry->chain = e / EffectChain::numSlots;
        entry->slot = e % EffectChain::numSlots;
        const juce::String chainPrefix = entry->chain < numAux ? ids::auxPrefix (entry->chain) : juce::String (ids::masterPrefix);
        entry->slotPrefix = EffectChain::slotPrefix (chainPrefix, entry->slot);

        entry->typeBox.addItemList (EffectFactory::typeChoices(), 1);
        theme::accentCombo (entry->typeBox, chainColour (entry->chain));
        entry->typeBox.setTooltip (entry->chain < numAux ? tips::effects::turboSlot : tips::effects::exhaustSlot);
        addAndMakeVisible (entry->typeBox);
        entry->attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
            apvts, EffectSlot::typeParamId (entry->slotPrefix), entry->typeBox);
        entry->typeBox.onChange = [this, e] { if (active == e) refreshPanel(); };

        entry->showButton.setButtonText (juce::CharPointer_UTF8 ("\xe2\x96\xb6"));   // play triangle
        entry->showButton.setClickingTogglesState (false);
        entry->showButton.setTooltip (tips::effects::show);
        entry->showButton.setColour (juce::TextButton::buttonOnColourId, chainColour (entry->chain));
        entry->showButton.onClick = [this, e] { activate (e); };
        addAndMakeVisible (entry->showButton);

        entries[(size_t) e] = std::move (entry);
    }

    for (int a = 0; a < numAux; ++a)
    {
        returnKnobs[(size_t) a] = std::make_unique<CamKnob> (processor, ids::auxReturn (a), "Return", theme::auxColour (a));
        returnKnobs[(size_t) a]->setBarStyle();
        returnKnobs[(size_t) a]->setTooltip (tips::effects::returnLevel);
        panKnobs[(size_t) a] = std::make_unique<CamKnob> (processor, ids::auxPan (a), "Pan", theme::auxColour (a), true);
        panKnobs[(size_t) a]->setBarStyle();
        panKnobs[(size_t) a]->setTooltip (tips::effects::returnPan);
        dryMutes[(size_t) a] = std::make_unique<fxme::FxmeButton> (apvts, ids::auxDryMute (a), "Dry mute", theme::auxColour (a));
        dryMutes[(size_t) a]->button.setTooltip (tips::effects::dryMute);
        addAndMakeVisible (*returnKnobs[(size_t) a]);
        addAndMakeVisible (*panKnobs[(size_t) a]);
        addAndMakeVisible (*dryMutes[(size_t) a]);
    }

    outputKnob.setBarStyle();
    outputKnob.setTooltip (tips::effects::output);
    limiterButton.button.setTooltip (tips::effects::limiter);
    addAndMakeVisible (outputKnob);
    addAndMakeVisible (limiterButton);
    addAndMakeVisible (master);

    diceButton.setButtonText ("Kick-start effects");
    diceButton.setTooltip (tips::effects::dice);
    diceButton.onClick = [this] { if (onRandomize) onRandomize(); };
    addAndMakeVisible (diceButton);

    placeholder.setText ("Pick an effect in a slot, then its arrow to show it", juce::dontSendNotification);
    placeholder.setJustificationType (juce::Justification::centred);
    placeholder.setColour (juce::Label::textColourId, theme::dimText);
    addAndMakeVisible (placeholder);
}

EffectsTab::~EffectsTab() = default;

juce::Colour EffectsTab::chainColour (int chain) const
{
    return chain < numAux ? theme::auxColour (chain) : theme::exhaustColour;
}

void EffectsTab::activate (int e)
{
    active = e;
    for (int i = 0; i < numEntries; ++i)
        entries[(size_t) i]->showButton.setToggleState (i == e, juce::dontSendNotification);
    refreshPanel();
}

void EffectsTab::refreshPanel()
{
    const int type = active >= 0 ? entries[(size_t) active]->typeBox.getSelectedItemIndex() - 1 : -1;
    const auto& types = EffectFactory::types();

    if (type < 0 || type >= (int) types.size())
    {
        shownPanel.reset();
        shownEntry = shownType = -1;
        if (presetBar != nullptr)
            presetBar->setVisible (false);
        placeholder.setVisible (true);
        repaint();
        return;
    }

    placeholder.setVisible (false);
    const auto& info = types[(size_t) type];
    const auto& entry = *entries[(size_t) active];
    const auto perTypePrefix = EffectSlot::perTypePrefix (entry.slotPrefix, info.name);

    if (active != shownEntry || type != shownType)
    {
        shownPanel.reset();
        shownEntry = active;
        shownType = type;

        auto& chain = entry.chain < numAux ? processor.getAuxChain (entry.chain) : processor.getMasterChain();
        auto& effect = chain.getSlot (entry.slot).getEffect (type);
        effect.ensureLoaded();   // the panel reads the IR list as it is made
        shownPanel = info.createComponent (effect, processor.getAPVTS(), perTypePrefix);
        addAndMakeVisible (*shownPanel);
    }

    auto* bank = processor.getEffectPresets (perTypePrefix);
    if (bank != presetBank)
    {
        presetBar.reset();
        presetBank = bank;
        if (bank != nullptr)
        {
            presetBar = std::make_unique<fxme::PresetBarComponent> (*bank);
            presetBar->setAccentColour (chainColour (entry.chain));
            presetBar->setBrowserButtonVisible (true);
            presetBar->setBrowserSize (320, 380);
            addAndMakeVisible (*presetBar);
        }
    }
    if (presetBar != nullptr)
        presetBar->setVisible (true);

    resized();
    repaint();
}

juce::Rectangle<int> EffectsTab::rightArea() const
{
    return getLocalBounds().reduced (pad).withTrimmedLeft (leftWidth).withTrimmedTop (presetBarH + pad + panelPad);
}

juce::Rectangle<int> EffectsTab::panelBounds (const EffectTypeInfo& info) const
{
    const auto r = rightArea().reduced (panelPad);
    const int w = juce::jmin (info.preferredWidth, r.getWidth());
    const int h = juce::jmin (info.preferredHeight, r.getHeight());
    return { r.getX() + (r.getWidth() - w) / 2, r.getY() + (r.getHeight() - h) / 2, w, h };
}

void EffectsTab::paint (juce::Graphics& g)
{
    // Left column and its section labels.
    const auto left = getLocalBounds().reduced (pad).withWidth (leftWidth - 2 * pad);
    theme::paintPanel (g, left.toFloat().expanded (4.0f));

    int y = left.getY();
    auto label = [&] (const juce::String& text, juce::Colour c)
    {
        g.setColour (c);
        g.fillRect (left.getX(), y + 4, 3, labelH - 8);
        theme::paintEngraved (g, text, { left.getX() + 8, y, left.getWidth() - 8, labelH });
    };
    for (int chain = 0; chain < 3; ++chain)
    {
        label (chainNames[chain], chainColour (chain));
        y += labelH + EffectChain::numSlots * (rowH + 3) + 6;
        if (chain < numAux)
            y += 3 * (rowH + 3) + 8;
    }
    label ("Output", theme::accent);

    // The panel container around the shown effect.
    if (shownPanel != nullptr && shownType >= 0)
    {
        const auto outer = panelBounds (EffectFactory::types()[(size_t) shownType]).toFloat().expanded ((float) panelPad);
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillRoundedRectangle (outer.translated (0.0f, 4.0f).reduced (2.0f), 10.0f);
        g.setColour (theme::panel);
        g.fillRoundedRectangle (outer, 8.0f);
        g.setColour (chainColour (entries[(size_t) shownEntry]->chain).withAlpha (0.6f));
        g.drawRoundedRectangle (outer.reduced (0.5f), 8.0f, 1.0f);
    }
}

void EffectsTab::resized()
{
    auto left = getLocalBounds().reduced (pad).withWidth (leftWidth - 2 * pad);

    for (int chain = 0; chain < 3; ++chain)
    {
        left.removeFromTop (labelH);
        for (int s = 0; s < EffectChain::numSlots; ++s)
        {
            auto& e = *entries[(size_t) (chain * EffectChain::numSlots + s)];
            auto row = left.removeFromTop (rowH);
            e.showButton.setBounds (row.removeFromRight (28));
            row.removeFromRight (4);
            e.typeBox.setBounds (row.withTrimmedLeft (8));
            left.removeFromTop (3);
        }
        if (chain < numAux)
        {
            returnKnobs[(size_t) chain]->setBounds (left.removeFromTop (rowH).withTrimmedLeft (8));
            left.removeFromTop (3);
            panKnobs[(size_t) chain]->setBounds (left.removeFromTop (rowH).withTrimmedLeft (8));
            left.removeFromTop (3);
            dryMutes[(size_t) chain]->setBounds (left.removeFromTop (rowH).withTrimmedLeft (8));
            left.removeFromTop (3 + 8);
        }
        left.removeFromTop (6);
    }

    left.removeFromTop (labelH);
    outputKnob.setBounds (left.removeFromTop (rowH).withTrimmedLeft (8));
    left.removeFromTop (3);
    auto line = left.removeFromTop (rowH).withTrimmedLeft (8);
    limiterButton.setBounds (line.removeFromLeft (line.getWidth() / 2));
    diceButton.setBounds (line.reduced (2, 0));
    left.removeFromTop (8);
    master.setBounds (left.withHeight (juce::jmin (left.getHeight(), 130)));

    placeholder.setBounds (rightArea());
    if (shownPanel != nullptr && shownType >= 0)
    {
        const auto& info = EffectFactory::types()[(size_t) shownType];
        const auto pb = panelBounds (info);
        shownPanel->setBounds (pb);
        if (presetBar != nullptr)
        {
            const auto outer = pb.expanded (panelPad);
            const int w = juce::jmin (presetBarW, outer.getWidth());
            presetBar->setBounds (outer.getCentreX() - w / 2, outer.getY() - pad - presetBarH, w, presetBarH);
        }
    }
}

} // namespace camshaft
