/*
  ------------------------------------------------------------------------------
    MatrixView.h

    The modulation matrix as a list: one row per route (64), each with its
    source, destination, optional via source (scales the depth: a mod wheel
    opening an LFO, say) and bipolar depth. Routes made from a knob's menu or
    by dropping a chip on a knob appear here too.

    Destinations are offered by section (each cam, each modulator, voices
    and output, the effect slots); an effect slot only lists the parameters
    of the effect loaded in it.

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

class MatrixRow : public juce::Component
{
public:
    MatrixRow (CamshaftAudioProcessor& processor, int index);

    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void chooseSource (bool via);
    void chooseDestination();

    CamshaftAudioProcessor& processor;
    int index;
    juce::TextButton sourceButton, destButton, viaButton, clearButton { "x" };
    CamKnob depth;
    bool used = false;
    int pendingSource = 0;     // chosen before any destination
};

class MatrixView : public juce::Component,
                   private juce::ChangeListener,
                   private juce::Timer
{
public:
    explicit MatrixView (CamshaftAudioProcessor& processor);
    ~MatrixView() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    CamshaftAudioProcessor& processor;
    juce::Viewport viewport;
    juce::Component list;
    std::vector<std::unique_ptr<MatrixRow>> rows;
};

/** The destination menu: item ID = 1 + index in Destinations::all(). */
juce::PopupMenu destinationsMenu (CamshaftAudioProcessor& processor, const juce::String& ticked);

} // namespace camshaft
