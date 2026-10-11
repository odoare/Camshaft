/*
  ------------------------------------------------------------------------------
    CamStrip.h

    One cam's controls, under its scope row, on four pages:

      Cam      profile (centre, lobe, overlap and its shape, snap), tuning
               (coarse, fine) and cylinders (count, detune, spread, random
               phase)
      Filters  routing (series / parallel and balance) and the two filters
               (type, cutoff, resonance, gain, key tracking, envelope amount,
               vowels and their position for the formant)
      Amp      the hardwired amp envelope with its segment curves and
               velocity amount
      Mix      volume, pan, the two Turbo sends and their tap points

    The header holds the cam's name in its colour, its "audible" switch (off:
    the cam only modulates others through the timing belt) and the page tabs.

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

/** A knob page laid out as rows; controls are owned by the page. A row
    may grow (up to a height) into the page's spare room, so its knobs get
    larger without the page changing size; toggle buttons keep a button's
    height whatever their row's. */
class ControlPage : public juce::Component
{
public:
    using Row = std::vector<juce::Component*>;

    void addRow (Row row, int height, int maxHeight = 0) { rows.push_back ({ std::move (row), height, juce::jmax (height, maxHeight) }); }
    void resized() override;

    template <typename T, typename... Args>
    T& make (Args&&... args)
    {
        auto c = std::make_unique<T> (std::forward<Args> (args)...);
        auto& ref = *c;
        addAndMakeVisible (ref);
        owned.push_back (std::move (c));
        return ref;
    }

    juce::ComboBox& combo (juce::AudioProcessorValueTreeState& apvts, const juce::String& id, juce::Colour accent,
                           const char* tooltip);

private:
    struct RowInfo { Row items; int height, maxHeight; };
    std::vector<RowInfo> rows;
    std::vector<std::unique_ptr<juce::Component>> owned;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>> attachments;
};

class CamStrip : public juce::Component,
                 private juce::Timer
{
public:
    CamStrip (CamshaftAudioProcessor& processor, int cam);

    void paint (juce::Graphics&) override;
    void resized() override;

    void showPage (int page);

private:
    void timerCallback() override;
    void buildCamPage();
    void buildFilterPage();
    void buildAmpPage();
    void buildMixPage();
    CamKnob& knob (ControlPage& page, int field, const char* tooltip, bool bipolar = false);

    CamshaftAudioProcessor& processor;
    int cam;
    juce::Colour colour;

    fxme::FxmeButton audible;
    std::array<juce::TextButton, 4> tabs;
    std::array<ControlPage, 4> pages;
    int currentPage = 0;

    // Controls greyed out when they do not apply.
    std::array<CamKnob*, 2> cutoffKnobs {}, gainKnobs {}, resKnobs {}, vowelPosKnobs {}, keyKnobs {}, envKnobs {};
    std::array<juce::ComboBox*, 2> vowelA {}, vowelB {}, typeBoxes {};
    CamKnob* balanceKnob = nullptr;
    CamKnob* detuneKnob = nullptr;
    CamKnob* spreadKnob = nullptr;
};

} // namespace camshaft
