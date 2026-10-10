/*
  ------------------------------------------------------------------------------
    GlobalPanel.cpp

    See GlobalPanel.h.

    Author: Olivier Doaré, github.com/odoare
    Dual-licensed, mirroring the JUCE framework it depends on: under the GNU
    AGPL Version 3.0, or under commercial terms available from the author.
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
  ------------------------------------------------------------------------------
*/

#include "GlobalPanel.h"
#include "PluginProcessor.h"
#include "AppSettings.h"
#include "ParamIDs.h"
#include "Theme.h"
#include "Tooltips.h"

namespace camshaft
{

namespace
{
    const char* const helpBody =
        "Voices: how many notes can sound at once (released notes are stolen "
        "first), or one note at a time (mono restarts the envelopes, legato only "
        "moves the pitch). Glide slides from one note to the next, in "
        "milliseconds or in a tempo division, always or only between notes "
        "played legato.\n\n"
        "Interpolation: how the audio is read between its samples. Cubic is "
        "clean everywhere; sinc is the flattest for bright sounds, linear the "
        "cheapest.\n\n"
        "CC sources: the controller numbers of the four CC modulation sources.\n\n"
        "MIDI learn: right-click any knob, Learn MIDI, move a controller. The "
        "map is saved with the session and presets; saved as default, new "
        "instances start with it.\n\n"
        "Curves: when a curve modulator's duration is in beats, its editor "
        "draws the beat grid at the division chosen here; with the snap on, "
        "the points you drag or add land on it (hold alt to move one freely). "
        "For every instance.";
}

GlobalPanel::GlobalPanel (CamshaftAudioProcessor& p)
    : processor (p),
      voices (p, ids::voices, "Voices", theme::accent),
      glide (p, ids::glideTime, "Glide", theme::accent),
      pitchBend (p, ids::pbRange, "Bend range", theme::accent),
      glideSync (p.getAPVTS(), ids::glideSync, "Sync", theme::accent)
{
    laf.setAccentColour (theme::accent);
    setLookAndFeel (&laf);

    voices.setTooltip (tips::global::voices);
    glide.setTooltip (tips::global::glide);
    pitchBend.setTooltip (tips::global::pitchBend);
    glideSync.button.setTooltip (tips::global::glideSync);
    for (auto* c : std::initializer_list<juce::Component*> { &voices, &glide, &pitchBend, &glideSync })
        addAndMakeVisible (c);

    polyMode = &combo (ids::polyMode, tips::global::voiceMode);
    glideDiv = &combo (ids::glideDiv, tips::global::glideDivision);
    glideMode = &combo (ids::glideMode, tips::global::glideMode);
    interp = &combo (ids::interp, tips::global::interpolation);

    for (int i = 0; i < 4; ++i)
    {
        auto& box = ccBoxes[(size_t) i];
        for (int cc = 0; cc < 128; ++cc)
            box.addItem ("CC " + juce::String::charToString ((juce::juce_wchar) ('A' + i)) + ": " + juce::String (cc), cc + 1);
        box.setSelectedId (processor.getStructure().getCcNumber (i) + 1, juce::dontSendNotification);
        box.setTooltip (tips::global::ccNumber);
        box.onChange = [this, i] { processor.getStructure().setCcNumber (i, ccBoxes[(size_t) i].getSelectedId() - 1); };
        theme::accentCombo (box, theme::steel);
        addAndMakeVisible (box);
    }

    saveLearn.setTooltip (tips::global::saveLearn);
    clearLearn.setTooltip (tips::global::clearLearn);
    saveLearn.onClick = [this] { processor.getMidiLearn().saveAsDefault(); };
    clearLearn.onClick = [this] { processor.getMidiLearn().clearDefault(); };
    addAndMakeVisible (saveLearn);
    addAndMakeVisible (clearLearn);

    curveSnap.setToggleState (getCurveGridSnap(), juce::dontSendNotification);
    curveSnap.setTooltip (tips::global::curveSnap);
    curveSnap.onClick = [this] { setCurveGridSnap (curveSnap.getToggleState()); };
    theme::accentToggle (curveSnap, theme::accent);
    addAndMakeVisible (curveSnap);

    const double gridBeats = getCurveGridBeats();
    int id = 1;
    for (const auto& d : curveGridDivisions)
    {
        curveGrid.addItem (d.name, id);
        if (std::abs (d.beats - gridBeats) < 1.0e-9)
            curveGrid.setSelectedId (id, juce::dontSendNotification);
        ++id;
    }
    if (curveGrid.getSelectedId() == 0)
        curveGrid.setSelectedId (5, juce::dontSendNotification);   // 1/4 beat
    curveGrid.setTooltip (tips::global::curveGrid);
    curveGrid.onChange = [this]
    {
        const int i = curveGrid.getSelectedId() - 1;
        if (i >= 0 && i < (int) std::size (curveGridDivisions))
            setCurveGridBeats (curveGridDivisions[(size_t) i].beats);
    };
    theme::accentCombo (curveGrid, theme::accent);
    addAndMakeVisible (curveGrid);

    keepAudio.setToggleState (processor.getKeepAudioOnPresetLoad(), juce::dontSendNotification);
    keepAudio.setTooltip (tips::global::keepAudio);
    keepAudio.onClick = [this] { processor.setKeepAudioOnPresetLoad (keepAudio.getToggleState()); };
    theme::accentToggle (keepAudio, theme::accent);
    addAndMakeVisible (keepAudio);

    tooltipsButton.setButtonText ("?");
    tooltipsButton.setAccent (theme::accent, theme::dimText);
    tooltipsButton.setTooltip (tips::global::tooltips);
    tooltipsButton.setToggleState (getUiTooltips(), juce::dontSendNotification);
    tooltipsButton.onClick = [this] { setUiTooltips (tooltipsButton.getToggleState()); };
    addAndMakeVisible (tooltipsButton);

    infoButton.setColours ({ theme::accent, theme::text, theme::panel, theme::panelLine });
    infoButton.setInfo ("Settings", helpBody);
    addAndMakeVisible (infoButton);

    setSize (440, 380);
}

juce::ComboBox& GlobalPanel::combo (const juce::String& id, const char* tooltip)
{
    auto box = std::make_unique<juce::ComboBox>();
    if (auto* pc = dynamic_cast<juce::AudioParameterChoice*> (processor.getAPVTS().getParameter (id)))
        box->addItemList (pc->choices, 1);
    box->setTooltip (tooltip);
    theme::accentCombo (*box, theme::accent);
    attachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (processor.getAPVTS(), id, *box));
    addAndMakeVisible (*box);
    combos.push_back (std::move (box));
    return *combos.back();
}

void GlobalPanel::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel);
    auto r = getLocalBounds().reduced (10);
    theme::paintEngraved (g, "Voices", r.removeFromTop (16));
    r.removeFromTop (24 + 74 + 6);
    theme::paintEngraved (g, "Engine", r.removeFromTop (16));
    r.removeFromTop (24 + 6);
    theme::paintEngraved (g, "MIDI", r.removeFromTop (16));
    r.removeFromTop (24 + 4 + 24 + 8);
    theme::paintEngraved (g, "Curves", r.removeFromTop (16));
}

void GlobalPanel::resized()
{
    auto r = getLocalBounds().reduced (10);

    auto title = r.removeFromTop (16);
    infoButton.setBounds (title.removeFromRight (18));
    title.removeFromRight (4);
    tooltipsButton.setBounds (title.removeFromRight (18));

    auto line = r.removeFromTop (24);
    const int w3 = line.getWidth() / 3;
    polyMode->setBounds (line.removeFromLeft (w3).reduced (2, 1));
    glideMode->setBounds (line.removeFromLeft (w3).reduced (2, 1));
    glideSync.setBounds (line.removeFromLeft (w3 / 2).reduced (2, 1));
    glideDiv->setBounds (line.reduced (2, 1));

    auto knobs = r.removeFromTop (74);
    voices.setBounds (knobs.removeFromLeft (w3).reduced (4));
    glide.setBounds (knobs.removeFromLeft (w3).reduced (4));
    pitchBend.setBounds (knobs.reduced (4));
    r.removeFromTop (6);

    r.removeFromTop (16);
    line = r.removeFromTop (24);
    interp->setBounds (line.removeFromLeft (line.getWidth() / 2).reduced (2, 1));
    r.removeFromTop (6);

    r.removeFromTop (16);
    line = r.removeFromTop (24);
    const int w4 = line.getWidth() / 4;
    for (auto& box : ccBoxes)
        box.setBounds (line.removeFromLeft (w4).reduced (2, 1));
    r.removeFromTop (4);
    line = r.removeFromTop (24);
    saveLearn.setBounds (line.removeFromLeft (line.getWidth() * 2 / 3).reduced (2, 1));
    clearLearn.setBounds (line.reduced (2, 1));
    r.removeFromTop (8);

    r.removeFromTop (16);
    line = r.removeFromTop (24);
    curveSnap.setBounds (line.removeFromLeft (line.getWidth() * 2 / 3).reduced (2, 1));
    curveGrid.setBounds (line.reduced (2, 1));
    r.removeFromTop (8);
    keepAudio.setBounds (r.removeFromTop (24));
}

} // namespace camshaft
