/*
  ------------------------------------------------------------------------------
    Dashboard.h

    The right half of the "Belt & Viz" pane, laid out like a car's
    instrument cluster:

      dials         the tachometer (output level, the rev limiter's
                    redline), the spectrum in the middle where the
                    speedometer sits (fxme::CircularSpectrum), and the
                    vectorscope (stereo image)
      warning lights
                    REV    the rev limiter is reducing the level
                    CHECK  the audio processing is near or over its time
                           budget (DSP load above 90 %)
                    STEAL  a voice was stolen: the polyphony is used up
                    MIDI   notes are coming in
                    HOLD   the sustain pedal is down
                    SYNC   the host is playing: synced LFOs and curves
                           follow its position
                    T1 T2  the Turbo chains are carrying signal
      gauges        FUEL   the voices still free (it drains as notes pile up)
                    TEMP   the DSP load
      controls      the gear selector (P, M, L: poly, mono, legato), the rev
                    limiter switch, and the hazard button (panic: every voice
                    silenced at once)
      throttle      the output level as an accelerator pedal: drag it down to
                    press it (louder), up to let it off; wheel; double-click
                    for the default
      full view     the small button in the top-left corner: the owner
                    (SynthTab) gives the dashboard the whole lower half, or
                    takes it back

    Also here: OutputMeter, the stereo level meter of the top bar.

    Everything reads atomics on a 30 Hz timer; a light holds for a moment
    after a short event (a steal, a note) so it can be seen.

    Author: Olivier Doaré, github.com/odoare
    Dual-licensed, mirroring the JUCE framework it depends on: under the GNU
    AGPL Version 3.0, or under commercial terms available from the author.
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>
#include "MasterView.h"

class CamshaftAudioProcessor;

namespace camshaft
{

/** A tell-tale: a small rounded lamp with its label engraved under it. */
class WarningLight : public juce::Component,
                     public juce::SettableTooltipClient
{
public:
    WarningLight (const juce::String& label, juce::Colour colour);
    void setOn (bool on);
    void paint (juce::Graphics&) override;

private:
    juce::String label;
    juce::Colour colour;
    bool lit = false;
};

/** A horizontal bar gauge with a label, 0 to 1, red past `warnAbove`. */
class BarGauge : public juce::Component,
                 public juce::SettableTooltipClient
{
public:
    BarGauge (const juce::String& label, juce::Colour colour, float warnAbove, bool warnBelow);
    void setValue (float v);
    void paint (juce::Graphics&) override;

private:
    juce::String label;
    juce::Colour colour;
    float warn;
    bool below;
    float value = 0.0f;
};

/** P / M / L: the voice mode, as a gear selector. */
class GearSelector : public juce::Component,
                     public juce::SettableTooltipClient
{
public:
    explicit GearSelector (CamshaftAudioProcessor& processor);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    int gearAt (juce::Point<float> p) const;
    juce::ParameterAttachment attachment;
    int gear = 0;
};

/** The output level as an accelerator pedal. */
class ThrottlePedal : public juce::Component,
                      public juce::SettableTooltipClient
{
public:
    explicit ThrottlePedal (CamshaftAudioProcessor& processor);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    juce::RangedAudioParameter& param;
    juce::ParameterAttachment attachment;
    float value = 0.0f;           // in dB
    float dragStartNorm = 0.0f;
};

/** Full view on or off: four corner brackets, pointing out (expand) or in
    (back). */
class ExpandButton : public juce::Button
{
public:
    ExpandButton() : juce::Button ("full view") { setMouseClickGrabsKeyboardFocus (false); }
    void setExpanded (bool e) { expanded = e; repaint(); }
    void paintButton (juce::Graphics&, bool over, bool down) override;

private:
    bool expanded = false;
};

/** The hazard button: a red triangle; click to silence every voice. */
class HazardButton : public juce::Button
{
public:
    HazardButton() : juce::Button ("hazard") { setMouseClickGrabsKeyboardFocus (false); }
    void paintButton (juce::Graphics&, bool over, bool down) override;
};

class Dashboard : public juce::Component,
                  private juce::Timer
{
public:
    explicit Dashboard (CamshaftAudioProcessor& processor);

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Called by the full-view button; the owner resizes the dashboard and
        calls setFullView(). */
    std::function<void()> onToggleFullView;
    void setFullView (bool on) { expand.setExpanded (on); }

private:
    void timerCallback() override;

    CamshaftAudioProcessor& processor;

    Tachometer tach;
    fxme::CircularSpectrum spectrum;
    fxme::Vectorscope scope;

    WarningLight rev, check, steal, midi, hold, sync, turbo1, turbo2;
    BarGauge fuel, temp;
    GearSelector gears;
    fxme::FxmeButton limiterSwitch;
    HazardButton hazard;
    ThrottlePedal throttle;
    ExpandButton expand;

    std::array<juce::Rectangle<float>, 3> bezels;
    int lastSteals = 0, lastNotes = 0;
    double stealUntil = 0.0, midiUntil = 0.0, checkUntil = 0.0;
};

/** The top bar's output meter: L and R as two horizontal bars. */
class OutputMeter : public juce::Component,
                    private juce::Timer
{
public:
    explicit OutputMeter (CamshaftAudioProcessor& processor);
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;

    CamshaftAudioProcessor& processor;
    fxme::StereoLevelMeter meter;
};

} // namespace camshaft
