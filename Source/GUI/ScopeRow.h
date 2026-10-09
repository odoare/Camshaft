/*
  ------------------------------------------------------------------------------
    ScopeRow.h

    One cam's display row, above its strip:

      CamProfileView  the cam's current wave drawn in polar form, as a real
                      cam profile: radius = base + wave. It turns while a
                      note plays (slowed to a visible rate) with a follower
                      riding its edge. The wave is built here, on the
                      message thread, with the same builder the engine uses
                      (level 0 of the table), from the unmodulated position.
      Oscilloscope    the cam's output in the newest voice (post filter and
                      envelope, before its volume), triggered on a rising
                      zero crossing.

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

class CamProfileView : public juce::Component,
                       public juce::SettableTooltipClient,
                       private juce::Timer
{
public:
    CamProfileView (CamshaftAudioProcessor& processor, int cam);

    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;
    void rebuild();

    CamshaftAudioProcessor& processor;
    int cam;
    juce::Colour colour;

    fxme::WavetableBuilder builder;
    fxme::WavetableSet set;
    std::vector<float> wave;                     // 256 points of level 0, normalised
    fxme::WavetableCache::Key shownKey;
    const void* shownSource = nullptr;

    float rotation = 0.0f;                       // turns
    bool playing = false;
    double lastTime = 0.0;
};

class Oscilloscope : public juce::Component,
                     public juce::SettableTooltipClient,
                     private juce::Timer
{
public:
    Oscilloscope (CamshaftAudioProcessor& processor, int cam);

    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;

    CamshaftAudioProcessor& processor;
    int cam;
    juce::Colour colour;
    std::vector<float> left, right;
    std::int64_t lastTotal = -1;
    bool silent = true;
};

class ScopeRow : public juce::Component
{
public:
    ScopeRow (CamshaftAudioProcessor& processor, int cam);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    int cam;
    CamProfileView profile;
    Oscilloscope scope;
};

} // namespace camshaft
