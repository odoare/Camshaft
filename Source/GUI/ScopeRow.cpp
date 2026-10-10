/*
  ------------------------------------------------------------------------------
    ScopeRow.cpp

    See ScopeRow.h.

    Author: Olivier Doaré, github.com/odoare
    Dual-licensed, mirroring the JUCE framework it depends on: under the GNU
    AGPL Version 3.0, or under commercial terms available from the author.
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
  ------------------------------------------------------------------------------
*/

#include "ScopeRow.h"
#include "PluginProcessor.h"
#include "ParamIDs.h"
#include "Theme.h"
#include "Tooltips.h"

namespace camshaft
{

using namespace cam;

//==============================================================================
CamProfileView::CamProfileView (CamshaftAudioProcessor& p, int c)
    : processor (p), cam (c), colour (theme::camColour (c))
{
    set.allocate();
    wave.assign (256, 0.0f);
    setTooltip (tips::scope::profile);
    lastTime = juce::Time::getMillisecondCounterHiRes();
    startTimerHz (30);
}

void CamProfileView::rebuild()
{
    const auto source = processor.getEngine().getSource();
    if (source == nullptr || source->numSamples < 16)
        return;

    auto& apvts = processor.getAPVTS();
    auto value = [&] (int f) { return apvts.getRawParameterValue (ids::cam (cam, f))->load(); };

    fxme::WavetableBuildSettings s;
    double start, length;
    camRange (value (cfStart), value (cfLength), (double) source->numSamples, start, length);
    s.centre = start + 0.5 * length;
    s.width = length;
    s.overlap = value (cfOverlap) * 0.01f;
    s.overlapShape = (fxme::WavetableBuildSettings::OverlapShape) (int) value (cfOverlapShape);
    s.snap = (fxme::WavetableBuildSettings::Snap) (int) value (cfSnap);

    // Rebuild only when the position (on the engine's own grid) changed.
    const auto key = fxme::WavetableCache::makeKey (0, (int) s.centre, (int) s.width, s.overlap,
                                                    (int) s.overlapShape, (int) s.snap);
    if (key == shownKey && source.get() == shownSource)
        return;
    shownKey = key;
    shownSource = source.get();

    builder.build (*source, s, set);

    float peak = 1.0e-6f;
    const int step = fxme::WavetableSet::tableSize / (int) wave.size();
    for (size_t i = 0; i < wave.size(); ++i)
    {
        float v = set.table (0, 0)[(int) i * step];
        if (set.numChannels > 1)
            v = 0.5f * (v + set.table (1, 0)[(int) i * step]);
        wave[i] = v;
        peak = juce::jmax (peak, std::abs (v));
    }
    for (auto& v : wave)
        v /= peak;
    repaint();
}

void CamProfileView::timerCallback()
{
    rebuild();

    const double now = juce::Time::getMillisecondCounterHiRes();
    const double dt = (now - lastTime) * 0.001;
    lastTime = now;

    const bool active = processor.getEngine().getDisplay().active[(size_t) cam].load();
    if (active)
    {
        // A slow, steady turn: a visible stand-in for the oscillator's speed.
        rotation += (float) (dt * 0.35);
        rotation -= std::floor (rotation);
    }
    if (active || active != playing)
    {
        playing = active;
        repaint();
    }
}

void CamProfileView::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat().reduced (4.0f);
    const float d = juce::jmin (b.getWidth(), b.getHeight() - 8.0f);
    const auto c = juce::Point<float> (b.getCentreX(), b.getY() + 8.0f + d * 0.5f);
    const float base = d * 0.30f, lift = d * 0.15f;
    const bool audible = processor.getAPVTS().getRawParameterValue (ids::cam (cam, cfAudible))->load() > 0.5f;
    const auto tint = audible ? colour : colour.withMultipliedSaturation (0.3f).withMultipliedAlpha (0.6f);

    // Base circle (the shaft) and the profile.
    auto radiusAt = [&] (float turn)
    {
        float t = turn - std::floor (turn);
        const float pos = t * (float) wave.size();
        const int i = (int) pos % (int) wave.size();
        const int j = (i + 1) % (int) wave.size();
        const float f = pos - std::floor (pos);
        return base + lift * (1.0f + wave[(size_t) i] + (wave[(size_t) j] - wave[(size_t) i]) * f);
    };

    juce::Path profile;
    constexpr int points = 256;
    for (int k = 0; k <= points; ++k)
    {
        const float turn = (float) k / (float) points;
        const float angle = juce::MathConstants<float>::twoPi * (turn + rotation);
        const auto pt = c.getPointOnCircumference (radiusAt (turn), angle);
        if (k == 0) profile.startNewSubPath (pt); else profile.lineTo (pt);
    }
    profile.closeSubPath();

    g.setColour (tint.withAlpha (0.18f));
    g.fillPath (profile);
    g.setColour (tint);
    g.strokePath (profile, juce::PathStrokeType (1.6f));

    g.setColour (theme::panelLine);
    g.fillEllipse (juce::Rectangle<float> (base * 0.5f, base * 0.5f).withCentre (c));
    g.setColour (theme::knobBody.brighter (0.4f));
    g.drawEllipse (juce::Rectangle<float> (base * 0.5f, base * 0.5f).withCentre (c), 1.0f);

    // The follower: a roller riding the profile at the top.
    const float r = radiusAt (-rotation);
    const auto contact = juce::Point<float> (c.x, c.y - r);
    g.setColour (theme::text.withAlpha (0.8f));
    g.drawLine (contact.x, contact.y - 3.0f, contact.x, b.getY(), 2.0f);
    g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (contact.translated (0.0f, -3.0f)));
}

//==============================================================================
Oscilloscope::Oscilloscope (CamshaftAudioProcessor& p, int c)
    : processor (p), cam (c), colour (theme::camColour (c))
{
    left.assign (2048, 0.0f);
    right.assign (2048, 0.0f);
    setTooltip (tips::scope::scope);
    startTimerHz (30);
}

void Oscilloscope::timerCallback()
{
    const auto& tap = processor.getEngine().getDisplay().scopes[(size_t) cam];
    const auto total = tap.getTotalPushed();
    if (total == lastTotal)
        return;
    lastTotal = total;
    tap.snapshot (left.data(), right.data(), (int) left.size());

    float peak = 0.0f;
    for (auto v : left)
        peak = juce::jmax (peak, std::abs (v));
    const bool nowSilent = peak < 1.0e-5f;
    if (nowSilent && silent)
        return;
    silent = nowSilent;
    repaint();
}

void Oscilloscope::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat().reduced (2.0f);
    g.setColour (theme::plotBack);
    g.fillRoundedRectangle (b, 3.0f);
    g.setColour (theme::grid);
    g.drawHorizontalLine ((int) b.getCentreY(), b.getX(), b.getRight());

    if (silent)
        return;

    // Trigger on a rising zero crossing in the first half, show 1024 samples.
    constexpr int shown = 1024;
    const int n = (int) left.size();
    int start = n - shown;
    for (int i = n - shown - 1; i > 1; --i)
        if (left[(size_t) i - 1] < 0.0f && left[(size_t) i] >= 0.0f)
        {
            start = i;
            break;
        }

    float peak = 1.0e-3f;
    for (int i = 0; i < shown; ++i)
        peak = juce::jmax (peak, std::abs (left[(size_t) (start + i)]));
    const float scale = (b.getHeight() * 0.45f) / juce::jmax (0.25f, peak);

    juce::Path p;
    for (int i = 0; i < shown; ++i)
    {
        const float x = b.getX() + b.getWidth() * (float) i / (float) (shown - 1);
        const float y = b.getCentreY() - left[(size_t) (start + i)] * scale;
        if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
    }
    g.setColour (colour);
    g.strokePath (p, juce::PathStrokeType (1.3f));
}

//==============================================================================
ScopeRow::ScopeRow (CamshaftAudioProcessor& p, int c)
    : cam (c), profile (p, c), scope (p, c)
{
    addAndMakeVisible (profile);
    addAndMakeVisible (scope);
}

void ScopeRow::paint (juce::Graphics& g)
{
    theme::paintPanel (g, getLocalBounds().toFloat(), theme::camColour (cam));
}

void ScopeRow::resized()
{
    auto r = getLocalBounds().reduced (4);
    profile.setBounds (r.removeFromLeft (juce::jmin (r.getHeight(), r.getWidth() / 2)));
    r.removeFromLeft (4);
    scope.setBounds (r);
}

} // namespace camshaft
