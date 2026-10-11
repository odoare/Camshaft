/*
  ------------------------------------------------------------------------------
    PluginEditor.cpp

    See PluginEditor.h.

    Author: Olivier Doaré, github.com/odoare
    Dual-licensed, mirroring the JUCE framework it depends on: under the GNU
    AGPL Version 3.0, or under commercial terms available from the author.
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
  ------------------------------------------------------------------------------
*/

#include "PluginEditor.h"
#include "GUI/GlobalPanel.h"
#include "Tooltips.h"
#include <BinaryData.h>

using namespace camshaft;

namespace
{
    const char* const helpBody =
        "Camshaft plays ranges of an audio file as wavetables: each of the four "
        "cams takes a slice of the file (where it opens and how long it lasts) "
        "as one cycle of a wave, played at the note's pitch.\n\n"
        "Audio: import a file (or drop it on the waveform); it is stored inside "
        "the session and the presets. Without one, a built-in morph of waveforms "
        "plays. On the waveform, drag inside a cam's range (or its numbered "
        "flag) to move it, drag an edge to move that edge; the bars along the "
        "bottom show what each playing voice reads. The wheel zooms, a drag on "
        "empty space scrolls, a double-click shows everything.\n\n"
        "Cams: a scope row (the cam's profile, turning while it plays, and its "
        "output) over its strip: profile and tuning, cylinders (unison), two "
        "filters, the amp envelope, the mix and the two Turbo sends.\n\n"
        "Modulators: sixteen slots, each an LFO, an ADSR or a drawn curve. Drag "
        "a slot (or a MIDI source) onto any knob to modulate it, or use the "
        "knob's right-click menu; the Matrix lists every route. A knob's outer "
        "ring shows its modulation, the dot its live value.\n\n"
        "Belt & Viz: the timing belt, cams modulating cams (FM, ring, AM): drag "
        "from one pulley to another, click a belt's badge to change its type. "
        "Beside it the dashboard: the output level, spectrum and stereo image, "
        "warning lights (limiter, DSP load, voice stealing, MIDI, pedal, host "
        "sync, Turbo signal), the free voices and the DSP load, the voice mode "
        "(P M L) and the hazard button (panic).\n\n"
        "Effects: two Turbo chains fed by the cams' sends, the Exhaust on the "
        "whole mix, then the output and the rev limiter.\n\n"
        "Garage (presets): the arrows step through them, \"...\" opens the "
        "browser. The dice kick-start (randomize) a section, or everything from "
        "the top bar; locked knobs (right-click) are left alone.\n\n"
        "Right-click a knob to type a value, learn MIDI, modulate it or lock it.";
}

//==============================================================================
CamshaftAudioProcessorEditor::CamshaftAudioProcessorEditor (CamshaftAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    laf.setAccentColour (theme::accent);
    laf.setKnobStyle (fxme::FxmeLookAndFeel::KnobStyle::flat);
    setLookAndFeel (&laf);

    // ---- Top bar ----
    topBar.setAccentColour (theme::accent);
    topBar.setBackgroundColour (theme::topBarBg);
    topBar.setDecoration (assetImage ("icon_png"));
    topBar.onLogoClicked = [this] { splash.show (-1); };
    addAndMakeVisible (topBar);

    presetBar.setAccentColour (theme::accent);
    presetBar.setBrowserButtonVisible (true);
    presetBar.setBrowserSize (320, 380);

    kickStart.setTooltip (tips::bar::kickStart);
    kickStart.onClick = [this] { processor.randomize (RandomSection::all); };
    gearButton.setAccent (theme::accent);
    gearButton.setTooltip (tips::bar::gear);
    gearButton.onClick = [this] { showGlobalPanel(); };
    infoButton.setColours ({ theme::accent, theme::text, theme::panel, theme::panelLine });
    infoButton.setInfo (theme::displayName, helpBody);

    topBar.setRightControls ({ { &headerMeter, 150 }, { &kickStart, 24 }, { &presetBar, 236 },
                               { &gearButton, 24 }, { &infoButton, 22 } });

    // ---- Tabs ----
    effectsTab.onRandomize = [this] { processor.randomize (RandomSection::effects); };
    tabs.setTabBarDepth (28);
    tabs.addTab ("Synth", theme::panel, &synthTab, false);
    tabs.addTab ("Effects", theme::panel, &effectsTab, false);
    addAndMakeVisible (tabs);

    // ---- Splash, last ----
    splash.setImage (assetImage ("splash_png"));
    splash.setLink ("fx-mechanics.com", juce::URL ("https://fx-mechanics.com"), assetImage ("logo686_png"));
    splash.setLinkColour (theme::accent);
    addChildComponent (splash);

    setResizable (true, true);
    setResizeLimits (1100, 780, 2400, 1600);
    setSize (1360, 900);

    splash.setBounds (getLocalBounds());
    if (processor.takeFirstSplash())
        splash.show (3000);
}

CamshaftAudioProcessorEditor::~CamshaftAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

juce::Image CamshaftAudioProcessorEditor::assetImage (const char* resourceName)
{
    int size = 0;
    if (const char* data = BinaryData::getNamedResource (resourceName, size))
        return juce::ImageCache::getFromMemory (data, size);
    return {};
}

void CamshaftAudioProcessorEditor::showGlobalPanel()
{
    juce::CallOutBox::launchAsynchronously (std::make_unique<GlobalPanel> (processor),
                                            getLocalArea (&topBar, gearButton.getBounds()), this);
}

void CamshaftAudioProcessorEditor::paint (juce::Graphics& g)
{
    theme::paintBackground (g, getLocalBounds().toFloat());
}

void CamshaftAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();
    topBar.setBounds (area.removeFromTop (theme::topBarHeight));
    tabs.setBounds (area);
    splash.setBounds (getLocalBounds());
}
