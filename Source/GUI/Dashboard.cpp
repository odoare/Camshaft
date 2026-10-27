/*
  ------------------------------------------------------------------------------
    Dashboard.cpp

    See Dashboard.h.

    Author: Olivier Doaré, github.com/odoare
    Dual-licensed, mirroring the JUCE framework it depends on: under the GNU
    AGPL Version 3.0, or under commercial terms available from the author.
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
  ------------------------------------------------------------------------------
*/

#include "Dashboard.h"
#include "PluginProcessor.h"
#include "ParamIDs.h"
#include "Theme.h"
#include "Tooltips.h"

namespace camshaft
{

namespace
{
    const juce::Colour amber { 0xffffb020 };
    const juce::Colour green { 0xff5fd16a };
    const juce::Colour blue  { 0xff5aa0e0 };
}

//==============================================================================
WarningLight::WarningLight (const juce::String& l, juce::Colour c) : label (l), colour (c) {}

void WarningLight::setOn (bool on)
{
    if (on != lit)
    {
        lit = on;
        repaint();
    }
}

void WarningLight::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    const auto text = b.removeFromBottom (11.0f);
    const float d = juce::jmin (b.getWidth(), b.getHeight()) - 2.0f;
    const auto lamp = juce::Rectangle<float> (d * 1.5f, d * 0.75f).withCentre (b.getCentre());

    if (lit)
    {
        // A glow, then the lit lens.
        g.setColour (colour.withAlpha (0.25f));
        g.fillRoundedRectangle (lamp.expanded (3.0f), 5.0f);
        g.setColour (colour);
    }
    else
        g.setColour (colour.withMultipliedBrightness (0.25f).withAlpha (0.8f));
    g.fillRoundedRectangle (lamp, 3.0f);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.drawRoundedRectangle (lamp, 3.0f, 1.0f);

    g.setFont (theme::labelFont (9.0f));
    g.setColour (lit ? colour.brighter (0.3f) : theme::engraved);
    g.drawText (label, text, juce::Justification::centred, false);
}

//==============================================================================
BarGauge::BarGauge (const juce::String& l, juce::Colour c, float warnAt, bool warnBelow)
    : label (l), colour (c), warn (warnAt), below (warnBelow) {}

void BarGauge::setValue (float v)
{
    v = juce::jlimit (0.0f, 1.0f, v);
    if (std::abs (v - value) > 0.004f)
    {
        value = v;
        repaint();
    }
}

void BarGauge::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setFont (theme::labelFont (9.0f));
    g.setColour (theme::engraved);
    g.drawText (label, b.removeFromLeft (34.0f), juce::Justification::centredLeft, false);

    const auto bar = b.withSizeKeepingCentre (b.getWidth(), juce::jmin (8.0f, b.getHeight()));
    g.setColour (theme::plotBack);
    g.fillRoundedRectangle (bar, 2.0f);

    // Segmented, like an LCD gauge.
    constexpr int segments = 12;
    const float sw = bar.getWidth() / (float) segments;
    const bool alarm = below ? value < warn : value > warn;
    for (int i = 0; i < segments; ++i)
    {
        const float at = ((float) i + 0.5f) / (float) segments;
        if (at > value)
            break;
        const bool hot = below ? at < warn : at > warn;
        g.setColour (hot || alarm ? theme::redline : colour);
        g.fillRect (juce::Rectangle<float> (bar.getX() + (float) i * sw + 1.0f, bar.getY() + 1.0f, sw - 2.0f, bar.getHeight() - 2.0f));
    }
}

//==============================================================================
GearSelector::GearSelector (CamshaftAudioProcessor& p)
    : attachment (*p.getAPVTS().getParameter (ids::polyMode),
                  [this] (float v) { gear = juce::jlimit (0, 2, juce::roundToInt (v)); repaint(); })
{
    setTooltip (tips::dash::gears);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    attachment.sendInitialUpdate();
}

int GearSelector::gearAt (juce::Point<float> p) const
{
    return juce::jlimit (0, 2, (int) (p.y / ((float) getHeight() / 3.0f)));
}

void GearSelector::mouseDown (const juce::MouseEvent& e)
{
    attachment.setValueAsCompleteGesture ((float) gearAt (e.position));
}

void GearSelector::paint (juce::Graphics& g)
{
    // A shift gate: three letters stacked, the engaged one lit.
    auto b = getLocalBounds().toFloat();
    g.setColour (theme::plotBack);
    g.fillRoundedRectangle (b, 4.0f);
    g.setColour (theme::panelLine);
    g.drawRoundedRectangle (b.reduced (0.5f), 4.0f, 1.0f);

    const char* letters[] = { "P", "M", "L" };
    const float h = b.getHeight() / 3.0f;
    g.setFont (theme::labelFont (juce::jmin (13.0f, h * 0.8f)));
    for (int i = 0; i < 3; ++i)
    {
        const auto cell = juce::Rectangle<float> (b.getX(), b.getY() + (float) i * h, b.getWidth(), h);
        if (i == gear)
        {
            g.setColour (theme::accent.withAlpha (0.25f));
            g.fillRoundedRectangle (cell.reduced (2.0f, 1.0f), 3.0f);
        }
        g.setColour (i == gear ? theme::accent : theme::engraved);
        g.drawText (letters[i], cell, juce::Justification::centred, false);
    }
}

//==============================================================================
ThrottlePedal::ThrottlePedal (CamshaftAudioProcessor& p)
    : param (*p.getAPVTS().getParameter (ids::masterOut)),
      attachment (param, [this] (float v) { value = v; repaint(); })
{
    setTooltip (tips::dash::throttle);
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    attachment.sendInitialUpdate();
}

void ThrottlePedal::mouseDown (const juce::MouseEvent&)
{
    dragStartNorm = param.convertTo0to1 (value);
    attachment.beginGesture();
}

void ThrottlePedal::mouseDrag (const juce::MouseEvent& e)
{
    // Down presses the pedal: the full travel over the pedal's own height.
    const float travel = (float) e.getDistanceFromDragStartY() / juce::jmax (40.0f, (float) getHeight() * 0.8f);
    const float norm = juce::jlimit (0.0f, 1.0f, dragStartNorm + travel * (e.mods.isShiftDown() ? 0.2f : 1.0f));
    attachment.setValueAsPartOfGesture (param.convertFrom0to1 (norm));
}

void ThrottlePedal::mouseUp (const juce::MouseEvent&)
{
    attachment.endGesture();
}

void ThrottlePedal::mouseDoubleClick (const juce::MouseEvent&)
{
    attachment.setValueAsCompleteGesture (param.convertFrom0to1 (param.getDefaultValue()));
}

void ThrottlePedal::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    const float norm = juce::jlimit (0.0f, 1.0f, param.convertTo0to1 (value) + wheel.deltaY * 0.05f);
    attachment.setValueAsCompleteGesture (param.convertFrom0to1 (norm));
}

void ThrottlePedal::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    const auto text = b.removeFromBottom (14.0f);
    const auto caption = b.removeFromTop (12.0f);
    const float press = param.convertTo0to1 (value);   // 0 released, 1 floored

    g.setFont (theme::labelFont (8.5f));
    g.setColour (theme::engraved);
    g.drawText ("THROTTLE", caption, juce::Justification::centred, false);

    // The pivot at the bottom, the arm up to the pad.
    const float w = juce::jmin (b.getWidth() * 0.8f, b.getHeight() * 0.55f);
    const float cx = b.getCentreX();
    const float pivotY = b.getBottom() - 4.0f;
    g.setColour (juce::Colour (0xff3a3c42));
    g.fillRoundedRectangle (juce::Rectangle<float> (w * 0.7f, 6.0f).withCentre ({ cx, pivotY }), 2.0f);

    // The pad: a trapezoid seen from the driver's seat; pressing it tips its
    // top away (shorter, narrower at the top) and lowers it.
    const float top = b.getY() + 4.0f + press * b.getHeight() * 0.22f;
    const float bottom = pivotY - 8.0f;
    const float topW = w * (1.0f - 0.25f * press), botW = w * 0.82f;
    juce::Path pad;
    pad.startNewSubPath (cx - topW * 0.5f, top);
    pad.lineTo (cx + topW * 0.5f, top);
    pad.lineTo (cx + botW * 0.5f, bottom);
    pad.lineTo (cx - botW * 0.5f, bottom);
    pad.closeSubPath();
    const auto rubber = juce::Colour (0xff26272c).interpolatedWith (theme::accent, 0.10f + 0.25f * press);
    g.setColour (rubber);
    g.fillPath (pad, juce::AffineTransform());
    g.setColour (theme::accent.withAlpha (0.35f + 0.5f * press));
    g.strokePath (pad, juce::PathStrokeType (1.2f, juce::PathStrokeType::curved));

    // Rubber ribs across it.
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    constexpr int ribs = 7;
    for (int i = 1; i <= ribs; ++i)
    {
        const float t = (float) i / (float) (ribs + 1);
        const float y = top + (bottom - top) * t;
        const float half = 0.5f * (topW + (botW - topW) * t) - 4.0f;
        g.drawLine (cx - half, y, cx + half, y, 1.6f);
    }

    g.setFont (theme::gaugeFont (9.0f));
    g.setColour (value > 0.05f ? theme::redline : theme::text);
    g.drawText (juce::String (value, 1) + " dB", text, juce::Justification::centred, false);
}

//==============================================================================
void HazardButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    auto b = getLocalBounds().toFloat().reduced (1.0f);
    const float d = juce::jmin (b.getWidth(), b.getHeight());
    b = b.withSizeKeepingCentre (d, d);

    g.setColour (down ? theme::redline.darker (0.4f) : (over ? theme::redline.brighter (0.15f) : theme::redline.darker (0.15f)));
    g.fillRoundedRectangle (b, d * 0.18f);
    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.drawRoundedRectangle (b, d * 0.18f, 1.0f);

    // The warning triangle, outlined twice as on a car's switch.
    auto tri = [&] (float scale)
    {
        juce::Path p;
        const auto c = b.getCentre().translated (0.0f, d * 0.04f);
        const float s = d * scale;
        p.addTriangle (c.x, c.y - s * 0.55f, c.x - s * 0.62f, c.y + s * 0.42f, c.x + s * 0.62f, c.y + s * 0.42f);
        return p;
    };
    g.setColour (juce::Colours::white.withAlpha (0.92f));
    g.strokePath (tri (0.62f), juce::PathStrokeType (d * 0.07f, juce::PathStrokeType::curved));
    g.strokePath (tri (0.30f), juce::PathStrokeType (d * 0.05f, juce::PathStrokeType::curved));
}

//==============================================================================
void ExpandButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    auto b = getLocalBounds().toFloat().reduced (2.0f);
    const float d = juce::jmin (b.getWidth(), b.getHeight());
    b = b.withSizeKeepingCentre (d, d);

    g.setColour (down ? theme::accent : (over ? theme::text : theme::dimText));
    const float arm = d * 0.38f;
    juce::Path p;
    // One bracket per corner; its tip at the corner (expand) or towards the
    // centre (back).
    for (int cx = 0; cx < 2; ++cx)
        for (int cy = 0; cy < 2; ++cy)
        {
            const float sx = cx == 0 ? 1.0f : -1.0f, sy = cy == 0 ? 1.0f : -1.0f;
            juce::Point<float> tip (cx == 0 ? b.getX() : b.getRight(), cy == 0 ? b.getY() : b.getBottom());
            float dir = 1.0f;
            if (expanded)
            {
                tip += { sx * arm, sy * arm };
                dir = -1.0f;
            }
            p.startNewSubPath (tip.x + dir * sx * arm, tip.y);
            p.lineTo (tip);
            p.lineTo (tip.x, tip.y + dir * sy * arm);
        }
    g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::mitered, juce::PathStrokeType::square));
}

//==============================================================================
Dashboard::Dashboard (CamshaftAudioProcessor& p)
    : processor (p),
      tach (p),
      spectrum (p.getSpectrumTap(), [&p] { return p.getSampleRate(); }),
      scope (p.getOutputTap(), 1024),
      rev ("REV", theme::redline), check ("CHECK", amber), steal ("STEAL", amber), midi ("MIDI", green),
      hold ("HOLD", green), sync ("SYNC", green), turbo1 ("T1", theme::auxColour (0)), turbo2 ("T2", theme::auxColour (1)),
      fuel ("FUEL", green, 0.15f, true), temp ("TEMP", blue, 0.75f, false),
      gears (p),
      limiterSwitch (p.getAPVTS(), ids::limiter, theme::accent),
      throttle (p)
{
    fxme::CircularSpectrum::Colours sc;
    sc.face = theme::plotBack;
    sc.rim = theme::panelLine;
    sc.ticks = theme::dimText;
    sc.barLow = theme::camColour (1);
    sc.barHigh = theme::accent;
    sc.peak = theme::text;
    sc.caption = theme::engraved;
    spectrum.setColours (sc);
    spectrum.setCaption ("SPECTRUM");
    spectrum.setTooltip (tips::dash::spectrum);

    fxme::Vectorscope::Colours vc;
    vc.trace = theme::accent;
    vc.grid = theme::grid;
    vc.background = theme::plotBack;
    scope.setColours (vc);
    scope.setTooltip (tips::master::scope);

    rev.setTooltip (tips::dash::rev);
    check.setTooltip (tips::dash::check);
    steal.setTooltip (tips::dash::steal);
    midi.setTooltip (tips::dash::midi);
    hold.setTooltip (tips::dash::hold);
    sync.setTooltip (tips::dash::sync);
    turbo1.setTooltip (tips::dash::turbo);
    turbo2.setTooltip (tips::dash::turbo);
    fuel.setTooltip (tips::dash::fuel);
    temp.setTooltip (tips::dash::temp);
    limiterSwitch.button.setButtonText ("Rev lim");
    limiterSwitch.button.setTooltip (tips::dash::limiter);
    hazard.setTooltip (tips::dash::hazard);
    hazard.onClick = [this] { processor.requestPanic(); };
    expand.setTooltip (tips::dash::expand);
    expand.onClick = [this] { if (onToggleFullView) onToggleFullView(); };

    for (auto* c : std::initializer_list<juce::Component*> { &tach, &spectrum, &scope, &rev, &check, &steal, &midi, &hold,
                                                              &sync, &turbo1, &turbo2, &fuel, &temp, &gears,
                                                              &limiterSwitch, &hazard, &throttle, &expand })
        addAndMakeVisible (c);

    startTimerHz (30);
}

void Dashboard::timerCallback()
{
    if (! isShowing())
        return;

    const auto& d = processor.getEngine().getDisplay();
    const double now = juce::Time::getMillisecondCounterHiRes();

    const int steals = d.steals.load (std::memory_order_relaxed);
    if (steals != lastSteals)
    {
        lastSteals = steals;
        stealUntil = now + 500.0;
    }
    const int notes = d.noteOns.load (std::memory_order_relaxed);
    if (notes != lastNotes)
    {
        lastNotes = notes;
        midiUntil = now + 120.0;
    }
    const double load = processor.getDspLoad();
    if (load > 0.9)
        checkUntil = now + 1000.0;

    rev.setOn (processor.getLimiterReductionDb() > 0.1f);
    check.setOn (now < checkUntil);
    steal.setOn (now < stealUntil);
    midi.setOn (now < midiUntil);
    hold.setOn (d.sustainDown.load (std::memory_order_relaxed));
    sync.setOn (processor.isHostPlaying());
    turbo1.setOn (processor.getAuxPeak (0) > 0.001f);
    turbo2.setOn (processor.getAuxPeak (1) > 0.001f);

    // Mono and legato play one voice.
    const bool poly = processor.getAPVTS().getRawParameterValue (ids::polyMode)->load() < 0.5f;
    const int available = poly ? juce::jmax (1, d.polyphony.load (std::memory_order_relaxed)) : 1;
    const int playing = d.voicesActive.load (std::memory_order_relaxed);
    fuel.setValue (1.0f - (float) juce::jmin (playing, available) / (float) available);
    temp.setValue ((float) load);
}

void Dashboard::paint (juce::Graphics& g)
{
    // The cluster's hood: a dark recessed panel, the dials in bezels.
    const auto b = getLocalBounds().toFloat();
    juce::ColourGradient hood (theme::panelHi, b.getX(), b.getY(), theme::plotBack, b.getX(), b.getBottom(), false);
    g.setGradientFill (hood);
    g.fillRoundedRectangle (b, 8.0f);
    g.setColour (theme::panelLine);
    g.drawRoundedRectangle (b.reduced (0.5f), 8.0f, 1.0f);

    for (const auto& bz : bezels)
    {
        if (bz.isEmpty())
            continue;
        juce::ColourGradient ring (juce::Colour (0xff5a5d66), bz.getX(), bz.getY(), juce::Colour (0xff1a1b1f), bz.getX(), bz.getBottom(), false);
        g.setGradientFill (ring);
        g.fillEllipse (bz);
        g.setColour (theme::plotBack);
        g.fillEllipse (bz.reduced (3.0f));
    }
}

void Dashboard::resized()
{
    auto area = getLocalBounds().reduced (8, 6);

    // Bottom strip: gauges, lights, controls.
    auto strip = area.removeFromBottom (40);
    area.removeFromBottom (4);

    auto gauges = strip.removeFromLeft (juce::jmin (150, strip.getWidth() / 4));
    fuel.setBounds (gauges.removeFromTop (gauges.getHeight() / 2).reduced (0, 2));
    temp.setBounds (gauges.reduced (0, 2));

    auto controls = strip.removeFromRight (juce::jmin (160, strip.getWidth() / 3));
    hazard.setBounds (controls.removeFromRight (40).reduced (2));
    controls.removeFromRight (4);
    gears.setBounds (controls.removeFromRight (26));
    controls.removeFromRight (4);
    limiterSwitch.setBounds (controls.withSizeKeepingCentre (controls.getWidth(), 24));

    strip.reduce (8, 0);
    WarningLight* lights[] = { &rev, &check, &steal, &midi, &hold, &sync, &turbo1, &turbo2 };
    const int lw = strip.getWidth() / 8;
    for (auto* l : lights)
        l->setBounds (strip.removeFromLeft (lw).reduced (2, 2));

    // The pedal on the right, then the dials: the spectrum in the middle,
    // larger, as a speedometer.
    // The full-view button sits in the top-left corner (clear of the round
    // bezels, which leave it the corner).
    expand.setBounds (area.getX(), area.getY(), 20, 20);
    // The pedal keeps its proportions as the dashboard grows: its width
    // follows its height (70 px wide at the usual height).
    throttle.setBounds (area.removeFromRight (juce::jmin (area.getWidth() / 5,
                                                          juce::jmax (70, juce::roundToInt ((float) area.getHeight() * 0.4f)))));
    area.removeFromRight (6);
    const float h = (float) area.getHeight();
    const float big = juce::jmin (h, (float) area.getWidth() * 0.40f);
    const float small = juce::jmin (big * 0.78f, ((float) area.getWidth() - big) * 0.5f - 6.0f);
    const auto centre = area.toFloat().getCentre();
    bezels[1] = juce::Rectangle<float> (big, big).withCentre (centre);
    bezels[0] = juce::Rectangle<float> (small, small).withCentre ({ bezels[1].getX() - 6.0f - small * 0.5f, centre.y + (big - small) * 0.25f });
    bezels[2] = juce::Rectangle<float> (small, small).withCentre ({ bezels[1].getRight() + 6.0f + small * 0.5f, centre.y + (big - small) * 0.25f });

    spectrum.setBounds (bezels[1].reduced (4.0f).toNearestInt());
    tach.setBounds (bezels[0].reduced (small * 0.10f).toNearestInt());
    // The vectorscope is square: inscribed in its round bezel.
    scope.setBounds (bezels[2].withSizeKeepingCentre (small * 0.66f, small * 0.66f).toNearestInt());
}

//==============================================================================
OutputMeter::OutputMeter (CamshaftAudioProcessor& p) : processor (p)
{
    fxme::StereoLevelMeter::Colours mc;
    mc.background = theme::plotBack;
    mc.outline = theme::panelLine;
    mc.bar = theme::accent.withAlpha (0.85f);
    mc.peak = theme::text;
    mc.over = theme::redline;
    meter.setColours (mc);
    meter.setHorizontal (true);
    meter.setRange (-48.0f, 3.0f);
    meter.setTooltip (tips::master::meter);
    addAndMakeVisible (meter);
    startTimerHz (30);
}

void OutputMeter::timerCallback()
{
    for (int ch = 0; ch < 2; ++ch)
        meter.update (ch, processor.getOutputRmsDb (ch), processor.getOutputBlockPeakDb (ch));
}

void OutputMeter::resized()
{
    auto b = getLocalBounds();
    b.removeFromLeft (12);   // the L / R letters
    meter.setBounds (b.withSizeKeepingCentre (b.getWidth(), juce::jmin (20, b.getHeight())));
}

void OutputMeter::paint (juce::Graphics& g)
{
    const auto m = meter.getBounds();
    g.setFont (theme::labelFont (8.5f));
    g.setColour (theme::engraved);
    g.drawText ("L", juce::Rectangle<int> (0, m.getY(), 10, m.getHeight() / 2), juce::Justification::centred, false);
    g.drawText ("R", juce::Rectangle<int> (0, m.getCentreY(), 10, m.getHeight() / 2), juce::Justification::centred, false);
}

} // namespace camshaft
