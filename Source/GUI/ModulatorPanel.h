/*
  ------------------------------------------------------------------------------
    ModulatorPanel.h

    The 16 modulator slots and the fixed MIDI / voice sources.

      Left:  one chip per slot (its name after its type, "LFO 3", and a bar
             with its live output; dimmed, without a bar, when the slot is
             off or feeds nothing, since it is then not computed) and the
             fixed sources (velocity, key, wheel, CCs, the cams' amp
             envelopes...). Click a slot chip to edit it; drag any chip onto
             a knob to modulate that knob (an "Off" slot becomes an LFO).
      Right: the selected slot's editor: type (LFO, ADSR, curve, Off) and
             output level, then
               LFO    shape, rate or tempo division, phase, delay, fade-in,
                      unipolar / bipolar, per-voice retrigger or global
               ADSR   the four stages, their curves, velocity amount
               curve  the breakpoint editor (live playheads), duration in
                      seconds or beats, speed, sustain region (hold, loop or
                      back and forth), release region, stick to DAW time

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

/** A draggable modulation source: a slot (clickable) or a fixed source. */
class SourceChip : public juce::Component,
                   public juce::SettableTooltipClient,
                   private juce::Timer
{
public:
    SourceChip (CamshaftAudioProcessor& processor, int source, juce::Colour colour);

    std::function<void()> onClick;
    void setSelected (bool s) { if (s != selected) { selected = s; repaint(); } }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void timerCallback() override;

    CamshaftAudioProcessor& processor;
    int source;
    juce::Colour colour;
    juce::String name;
    float level = 0.0f;
    bool bipolar = false, selected = false, dragging = false;
    bool inUse = true;    // a modulator slot that feeds nothing (or is off) is drawn dimmed, without its bar
};

/** The editor of one modulator slot (remade when another slot is chosen). */
class ModulatorEditor : public juce::Component,
                        private juce::Timer
{
public:
    ModulatorEditor (CamshaftAudioProcessor& processor, int slot);
    ~ModulatorEditor() override;

    /** While a point of the curve is held: the slot and the curve's value
        there (0 to 1); slot -1 when it is let go. */
    std::function<void (int slot, float value)> onCurveProbe;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void layoutForType();
    void curveChanged (const fxme::CurveShape& shape);
    CamKnob& knob (int field, const char* tooltip, bool bipolar = false);
    juce::ComboBox& combo (int field, const char* tooltip);
    fxme::FxmeButton& toggle (int field, const char* label, const char* tooltip);

    CamshaftAudioProcessor& processor;
    int slot;
    juce::Colour colour;                // the controls: the plugin's accent
    juce::Colour slotColour;            // the modulator's own: its name, its curve
    inline static const juce::Colour sustainBlue { 0xff40c0ff };
    int shownType = -1;

    std::vector<std::unique_ptr<juce::Component>> owned;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>> attachments;

    juce::ComboBox* typeBox = nullptr;
    CamKnob* level = nullptr;
    fxme::FxmeButton* bipolar = nullptr;

    // LFO
    juce::ComboBox *shapeBox = nullptr, *divisionBox = nullptr, *modeBox = nullptr;
    CamKnob *rate = nullptr, *phase = nullptr, *delay = nullptr, *fade = nullptr;
    fxme::FxmeButton* sync = nullptr;
    // ADSR
    CamKnob *attack = nullptr, *decay = nullptr, *sustain = nullptr, *release = nullptr,
            *attackCurve = nullptr, *decayCurve = nullptr, *releaseCurve = nullptr, *velocity = nullptr;
    // Curve
    fxme::BreakpointCurveEditor curveEditor;
    CamKnob *duration = nullptr, *beats = nullptr, *speed = nullptr;
    juce::ComboBox* durationMode = nullptr;
    fxme::FxmeButton* dawSync = nullptr;
    juce::ToggleButton sustainToggle { "Sustain" }, releaseToggle { "Release" };
    juce::ComboBox sustainModeBox;    // Hold, Loop, Back & forth
};

class ModulatorPanel : public juce::Component
{
public:
    explicit ModulatorPanel (CamshaftAudioProcessor& processor);

    void paint (juce::Graphics&) override;
    void resized() override;

    void select (int slot);

    /** See ModulatorEditor::onCurveProbe. */
    std::function<void (int slot, float value)> onCurveProbe;

private:
    CamshaftAudioProcessor& processor;
    std::vector<std::unique_ptr<SourceChip>> slotChips, fixedChips;
    std::unique_ptr<ModulatorEditor> editor;
    int selected = 0;
};

} // namespace camshaft
