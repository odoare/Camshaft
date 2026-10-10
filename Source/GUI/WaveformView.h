/*
  ------------------------------------------------------------------------------
    WaveformView.h

    The imported audio and the four cams' ranges on it.

      - Overview from the source's min / max pyramid, any zoom.
      - Each cam's range in its colour, as set by its opening and duration:
        drag inside it, or its numbered flag (always grabbable, even on a
        narrow range), to move it; drag an edge to move that edge only (the
        opening and the duration adapt, the other edge stays). The overlap
        (loop crossfade) zone is hatched at its end.
      - While notes play, what every voice reads: one lane per cam along the
        bottom with a bar per voice (its range, modulation, snapping and
        smoothing included) and a tick where it reads; the newest voice's
        read position also runs across the whole height.
      - While a point of a curve modulator is held (setProbe), where its
        value puts the range of each cam whose opening or duration that
        curve modulates (the curve's contribution alone, on the knobs'
        values): the edge it moves as a marker, the range as a ghost.
      - Wheel zooms around the mouse, shift+wheel or a drag on empty space
        scrolls, double-click on empty space shows the whole file.
      - Drop an audio file anywhere on it to import it.

    Every gesture writes the cam's parameters through the host (begin /
    end gesture), and the view follows the parameters on a timer, so
    automation, presets and modulation show up without extra wiring.

    Author: Olivier Doaré, github.com/odoare
    Dual-licensed, mirroring the JUCE framework it depends on: under the GNU
    AGPL Version 3.0, or under commercial terms available from the author.
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>
#include "Shell/SourceManager.h"
#include "Engine/CamParams.h"

class CamshaftAudioProcessor;

namespace camshaft
{

class WaveformView : public juce::Component,
                     public juce::SettableTooltipClient,
                     public juce::FileDragAndDropTarget,
                     private juce::Timer,
                     private juce::ChangeListener
{
public:
    explicit WaveformView (CamshaftAudioProcessor& processor);
    ~WaveformView() override;

    /** Called after a file was dropped and imported (or failed: the message). */
    std::function<void (const juce::String& error)> onImported;

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    /** A curve modulator's point is held at `value` (0 to 1): mark where it
        puts the ranges it modulates. slot -1: none. */
    void setProbe (int slot, float value);

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragHover = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override              { dragHover = false; repaint(); }
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    struct Range
    {
        double start = 0.0, length = 0.0;   // in source samples
        double overlap = 0.0;               // samples blended at the end
        bool audible = false;
    };

    enum class Hit { none, body, flag, leftEdge, rightEdge };

    /** What one voice reads on one cam (Display::VoiceRange); phase < 0: nothing. */
    struct Live
    {
        float start = 0.0f, cycle = 0.0f, overlap = 0.0f, phase = -1.0f;
    };

    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    Range rangeOf (int cam) const;
    juce::Rectangle<float> plotArea() const;
    double xToSample (float x) const;
    float sampleToX (double s) const;
    std::pair<int, Hit> hitTest (juce::Point<float> p) const;
    juce::Rectangle<float> flagArea (int cam) const;
    float laneTop (int cam) const;
    void paintLive (juce::Graphics& g, int cam) const;
    void paintProbe (juce::Graphics& g) const;
    void setParameter (int cam, int field, float value);
    void beginGesture (int cam, Hit hit);
    void endGesture();
    void clampView();

    CamshaftAudioProcessor& processor;
    std::shared_ptr<const SourceManager::View> view;
    int numSamples = 0;

    double viewStart = 0.0, viewLength = 1.0;   // in source samples
    int dragCam = -1;
    Hit dragHit = Hit::none;
    double dragStartStart = 0.0, dragStartLength = 0.0, dragStartView = 0.0;   // range in samples
    bool panning = false, dragHover = false;
    int hoverCam = -1;
    Hit hoverHit = Hit::none;

    std::array<Range, cam::numCams> shown {};
    std::array<std::array<Live, cam::numCams>, cam::maxVoices> live {};
    int newestVoice = -1;

    int probeSlot = -1;
    float probeValue = 0.0f;
};

} // namespace camshaft
