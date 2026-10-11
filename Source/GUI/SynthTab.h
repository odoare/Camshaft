/*
  ------------------------------------------------------------------------------
    SynthTab.h

    The Synth tab, top to bottom (the plan's five bands, the header being the
    editor's top bar):

      file      the audio file: import / export / back to the built-in
                source, channel choice, normalisation, and the waveform view
                with the cams' ranges
      scopes    per cam: its rotating profile and its oscilloscope
      strips    per cam: its controls, in the same column as its scope
      bottom    belt & viz (the timing belt beside the dashboard), the
                modulators, or the matrix (one at a time; belt & viz first)

    Each section with a Kick-start (randomize) has its dice in its header.

    Author: Olivier Doaré, github.com/odoare
    Dual-licensed, mirroring the JUCE framework it depends on: under the GNU
    AGPL Version 3.0, or under commercial terms available from the author.
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>
#include "WaveformView.h"
#include "ScopeRow.h"
#include "CamStrip.h"
#include "ModulatorPanel.h"
#include "MatrixView.h"
#include "TimingBeltView.h"
#include "Dashboard.h"
#include "Shell/Randomizer.h"

class CamshaftAudioProcessor;

namespace camshaft
{

/** A small die face; clicking it kick-starts (randomizes) a section. */
class DiceButton : public juce::Button
{
public:
    DiceButton() : juce::Button ("dice") { setMouseClickGrabsKeyboardFocus (false); }
    void paintButton (juce::Graphics&, bool over, bool down) override;
};

class SynthTab : public juce::Component,
                 private juce::ChangeListener
{
public:
    explicit SynthTab (CamshaftAudioProcessor& processor);
    ~SynthTab() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void showBottom (int index);
    void importDialog();
    void exportDialog();
    void showMessage (const juce::String& text);

    CamshaftAudioProcessor& processor;

    // File band.
    juce::TextButton importButton { "Import..." }, exportButton { "Export..." }, builtInButton { "Built-in" };
    juce::ComboBox channelBox;
    juce::ToggleButton normaliseButton { "Normalise" };
    juce::Label fileLabel, messageLabel;
    WaveformView waveform;
    std::unique_ptr<juce::FileChooser> chooser;

    std::vector<std::unique_ptr<ScopeRow>> scopes;
    std::vector<std::unique_ptr<CamStrip>> strips;

    // Bottom band.
    std::array<juce::TextButton, 3> bottomTabs;
    ModulatorPanel modulators;
    MatrixView matrix;
    TimingBeltView belt;
    Dashboard dashboard;
    int bottom = 0;

    DiceButton camsDice, filtersDice, modsDice;

    juce::Rectangle<int> fileHeader, stripsHeader, bottomHeader;
};

} // namespace camshaft
