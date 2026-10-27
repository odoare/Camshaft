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
#include "CamKnob.h"
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
    auto value = [&] (int f) { return apvts.getRawParameterValue (ids::cam (c, f))->load(); };
    camRange (value (cfOpening), value (cfDuration), (double) numSamples, r.start, r.length);
    r.overlap = r.length * juce::jlimit (0.0, 0.5, (double) value (cfOverlap) * 0.01);
    r.audible = value (cfAudible) > 0.5f;
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
    }

    // What the voices read. Only redrawn when something moved: nothing
    // playing, nothing to do.
    const auto& display = processor.getEngine().getDisplay();
    for (int v = 0; v < maxVoices; ++v)
        for (int c = 0; c < numCams; ++c)
        {
            const auto& from = display.ranges[(size_t) v][(size_t) c];
            Live now;
            now.phase = from.phase.load (std::memory_order_relaxed);
            if (now.phase >= 0.0f)
            {
                now.start = from.start.load (std::memory_order_relaxed);
                now.cycle = from.cycle.load (std::memory_order_relaxed);
                now.overlap = from.overlap.load (std::memory_order_relaxed);
            }
            auto& was = live[(size_t) v][(size_t) c];
            if (std::abs (now.phase - was.phase) > 1.0e-4f || std::abs (now.start - was.start) > 0.25f
                || std::abs (now.cycle - was.cycle) > 0.25f || std::abs (now.overlap - was.overlap) > 0.25f)
            {
                was = now;
                changed = true;
            }
        }
    const int newest = display.newestVoice.load (std::memory_order_relaxed);
    if (newest != newestVoice)
    {
        newestVoice = newest;
        changed = true;
    }

    if (changed)
        repaint();
}

void WaveformView::setProbe (int slot, float value)
{
    if (slot != probeSlot || (slot >= 0 && std::abs (value - probeValue) > 1.0e-6f))
    {
        probeSlot = slot;
        probeValue = value;
        repaint();
    }
}

juce::Rectangle<float> WaveformView::flagArea (int c) const
{
    const auto area = plotArea();
    return { sampleToX (shown[(size_t) c].start), area.getY() + (float) c * 15.0f, 16.0f, 13.0f };
}

float WaveformView::laneTop (int c) const
{
    // Cam 1's lane lowest, just above the zoom gauge.
    constexpr float laneHeight = 5.0f;
    return plotArea().getBottom() - 3.0f - (float) (c + 1) * (laneHeight + 1.0f);
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

    drawWaveform (g, area);

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

        // Flag with the cam number (grab it to move the range).
        const auto tab = flagArea (c);
        g.setColour (colour.withAlpha (alpha));
        g.fillRoundedRectangle (tab, 2.0f);
        g.setColour (theme::background);
        g.setFont (theme::labelFont (10.0f));
        g.drawText (juce::String (c + 1), tab, juce::Justification::centred);
    }

    for (int c = numCams; --c >= 0;)
        paintLive (g, c);
    paintProbe (g);

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

void WaveformView::paintLive (juce::Graphics& g, int c) const
{
    const auto area = plotArea();
    const auto colour = theme::camColour (c);
    const float top = laneTop (c), height = 5.0f;
    bool any = false;

    for (int v = 0; v < maxVoices; ++v)
    {
        const auto& l = live[(size_t) v][(size_t) c];
        if (l.phase < 0.0f)
            continue;
        any = true;

        // The range this voice reads, and where it reads in it (the phase
        // runs over the cycle, which the overlap shortens).
        const float x0 = juce::jmax (area.getX(), sampleToX ((double) l.start));
        const float x1 = juce::jmin (area.getRight(), sampleToX ((double) l.start + (double) l.cycle + (double) l.overlap));
        if (x1 > x0 - 1.0f)
        {
            g.setColour (colour.withAlpha (0.4f));
            g.fillRect (juce::Rectangle<float> (x0, top, juce::jmax (1.0f, x1 - x0), height));
        }
        const float xp = sampleToX ((double) l.start + (double) l.phase * (double) l.cycle);
        if (xp >= area.getX() && xp <= area.getRight())
        {
            g.setColour (colour.brighter (0.6f));
            g.fillRect (juce::Rectangle<float> (xp - 0.75f, top - 1.0f, 1.5f, height + 2.0f));
            if (v == newestVoice)
            {
                g.setColour (colour.brighter (0.5f).withAlpha (0.85f));
                g.drawLine (xp, area.getY(), xp, top, 1.2f);
            }
        }
    }

    if (any)
    {
        g.setColour (colour.withAlpha (0.25f));
        g.drawHorizontalLine ((int) (top + height), area.getX(), area.getRight());
    }
}

void WaveformView::paintProbe (juce::Graphics& g) const
{
    if (probeSlot < 0 || probeSlot >= numModulators || numSamples <= 0)
        return;

    auto& apvts = processor.getAPVTS();
    auto& structure = processor.getStructure();
    const auto area = plotArea();
    const int source = srcModFirst + probeSlot;

    // The curve's output at the held value, as the engine computes it.
    const bool bipolar = sourceIsBipolar (apvts, source);
    const float out = (bipolar ? 2.0f * probeValue - 1.0f : probeValue)
                    * apvts.getRawParameterValue (ids::mod (probeSlot, mfLevel))->load();

    for (int c = 0; c < numCams; ++c)
    {
        const auto startId = ids::cam (c, cfOpening), lengthId = ids::cam (c, cfDuration);
        float startOffset = 0.0f, lengthOffset = 0.0f;
        bool moves[2] {};     // opening, duration
        bool throughVia = false;
        for (int i = 0; i < maxRoutes; ++i)
        {
            const auto r = structure.getRoute (i);
            if (r.source != source || (r.dest != startId && r.dest != lengthId))
                continue;
            const float depth = apvts.getRawParameterValue (ids::routeDepth (i))->load();
            const bool isStart = r.dest == startId;
            (isStart ? startOffset : lengthOffset) += out * depth;
            moves[isStart ? 0 : 1] = true;
            throughVia = throughVia || r.via > 0;
        }
        if (! moves[0] && ! moves[1])
            continue;

        auto value = [&] (int f) { return apvts.getRawParameterValue (ids::cam (c, f))->load(); };
        const float startValue = camSpecs[cfOpening].fromNorm (camSpecs[cfOpening].toNorm (value (cfOpening)) + startOffset);
        const float lengthValue = camSpecs[cfDuration].fromNorm (camSpecs[cfDuration].toNorm (value (cfDuration)) + lengthOffset);
        double start, length;
        camRange (startValue, lengthValue, (double) numSamples, start, length);

        // The range where the curve puts it, as a ghost, and the edge it
        // moves as a marker (both edges when it moves the opening). A route
        // through a "via" source is drawn as if that source were at 1.
        const auto colour = theme::camColour (c);
        const float x0 = sampleToX (start), x1 = sampleToX (start + length);
        g.setColour (colour.withAlpha (0.12f));
        g.fillRect (juce::Rectangle<float> (x0, area.getY(), juce::jmax (1.0f, x1 - x0), area.getHeight()));

        auto marker = [&] (float x, bool strong)
        {
            if (x < area.getX() - 1.0f || x > area.getRight() + 1.0f)
                return;
            g.setColour (colour.brighter (0.4f).withAlpha (strong ? 1.0f : 0.6f));
            if (throughVia)
            {
                const float dashes[] { 4.0f, 3.0f };
                g.drawDashedLine ({ x, area.getY(), x, area.getBottom() }, dashes, 2, strong ? 2.0f : 1.0f);
            }
            else
                g.drawLine (x, area.getY(), x, area.getBottom(), strong ? 2.0f : 1.0f);
            // A small arrowhead at the top, pointing down.
            juce::Path tip;
            tip.addTriangle (x - 4.0f, area.getY(), x + 4.0f, area.getY(), x, area.getY() + 6.0f);
            g.fillPath (tip);
        };
        marker (x0, moves[0]);
        marker (x1, true);
    }
}

void WaveformView::drawWaveform (juce::Graphics& g, juce::Rectangle<float> area)
{
    const float scale = g.getInternalContext().getPhysicalPixelScaleFactor();
    const int w = juce::jmax (1, juce::roundToInt (area.getWidth() * scale));
    const int h = juce::jmax (1, juce::roundToInt (area.getHeight() * scale));
    const bool stale = ! waveImage.isValid() || waveImage.getWidth() != w || waveImage.getHeight() != h
                    || std::abs (imageStart - viewStart) > 1.0e-9 || std::abs (imageLength - viewLength) > 1.0e-9
                    || std::abs (imageScale - scale) > 1.0e-6f || imageSource != view.get();
    if (stale)
    {
        // Min / max per pixel column (physical pixels), channels overlaid.
        waveImage = juce::Image (juce::Image::ARGB, w, h, true);
        juce::Graphics ig (waveImage);
        const float mid = (float) h * 0.5f, half = (float) h * 0.48f;
        for (size_t ch = 0; ch < view->overview.size(); ++ch)
        {
            // Almost white, the second channel a little softer behind it.
            ig.setColour (theme::waveform.withAlpha (ch == 0 ? 0.85f : 0.55f));
            const auto& pyramid = view->overview[ch];
            for (int x = 0; x < w; ++x)
            {
                const int a = (int) (viewStart + viewLength * (double) x / (double) w);
                const int b = juce::jmax (a + 1, (int) (viewStart + viewLength * (double) (x + 1) / (double) w));
                float lo, hi;
                pyramid.rangeAt (a, b, lo, hi);
                ig.drawVerticalLine (x, mid - hi * half, mid - lo * half + 1.0f);
            }
        }
        imageStart = viewStart;
        imageLength = viewLength;
        imageScale = scale;
        imageSource = view.get();
    }
    g.drawImage (waveImage, area, juce::RectanglePlacement::stretchToFit);
}

void WaveformView::resized() {}

//==============================================================================
std::pair<int, WaveformView::Hit> WaveformView::hitTest (juce::Point<float> p) const
{
    // Flags first (cam 1's drawn on top), then edges, then the narrowest
    // range under the mouse.
    for (int c = 0; c < numCams; ++c)
        if (flagArea (c).expanded (1.0f).contains (p))
            return { c, Hit::flag };
    for (int c = 0; c < numCams; ++c)
    {
        const auto& r = shown[(size_t) c];
        const float dl = std::abs (p.x - sampleToX (r.start)), dr = std::abs (p.x - sampleToX (r.start + r.length));
        if (juce::jmin (dl, dr) < 5.0f)
            return { c, dl <= dr ? Hit::leftEdge : Hit::rightEdge };
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
        setMouseCursor (hit == Hit::body || hit == Hit::flag ? juce::MouseCursor::DraggingHandCursor
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
    // The range as shown (the duration cut short at the end of the file):
    // the edge not dragged stays where it is seen.
    const auto r = rangeOf (cam);
    dragStartStart = r.start;
    dragStartLength = r.length;
    for (int f : { (int) cfOpening, (int) cfDuration })
        if (auto* p = processor.getAPVTS().getParameter (ids::cam (cam, f)))
            p->beginChangeGesture();
}

void WaveformView::endGesture()
{
    if (dragCam >= 0)
        for (int f : { (int) cfOpening, (int) cfDuration })
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

    const double n = (double) numSamples, minLength = (double) camSpecs[cfDuration].min;
    const double end = dragStartStart + dragStartLength;
    if (dragHit == Hit::body || dragHit == Hit::flag)
    {
        // The whole range moves, its duration kept.
        const double start = juce::jlimit (0.0, juce::jmax (0.0, n - dragStartLength), dragStartStart + dx);
        setParameter (dragCam, cfOpening, (float) (start / n));
    }
    else if (dragHit == Hit::leftEdge)
    {
        // The opening moves, the end stays: the duration takes up the difference.
        const double start = juce::jlimit (0.0, juce::jmax (0.0, end - minLength), dragStartStart + dx);
        setParameter (dragCam, cfOpening, (float) (start / n));
        setParameter (dragCam, cfDuration, (float) (end - start));
    }
    else
    {
        // The end moves, the opening stays.
        const double newEnd = juce::jlimit (dragStartStart + minLength, n, end + dx);
        setParameter (dragCam, cfDuration, (float) (newEnd - dragStartStart));
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
    if (c >= 0 && (hit == Hit::body || hit == Hit::flag))
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
