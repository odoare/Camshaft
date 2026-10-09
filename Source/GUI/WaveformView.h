/*
  ------------------------------------------------------------------------------
    WaveformView.h

    The imported audio and the four cams' ranges on it.

      - Overview from the source's min / max pyramid, any zoom.
      - Each cam's range in its colour: drag inside it to move the centre,
        drag an edge to change the lobe (width), both symmetric around the
        centre; the overlap (loop crossfade) zone is hatched at its end.
      - A playhead per cam while a note plays (the newest voice's phase).
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

    enum class Hit { none, body, leftEdge, rightEdge };

    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    Range rangeOf (int cam) const;
    juce::Rectangle<float> plotArea() const;
    double xToSample (float x) const;
    float sampleToX (double s) const;
    std::pair<int, Hit> hitTest (juce::Point<float> p) const;
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
    double dragStartCentre = 0.0, dragStartWidth = 0.0, dragStartView = 0.0;
    bool panning = false, dragHover = false;
    int hoverCam = -1;
    Hit hoverHit = Hit::none;

    std::array<float, 4> playhead {};   // normalised phase, -1 when silent
    std::array<Range, 4> shown {};
};

} // namespace camshaft
