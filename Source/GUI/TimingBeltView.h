/*
  ------------------------------------------------------------------------------
    TimingBeltView.h

    The timing belt: cross-modulation between the cams, drawn as pulleys
    linked by belts.

      - Drag from one pulley to another: an FM connection from the first to
        the second. A connection that would close a loop (A -> B -> A) is
        refused, and the belt flashes red.
      - Double-click a pulley: feedback FM on itself (on / off).
      - Click a belt's badge: FM -> Ring -> AM -> removed.
      - The small knob on each belt is its lift (amount), the cfLift
        parameter of the cam it drives, so it is automatable and can be
        modulated.

    Pulleys turn while their cam sounds. A cam that is not audible but
    drives another is still computed (it shows as a pulley with a dashed rim).

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

class TimingBeltView : public juce::Component,
                       public juce::SettableTooltipClient,
                       private juce::Timer,
                       private juce::ChangeListener
{
public:
    explicit TimingBeltView (CamshaftAudioProcessor& processor);
    ~TimingBeltView() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void updateKnobs();

    juce::Point<float> pulleyCentre (int cam) const;
    float pulleyRadius() const;
    int pulleyAt (juce::Point<float> p) const;
    juce::Point<float> badgeCentre (int src, int dst) const;
    std::pair<int, int> badgeAt (juce::Point<float> p) const;

    CamshaftAudioProcessor& processor;
    std::array<std::unique_ptr<CamKnob>, 16> liftKnobs;   // [src * 4 + dst]

    int dragFrom = -1;
    juce::Point<float> dragPoint;
    std::pair<int, int> refused { -1, -1 };
    int refusedFrames = 0;
    std::array<float, 4> angle {};
    double lastTime = 0.0;
};

} // namespace camshaft
