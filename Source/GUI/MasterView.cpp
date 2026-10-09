/*
  ------------------------------------------------------------------------------
    MasterView.cpp

    See MasterView.h.

    Author: Olivier Doaré, github.com/odoare
    Dual-licensed, mirroring the JUCE framework it depends on: under the GNU
    AGPL Version 3.0, or under commercial terms available from the author.
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
  ------------------------------------------------------------------------------
*/

#include "MasterView.h"
#include "PluginProcessor.h"
#include "Theme.h"
#include "Tooltips.h"

namespace camshaft
{

Tachometer::Tachometer (CamshaftAudioProcessor& p) : processor (p)
{
    setTooltip (tips::master::tach);
    startTimerHz (30);
}

void Tachometer::timerCallback()
{
    const float target = juce::jlimit (minDb, maxDb, processor.getOutputPeakDb());
    // Needle ballistics: quick up, slower down, like a real gauge.
    needleDb += (target - needleDb) * (target > needleDb ? 0.6f : 0.15f);
    const float red = processor.getLimiterReductionDb();
    if (std::abs (needleDb - target) > 0.05f || std::abs (red - reductionDb) > 0.01f)
    {
        reductionDb = red;
        repaint();
    }
}

void Tachometer::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat().reduced (2.0f);
    const float r = juce::jmin (b.getWidth() * 0.5f, b.getHeight() * 0.62f);
    const auto c = juce::Point<float> (b.getCentreX(), b.getY() + r + 1.0f);
    const float a0 = -2.1f, a1 = 2.1f;   // radians from straight up
    auto angleFor = [&] (float db) { return a0 + (a1 - a0) * (db - minDb) / (maxDb - minDb); };

    // Dial face.
    g.setColour (theme::plotBack);
    g.fillEllipse (juce::Rectangle<float> (2.0f * r, 2.0f * r).withCentre (c));

    // Scale and redline.
    juce::Path scale, red;
    scale.addCentredArc (c.x, c.y, r * 0.86f, r * 0.86f, 0.0f, a0, angleFor (-0.3f), true);
    red.addCentredArc (c.x, c.y, r * 0.86f, r * 0.86f, 0.0f, angleFor (-0.3f), a1, true);
    g.setColour (theme::dimText);
    g.strokePath (scale, juce::PathStrokeType (1.5f));
    g.setColour (theme::redline);
    g.strokePath (red, juce::PathStrokeType (3.0f));

    for (float db : { -48.0f, -36.0f, -24.0f, -12.0f, -6.0f, 0.0f })
    {
        const float a = angleFor (db);
        g.setColour (db >= -0.3f ? theme::redline : theme::dimText);
        g.drawLine ({ c.getPointOnCircumference (r * 0.74f, a), c.getPointOnCircumference (r * 0.86f, a) }, 1.2f);
    }

    // Needle.
    const float a = angleFor (needleDb);
    g.setColour (theme::accent);
    g.drawLine ({ c, c.getPointOnCircumference (r * 0.8f, a) }, 2.0f);
    g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre (c));

    // Rev limiter warning light.
    const auto light = juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ c.x, c.y + r * 0.42f });
    g.setColour (reductionDb > 0.1f ? theme::redline : theme::redline.withAlpha (0.15f));
    g.fillEllipse (light);
}

//==============================================================================
MasterView::MasterView (CamshaftAudioProcessor& p)
    : tach (p), scope (p.getOutputTap(), 1024)
{
    fxme::Vectorscope::Colours colours;
    colours.trace = theme::accent;
    colours.grid = theme::grid;
    colours.background = theme::plotBack;
    scope.setColours (colours);
    scope.setTooltip (tips::master::scope);
    addAndMakeVisible (tach);
    addAndMakeVisible (scope);
}

void MasterView::resized()
{
    auto b = getLocalBounds();
    const int side = juce::jmin (b.getHeight(), b.getWidth() / 2);
    scope.setBounds (b.removeFromRight (side));
    tach.setBounds (b);
}

} // namespace camshaft
