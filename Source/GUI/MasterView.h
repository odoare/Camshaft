/*
  ------------------------------------------------------------------------------
    MasterView.h

    The master output, as an engine gauge and a scope:

      Tachometer   an arc whose needle follows the output peak (dBFS, its
                   ticks labelled), a redline at the rev limiter's ceiling
                   (-0.3 dBFS) and a warning light while the limiter is
                   cutting in.
      Vectorscope  fxme::Vectorscope on the output tap (Lissajous, mid up).

    MasterView puts the two side by side; the header carries a small one,
    the Effects tab a large one. Both read atomics and a lock-free tap, at
    30 fps.

    Author: Olivier Doaré, github.com/odoare
    Dual-licensed, mirroring the JUCE framework it depends on: under the GNU
    AGPL Version 3.0, or under commercial terms available from the author.
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>

class CamshaftAudioProcessor;

namespace camshaft
{

class Tachometer : public juce::Component,
                   public juce::SettableTooltipClient,
                   private juce::Timer
{
public:
    explicit Tachometer (CamshaftAudioProcessor& processor);
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;

    CamshaftAudioProcessor& processor;
    float needleDb = -60.0f, reductionDb = 0.0f;
    static constexpr float minDb = -48.0f, maxDb = 3.0f;
};

class MasterView : public juce::Component
{
public:
    explicit MasterView (CamshaftAudioProcessor& processor);
    void resized() override;

private:
    Tachometer tach;
    fxme::Vectorscope scope;
};

} // namespace camshaft
