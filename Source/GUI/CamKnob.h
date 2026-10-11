/*
  ------------------------------------------------------------------------------
    CamKnob.h

    Every knob in Camshaft: an fxme::FxmeSlider bound to a parameter, plus

      - right-click menu: type a value (FxmeSlider's own entry), learn / forget
        MIDI, modulate from any source, remove its modulations, lock it from
        Kick-start (randomize);
      - a modulation ring outside the value arc: the span of every route into
        the parameter, and a dot at the live (modulated) value of the newest
        voice;
      - a drop target: drop a modulator or MIDI-source chip on it to route
        that source to it;
      - small badges: "M" when MIDI-mapped, a padlock when locked, a
        blinking outline while learning.

    Also sourceName(): the display name of a modulation source, after its
    type ("LFO 3", "Curve 7", "Velocity").

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

/** "LFO 3", "ADSR 1", "Curve 7", "Velocity", "Cam 2 env"... */
juce::String sourceName (juce::AudioProcessorValueTreeState& apvts, int source);

/** Whether a source can go negative (an LFO or curve set to bipolar, pitch
    bend, key track, random): its routes span both ways, its meter grows
    from the centre. */
bool sourceIsBipolar (juce::AudioProcessorValueTreeState& apvts, int source);

/** A source's colour: a modulator slot's own (grey when off), a cam's for
    its amp envelope, steel for the MIDI and voice sources. */
juce::Colour sourceColour (juce::AudioProcessorValueTreeState& apvts, int source);

/** Drag description of a modulation source chip: "modsrc:<index>". */
juce::String sourceDragDescription (int source);
int sourceFromDragDescription (const juce::var& description);

/** A popup menu of every modulation source; item IDs are 1 + source index. */
juce::PopupMenu sourcesMenu (juce::AudioProcessorValueTreeState& apvts, int ticked = -1, bool withNone = false);

class CamKnob : public fxme::FxmeSlider,
                public juce::DragAndDropTarget,
                private juce::Timer
{
public:
    CamKnob (CamshaftAudioProcessor& processor, const juce::String& paramID,
             const juce::String& label, juce::Colour accent, bool bipolar = false);
    ~CamKnob() override;

    const juce::String& getParamID() const noexcept { return paramID; }

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;

    bool isInterestedInDragSource (const SourceDetails& details) override;
    void itemDragEnter (const SourceDetails&) override { dropHover = true; repaint(); }
    void itemDragExit (const SourceDetails&) override  { dropHover = false; repaint(); }
    void itemDropped (const SourceDetails& details) override;

private:
    void timerCallback() override;
    void showMenu();
    float liveValue() const;   // normalised, -1 when unknown

    CamshaftAudioProcessor& processor;
    juce::String paramID;
    juce::Colour accent;
    juce::RangedAudioParameter* param = nullptr;
    int camIndex = -1, camField = -1;          // for the live value of a cam field
    std::atomic<float>* raw = nullptr;

    // What is drawn, refreshed by the timer.
    float ringLo = 0.0f, ringHi = 0.0f;
    /** One ring per route into the knob, in its source's colour. */
    struct Ring
    {
        float lo = 0.0f, hi = 0.0f;
        juce::Colour colour;
        bool operator!= (const Ring& o) const noexcept
        {
            return std::abs (lo - o.lo) > 1.0e-6f || std::abs (hi - o.hi) > 1.0e-6f || colour != o.colour;
        }
    };
    std::vector<Ring> rings;
    juce::Array<int> routesIn;     // the routes into this knob, kept until the routes change
    int routesSeen = -1;
    bool hasRing = false;
    float live = -1.0f;
    bool mapped = false, locked = false, learning = false, dropHover = false;
    int blink = 0;
};

} // namespace camshaft
