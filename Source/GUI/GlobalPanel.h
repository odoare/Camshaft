/*
  ------------------------------------------------------------------------------
    GlobalPanel.h

    The settings behind the top bar's gear, in a callout:

      Voices        polyphony, poly / mono / legato, glide (time or tempo
                    division, always or legato only), pitch bend range
      Engine        interpolation quality
      MIDI          the CC numbers of the four CC sources, the MIDI learn
                    default (save the current map, or clear it)
      Instance      keep the audio when loading presets
      Help          the "?" switch for hover tips (machine-wide)

    Author: Olivier Doaré, github.com/odoare
    Dual-licensed, mirroring the JUCE framework it depends on: under the GNU
    AGPL Version 3.0, or under commercial terms available from the author.
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>
#include "CamKnob.h"

class CamshaftAudioProcessor;

namespace camshaft
{

class GlobalPanel : public juce::Component
{
public:
    explicit GlobalPanel (CamshaftAudioProcessor& processor);
    ~GlobalPanel() override { setLookAndFeel (nullptr); }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::ComboBox& combo (const juce::String& id, const char* tooltip);

    CamshaftAudioProcessor& processor;
    fxme::FxmeLookAndFeel laf;   // a callout is its own window: no inherited look-and-feel

    CamKnob voices, glide, pitchBend;
    fxme::FxmeButton glideSync;
    std::vector<std::unique_ptr<juce::ComboBox>> combos;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>> attachments;
    juce::ComboBox *polyMode = nullptr, *glideDiv = nullptr, *glideMode = nullptr, *interp = nullptr;

    std::array<juce::ComboBox, 4> ccBoxes;
    juce::TextButton saveLearn { "Save MIDI map as default" }, clearLearn { "Clear default" };
    juce::ToggleButton keepAudio { "Keep audio when loading presets" };
    fxme::AccentToggle tooltipsButton;
    fxme::InfoButton infoButton;
};

} // namespace camshaft
