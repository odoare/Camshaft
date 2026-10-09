/*
  ------------------------------------------------------------------------------
    WaveformView.cpp

    See WaveformView.h.

    Author: Olivier Doaré, github.com/odoare
    Dual-licensed, mirroring the JUCE framework it depends on: under the GNU
    AGPL Version 3.0, or under commercial terms available from the author.
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
  ------------------------------------------------------------------------------
*/

#include "WaveformView.h"
#include "PluginProcessor.h"
#include "ParamIDs.h"
#include "Theme.h"

namespace camshaft
{

using namespace cam;

WaveformView::WaveformView (CamshaftAudioProcessor& p) : processor (p)
{
    processor.getSources().addChangeListener (this);
    changeListenerCallback (nullptr);
    playhead.fill (-1.0f);
    startTimerHz (30);
}

WaveformView::~WaveformView()
{
    processor.getSources().removeChangeListener (this);
}

void WaveformView::changeListenerCallback (juce::ChangeBroadcaster*)
{
    view = processor.getSources().getView();
    const int n = view != nullptr && view->source != nullptr ? view->source->numSamples : 0;
    if (n != numSamples)
    {
        numSamples = n;
        viewStart = 0.0;
        viewLength = juce::jmax (1.0, (double) n);
    }
    repaint();
}

//==============================================================================
WaveformView::Range WaveformView::rangeOf (int c) const
{
    Range r;
    if (numSamples <= 0)
        return r;
    auto& apvts = processor.getAPVTS();
    const float centre = apvts.getRawParameterValue (ids::cam (c, cfCentre))->load();
    const float width = apvts.getRawParameterValue (ids::cam (c, cfWidth))->load();
    const float overlap = apvts.getRawParameterValue (ids::cam (c, cfOverlap))->load();
    r.length = juce::jlimit (8.0, (double) numSamples, (double) width);
    r.start = juce::jlimit (0.0, (double) numSamples - r.length, std::round ((double) centre * numSamples - 0.5 * r.length));
    r.overlap = r.length * juce::jlimit (0.0, 0.5, (double) overlap * 0.01);
    r.audible = apvts.getRawParameterValue (ids::cam (c, cfAudible))->load() > 0.5f;
    return r;
}

juce::Rectangle<float> WaveformView::plotArea() const
{
    return getLocalBounds().toFloat().reduced (2.0f, 2.0f);
}

double WaveformView::xToSample (float x) const
{
    const auto r = plotArea();
    return viewStart + (double) ((x - r.getX()) / r.getWidth()) * viewLength;
}

float WaveformView::sampleToX (double s) const
{
    const auto r = plotArea();
    return r.getX() + (float) ((s - viewStart) / viewLength) * r.getWidth();
}

void WaveformView::clampView()
{
    const double n = juce::jmax (1.0, (double) numSamples);
    viewLength = juce::jlimit (64.0, n, viewLength);
    viewStart = juce::jlimit (0.0, n - viewLength, viewStart);
}

void WaveformView::timerCallback()
{
    bool changed = false;
    const auto& display = processor.getEngine().getDisplay();
    for (int c = 0; c < numCams; ++c)
    {
        const auto r = rangeOf (c);
        auto& s = shown[(size_t) c];
        auto differs = [] (double a, double b) { return std::abs (a - b) > 1.0e-9; };
        if (differs (r.start, s.start) || differs (r.length, s.length) || differs (r.overlap, s.overlap) || r.audible != s.audible)
        {
            s = r;
            changed = true;
        }
        const float ph = display.active[(size_t) c].load() ? display.phase[(size_t) c].load() : -1.0f;
        if (std::abs (ph - playhead[(size_t) c]) > 1.0e-4f)
        {
            playhead[(size_t) c] = ph;
            changed = true;
        }
    }
    if (changed)
        repaint();
}

//==============================================================================
void WaveformView::paint (juce::Graphics& g)
{
    const auto area = plotArea();
    g.setColour (theme::plotBack);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);

    // Centre line.
    g.setColour (theme::grid);
    g.drawHorizontalLine ((int) area.getCentreY(), area.getX(), area.getRight());

    if (view == nullptr || view->source == nullptr || numSamples <= 0)
    {
        theme::paintEngraved (g, "Drop an audio file here", getLocalBounds(), juce::Justification::centred, 13.0f);
        return;
    }

    // The waveform: min / max per pixel column, channels overlaid.
    const int w = (int) area.getWidth();
    const float mid = area.getCentreY(), half = area.getHeight() * 0.48f;
    for (size_t ch = 0; ch < view->overview.size(); ++ch)
    {
        g.setColour ((ch == 0 ? theme::text : theme::steel).withAlpha (ch == 0 ? 0.55f : 0.35f));
        const auto& pyramid = view->overview[ch];
        for (int x = 0; x < w; ++x)
        {
            const int a = (int) (viewStart + viewLength * (double) x / (double) w);
            const int b = juce::jmax (a + 1, (int) (viewStart + viewLength * (double) (x + 1) / (double) w));
            float lo, hi;
            pyramid.rangeAt (a, b, lo, hi);
            g.drawVerticalLine ((int) area.getX() + x, mid - hi * half, mid - lo * half + 1.0f);
        }
    }

    // The cams' ranges, cam 1 on top.
    for (int c = numCams; --c >= 0;)
    {
        const auto& r = shown[(size_t) c];
        const auto colour = theme::camColour (c);
        const float x0 = sampleToX (r.start), x1 = sampleToX (r.start + r.length);
        if (x1 < area.getX() || x0 > area.getRight())
            continue;

        const float alpha = r.audible ? 1.0f : 0.45f;
        const auto box = juce::Rectangle<float> (x0, area.getY(), juce::jmax (1.5f, x1 - x0), area.getHeight());
        g.setColour (colour.withAlpha (0.10f * alpha));
        g.fillRect (box);

        // Overlap zone at the end of the range, hatched.
        if (r.overlap > 0.0)
        {
            const float xo = sampleToX (r.start + r.length - r.overlap);
            juce::Graphics::ScopedSaveState s (g);
            g.reduceClipRegion (juce::Rectangle<float> (xo, area.getY(), x1 - xo, area.getHeight()).toNearestInt());
            g.setColour (colour.withAlpha (0.25f * alpha));
            for (float hx = xo - area.getHeight(); hx < x1; hx += 6.0f)
                g.drawLine (hx, area.getBottom(), hx + area.getHeight(), area.getY(), 1.0f);
        }

        const bool hot = (hoverCam == c && hoverHit != Hit::none) || dragCam == c;
        g.setColour (colour.withAlpha ((hot ? 1.0f : 0.75f) * alpha));
        g.drawVerticalLine ((int) x0, area.getY(), area.getBottom());
        g.drawVerticalLine ((int) x1, area.getY(), area.getBottom());
        if (hot && (hoverHit == Hit::leftEdge || dragHit == Hit::leftEdge))
            g.fillRect (juce::Rectangle<float> (x0 - 1.5f, area.getY(), 3.0f, area.getHeight()));
        if (hot && (hoverHit == Hit::rightEdge || dragHit == Hit::rightEdge))
            g.fillRect (juce::Rectangle<float> (x1 - 1.5f, area.getY(), 3.0f, area.getHeight()));

        // Tab with the cam number.
        const auto tab = juce::Rectangle<float> (x0, area.getY() + (float) c * 15.0f, 16.0f, 13.0f);
        g.setColour (colour.withAlpha (alpha));
        g.fillRoundedRectangle (tab, 2.0f);
        g.setColour (theme::background);
        g.setFont (theme::labelFont (10.0f));
        g.drawText (juce::String (c + 1), tab, juce::Justification::centred);

        // Playhead of the newest voice: the phase runs over the cycle,
        // which the overlap shortens.
        const float ph = playhead[(size_t) c];
        if (ph >= 0.0f)
        {
            const float xp = sampleToX (r.start + (double) ph * (r.length - r.overlap));
            g.setColour (colour.brighter (0.5f));
            g.drawLine (xp, area.getY(), xp, area.getBottom(), 1.5f);
        }
    }

    // Zoom gauge.
    if (viewLength < (double) numSamples - 1.0)
    {
        const float bw = area.getWidth();
        const float zx = area.getX() + (float) (viewStart / numSamples) * bw;
        const float zw = juce::jmax (4.0f, (float) (viewLength / numSamples) * bw);
        g.setColour (theme::accent.withAlpha (0.5f));
        g.fillRect (zx, area.getBottom() - 2.0f, zw, 2.0f);
    }

    if (dragHover)
    {
        g.setColour (theme::accent.withAlpha (0.8f));
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 4.0f, 2.0f);
    }

    if (processor.getSources().isLoading())
        theme::paintEngraved (g, "Loading...", getLocalBounds().reduced (8), juce::Justification::topRight);
}

void WaveformView::resized() {}

//==============================================================================
std::pair<int, WaveformView::Hit> WaveformView::hitTest (juce::Point<float> p) const
{
    // Edges first, any cam; then the narrowest range under the mouse.
    for (int c = 0; c < numCams; ++c)
    {
        const auto& r = shown[(size_t) c];
        if (std::abs (p.x - sampleToX (r.start)) < 5.0f)                return { c, Hit::leftEdge };
        if (std::abs (p.x - sampleToX (r.start + r.length)) < 5.0f)     return { c, Hit::rightEdge };
    }
    int best = -1;
    double bestLen = 1.0e30;
    const double s = xToSample (p.x);
    for (int c = 0; c < numCams; ++c)
    {
        const auto& r = shown[(size_t) c];
        if (s >= r.start && s <= r.start + r.length && r.length < bestLen)
        {
            best = c;
            bestLen = r.length;
        }
    }
    return { best, best >= 0 ? Hit::body : Hit::none };
}

void WaveformView::mouseMove (const juce::MouseEvent& e)
{
    const auto [c, hit] = hitTest (e.position);
    if (c != hoverCam || hit != hoverHit)
    {
        hoverCam = c;
        hoverHit = hit;
        setMouseCursor (hit == Hit::body ? juce::MouseCursor::DraggingHandCursor
                      : hit == Hit::none ? juce::MouseCursor::NormalCursor
                                         : juce::MouseCursor::LeftRightResizeCursor);
        repaint();
    }
}

void WaveformView::setParameter (int cam, int field, float value)
{
    if (auto* p = processor.getAPVTS().getParameter (ids::cam (cam, field)))
        p->setValueNotifyingHost (p->convertTo0to1 (value));
}

void WaveformView::beginGesture (int cam, Hit hit)
{
    dragCam = cam;
    dragHit = hit;
    auto& apvts = processor.getAPVTS();
    dragStartCentre = apvts.getRawParameterValue (ids::cam (cam, cfCentre))->load();
    dragStartWidth = apvts.getRawParameterValue (ids::cam (cam, cfWidth))->load();
    for (int f : { (int) cfCentre, (int) cfWidth })
        if (auto* p = apvts.getParameter (ids::cam (cam, f)))
            p->beginChangeGesture();
}

void WaveformView::endGesture()
{
    if (dragCam >= 0)
        for (int f : { (int) cfCentre, (int) cfWidth })
            if (auto* p = processor.getAPVTS().getParameter (ids::cam (dragCam, f)))
                p->endChangeGesture();
    dragCam = -1;
    dragHit = Hit::none;
}

void WaveformView::mouseDown (const juce::MouseEvent& e)
{
    if (numSamples <= 0)
        return;
    const auto [c, hit] = hitTest (e.position);
    if (c >= 0 && ! e.mods.isMiddleButtonDown() && ! e.mods.isAltDown())
    {
        beginGesture (c, hit);
        return;
    }
    panning = true;
    dragStartView = viewStart;
}

void WaveformView::mouseDrag (const juce::MouseEvent& e)
{
    const double perPixel = viewLength / (double) juce::jmax (1.0f, plotArea().getWidth());
    const double dx = (double) e.getDistanceFromDragStartX() * perPixel;

    if (panning)
    {
        viewStart = dragStartView - dx;
        clampView();
        repaint();
        return;
    }
    if (dragCam < 0 || numSamples <= 0)
        return;

    if (dragHit == Hit::body)
        setParameter (dragCam, cfCentre, (float) juce::jlimit (0.0, 1.0, dragStartCentre + dx / numSamples));
    else
    {
        // Edges move symmetrically around the centre.
        const double sign = dragHit == Hit::rightEdge ? 2.0 : -2.0;
        const double width = juce::jlimit ((double) camSpecs[cfWidth].min, (double) camSpecs[cfWidth].max,
                                           dragStartWidth + sign * dx);
        setParameter (dragCam, cfWidth, (float) width);
    }
}

void WaveformView::mouseUp (const juce::MouseEvent&)
{
    panning = false;
    endGesture();
}

void WaveformView::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto [c, hit] = hitTest (e.position);
    if (c >= 0 && hit == Hit::body)
    {
        const auto& r = shown[(size_t) c];
        viewLength = r.length * 1.5;
        viewStart = r.start - r.length * 0.25;
    }
    else
    {
        viewStart = 0.0;
        viewLength = (double) numSamples;
    }
    clampView();
    repaint();
}

void WaveformView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (numSamples <= 0)
        return;
    const float delta = std::abs (wheel.deltaY) > std::abs (wheel.deltaX) ? wheel.deltaY : -wheel.deltaX;
    if (e.mods.isShiftDown() || std::abs (wheel.deltaX) > std::abs (wheel.deltaY))
        viewStart -= (double) delta * viewLength * 0.25;
    else
    {
        const double anchor = xToSample (e.position.x);
        const double factor = std::pow (0.8, (double) delta * 4.0);
        viewLength *= factor;
        clampView();
        viewStart = anchor - (anchor - viewStart) * factor;
    }
    clampView();
    repaint();
}

//==============================================================================
bool WaveformView::isInterestedInFileDrag (const juce::StringArray& files)
{
    return files.size() == 1 && SourceManager::canImport (files[0]);
}

void WaveformView::filesDropped (const juce::StringArray& files, int, int)
{
    dragHover = false;
    const auto error = processor.getSources().importFile (juce::File (files[0]));
    if (onImported)
        onImported (error);
    repaint();
}

} // namespace camshaft
