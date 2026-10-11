/*
  ------------------------------------------------------------------------------
    PluginEditor.h

    Camshaft's window: the FxmeTools top bar (name, the master view, the
    global Kick-start, the presets of the "Garage" with their browser, the
    gear for the settings, help), then two tabs, Synth and Effects. The
    header stays visible on both.

    Author: Olivier Doaré, github.com/odoare
    Dual-licensed, mirroring the JUCE framework it depends on: under the GNU
    AGPL Version 3.0, or under commercial terms available from the author.
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "AppSettings.h"
#include "Theme.h"
#include "GUI/SynthTab.h"
#include "GUI/EffectsTab.h"
#include "GUI/MasterView.h"
#include "GUI/Dashboard.h"

class CamshaftAudioProcessorEditor : public juce::AudioProcessorEditor,
                                     public juce::DragAndDropContainer
{
public:
    explicit CamshaftAudioProcessorEditor (CamshaftAudioProcessor&);
    ~CamshaftAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void showGlobalPanel();
    static juce::Image assetImage (const char* resourceName);

    /** Tabs over the shared background. */
    struct Tabs : public juce::TabbedComponent
    {
        Tabs() : juce::TabbedComponent (juce::TabbedButtonBar::TabsAtTop) {}
        void paint (juce::Graphics& g) override { camshaft::theme::paintBackground (g, getLocalBounds().toFloat()); }
    };

    CamshaftAudioProcessor& processor;
    fxme::FxmeLookAndFeel laf;

    fxme::TopBar topBar { camshaft::theme::displayName, camshaft::theme::tagline,
                          JucePlugin_VersionString, assetImage ("logo686_png") };
    camshaft::OutputMeter headerMeter { processor };
    camshaft::DiceButton kickStart;
    fxme::PresetBarComponent presetBar { processor.getPresetManager() };
    fxme::GearButton gearButton;
    fxme::InfoButton infoButton;

    Tabs tabs;
    camshaft::SynthTab synthTab { processor };
    camshaft::EffectsTab effectsTab { processor };

    /** The about screen: 3 s the first time an instance opens its editor,
        and until clicked from the logo or the icon. Last: over everything. */
    fxme::SplashOverlay splash;

    /** Hover help with an off switch (the "?" in the settings, saved
        machine-wide); asked per tip, so it covers callouts too. */
    struct ToggleableTooltipWindow : public juce::TooltipWindow
    {
        using juce::TooltipWindow::TooltipWindow;
        juce::String getTipFor (juce::Component& c) override
        {
            return camshaft::getUiTooltips() ? juce::TooltipWindow::getTipFor (c) : juce::String();
        }
    };
    ToggleableTooltipWindow tooltipWindow { this, 700 };

    // Exactly one, after the children: typing in the preset browser's name
    // field and in the knobs' value entry, in hosted windows.
    fxme::TextEntryFocusFixer textEntryFixer { *this };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CamshaftAudioProcessorEditor)
};
