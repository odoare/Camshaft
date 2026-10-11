/*
  ------------------------------------------------------------------------------
    TimingBeltView.cpp

    See TimingBeltView.h.

    Author: Olivier Doaré, github.com/odoare
    Dual-licensed, mirroring the JUCE framework it depends on: under the GNU
    AGPL Version 3.0, or under commercial terms available from the author.
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
  ------------------------------------------------------------------------------
*/

#include "TimingBeltView.h"
#include "PluginProcessor.h"
#include "ParamIDs.h"
#include "Theme.h"
#include "Tooltips.h"

namespace camshaft
{

namespace
{
    const juce::Colour beltGround { 0xff1c1d21 };
    const juce::Colour pulleyFace { 0xff2a2c31 };
}

using namespace cam;

namespace
{
    const char* const typeNames[] = { "", "FM", "RING", "AM" };
}

TimingBeltView::TimingBeltView (CamshaftAudioProcessor& p) : processor (p)
{
    for (int s = 0; s < numCams; ++s)
        for (int d = 0; d < numCams; ++d)
        {
            auto k = std::make_unique<CamKnob> (processor, ids::cam (d, cfLift1 + s), "Lift", theme::camColour (s));
            k->setShowLabel (false);
            k->setTooltip (tips::belt::lift);
            addChildComponent (*k);
            liftKnobs[(size_t) (s * numCams + d)] = std::move (k);
        }

    processor.getStructure().addChangeListener (this);
    setTooltip (tips::belt::view);
    lastTime = juce::Time::getMillisecondCounterHiRes();
    updateKnobs();
    startTimerHz (30);
}

TimingBeltView::~TimingBeltView()
{
    processor.getStructure().removeChangeListener (this);
}

void TimingBeltView::changeListenerCallback (juce::ChangeBroadcaster*)
{
    updateKnobs();
    repaint();
}

void TimingBeltView::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const float dt = (float) ((now - lastTime) * 0.001);
    lastTime = now;

    bool moving = false;
    const auto& display = processor.getEngine().getDisplay();
    for (int c = 0; c < numCams; ++c)
        if (display.active[(size_t) c].load())
        {
            angle[(size_t) c] += dt * 0.35f * juce::MathConstants<float>::twoPi;   // as the cam profiles turn
            moving = true;
        }
    if (refusedFrames > 0 && --refusedFrames == 0)
        moving = true;
    if (moving)
        repaint();
}

//==============================================================================
float TimingBeltView::pulleyRadius() const
{
    return juce::jmin ((float) getWidth() * 0.12f, (float) getHeight() * 0.17f);
}

juce::Point<float> TimingBeltView::pulleyCentre (int cam) const
{
    const auto b = getLocalBounds().toFloat().reduced (pulleyRadius() * 1.3f);
    const float x = (cam % 2 == 0) ? b.getX() + b.getWidth() * 0.15f : b.getRight() - b.getWidth() * 0.15f;
    const float y = (cam < 2) ? b.getY() : b.getBottom();
    return { x, y };
}

int TimingBeltView::pulleyAt (juce::Point<float> p) const
{
    for (int c = 0; c < numCams; ++c)
        if (p.getDistanceFrom (pulleyCentre (c)) < pulleyRadius())
            return c;
    return -1;
}

juce::Point<float> TimingBeltView::badgeCentre (int src, int dst) const
{
    if (src == dst)
    {
        const auto c = pulleyCentre (src);
        const float r = pulleyRadius();
        const float side = (src % 2 == 0) ? -1.0f : 1.0f;
        return { c.x + side * r * 1.35f, c.y - r * 0.9f };
    }
    // Two-thirds of the way along, so belts crossing in the middle stay apart.
    return pulleyCentre (src) + (pulleyCentre (dst) - pulleyCentre (src)) * 0.62f;
}

std::pair<int, int> TimingBeltView::badgeAt (juce::Point<float> p) const
{
    auto& structure = processor.getStructure();
    for (int s = 0; s < numCams; ++s)
        for (int d = 0; d < numCams; ++d)
            if (structure.getXmod (s, d) != 0 && p.getDistanceFrom (badgeCentre (s, d)) < 14.0f)
                return { s, d };
    return { -1, -1 };
}

void TimingBeltView::updateKnobs()
{
    auto& structure = processor.getStructure();
    for (int s = 0; s < numCams; ++s)
        for (int d = 0; d < numCams; ++d)
        {
            auto& k = *liftKnobs[(size_t) (s * numCams + d)];
            const bool on = structure.getXmod (s, d) != 0;
            k.setVisible (on);
            if (on)
                k.setBounds (juce::Rectangle<int> (52, 56).withCentre ((badgeCentre (s, d) + juce::Point<float> (0.0f, 38.0f)).toInt()));
        }
}

void TimingBeltView::resized()
{
    updateKnobs();
}

//==============================================================================
void TimingBeltView::paint (juce::Graphics& g)
{
    // Dark grey ground, the pulleys a shade lighter.
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (beltGround);
    g.fillRoundedRectangle (bounds, 5.0f);
    g.setColour (theme::panelLine);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 5.0f, 1.0f);
    theme::paintEngraved (g, "Timing belt", getLocalBounds().reduced (10, 6), juce::Justification::topLeft);

    auto& structure = processor.getStructure();
    auto& apvts = processor.getAPVTS();
    const float r = pulleyRadius();

    // Belts.
    for (int s = 0; s < numCams; ++s)
        for (int d = 0; d < numCams; ++d)
        {
            const int type = structure.getXmod (s, d);
            if (type == 0)
                continue;
            const auto colour = theme::camColour (s);
            g.setColour (colour.withAlpha (0.8f));
            if (s == d)
            {
                const auto c = pulleyCentre (s);
                juce::Path loop;
                loop.addCentredArc (c.x, c.y - r * 0.5f, r * 1.1f, r * 0.9f, 0.0f, -2.4f, 2.4f, true);
                g.strokePath (loop, juce::PathStrokeType (3.0f));
            }
            else
            {
                const auto a = pulleyCentre (s), b = pulleyCentre (d);
                const auto dir = (b - a) / juce::jmax (1.0f, a.getDistanceFrom (b));
                const auto normal = juce::Point<float> (-dir.y, dir.x) * (r * 0.75f);
                // Two strands, like a real belt, tangent-ish to both pulleys.
                g.drawLine ({ a + normal, b + normal * 0.55f }, 3.0f);
                g.drawLine ({ a - normal, b - normal * 0.55f }, 3.0f);
                juce::Path arrow;
                const auto tip = b - dir * (r * 1.05f);
                arrow.addTriangle (tip, tip - dir * 10.0f + normal * 0.25f, tip - dir * 10.0f - normal * 0.25f);
                g.fillPath (arrow);
            }

            // Badge with the connection type.
            const auto bc = badgeCentre (s, d);
            const auto badge = juce::Rectangle<float> (34.0f, 16.0f).withCentre (bc);
            g.setColour (theme::background);
            g.fillRoundedRectangle (badge, 4.0f);
            g.setColour (colour);
            g.drawRoundedRectangle (badge, 4.0f, 1.2f);
            g.setFont (theme::labelFont (10.0f));
            g.drawText (typeNames[type], badge, juce::Justification::centred);
        }

    // A refused connection, for a moment.
    if (refusedFrames > 0 && refused.first >= 0)
    {
        g.setColour (theme::redline.withAlpha ((float) refusedFrames / 20.0f));
        g.drawLine ({ pulleyCentre (refused.first), pulleyCentre (refused.second) }, 3.0f);
    }

    // The belt being dragged.
    if (dragFrom >= 0)
    {
        g.setColour (theme::camColour (dragFrom).withAlpha (0.6f));
        const float dashes[] = { 6.0f, 4.0f };
        g.drawDashedLine ({ pulleyCentre (dragFrom), dragPoint }, dashes, 2, 2.0f);
    }

    // Pulleys.
    for (int c = 0; c < numCams; ++c)
    {
        const auto centre = pulleyCentre (c);
        const auto colour = theme::camColour (c);
        const bool audible = apvts.getRawParameterValue (ids::cam (c, cfAudible))->load() > 0.5f;

        g.setColour (pulleyFace);
        g.fillEllipse (juce::Rectangle<float> (2.0f * r, 2.0f * r).withCentre (centre));

        // Teeth around the rim.
        juce::Path teeth;
        constexpr int numTeeth = 18;
        for (int t = 0; t < numTeeth; ++t)
        {
            const float a = angle[(size_t) c] + juce::MathConstants<float>::twoPi * (float) t / (float) numTeeth;
            teeth.addLineSegment ({ centre.getPointOnCircumference (r * 0.86f, a), centre.getPointOnCircumference (r, a) }, 2.0f);
        }
        g.setColour (colour.withAlpha (audible ? 0.9f : 0.4f));
        g.fillPath (teeth);
        if (audible)
            g.drawEllipse (juce::Rectangle<float> (2.0f * r, 2.0f * r).withCentre (centre), 1.5f);
        else
        {
            juce::Path rim;
            rim.addEllipse (juce::Rectangle<float> (2.0f * r, 2.0f * r).withCentre (centre));
            juce::Path dashed;
            const float dashes[] = { 4.0f, 4.0f };
            juce::PathStrokeType (1.5f).createDashedStroke (dashed, rim, dashes, 2);
            g.fillPath (dashed);
        }

        g.setColour (theme::text);
        g.setFont (theme::labelFont (12.0f));
        g.drawText ("CAM " + juce::String (c + 1), juce::Rectangle<float> (2.0f * r, 16.0f).withCentre (centre),
                    juce::Justification::centred);
    }
}

//==============================================================================
void TimingBeltView::mouseMove (const juce::MouseEvent& e)
{
    const bool overSomething = pulleyAt (e.position) >= 0 || badgeAt (e.position).first >= 0;
    setMouseCursor (overSomething ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void TimingBeltView::mouseDown (const juce::MouseEvent& e)
{
    const auto [s, d] = badgeAt (e.position);
    if (s >= 0)
    {
        // Cycle FM -> Ring -> AM -> removed (feedback can only be FM).
        auto& structure = processor.getStructure();
        const int type = structure.getXmod (s, d);
        const int next = (s == d) ? 0 : (type + 1) % 4;
        structure.setXmod (s, d, next);
        return;
    }
    dragFrom = pulleyAt (e.position);
    dragPoint = e.position;
}

void TimingBeltView::mouseDrag (const juce::MouseEvent& e)
{
    if (dragFrom >= 0)
    {
        dragPoint = e.position;
        repaint();
    }
}

void TimingBeltView::mouseUp (const juce::MouseEvent& e)
{
    if (dragFrom < 0)
        return;
    const int to = pulleyAt (e.position);
    if (to >= 0 && to != dragFrom)
    {
        auto& structure = processor.getStructure();
        if (structure.getXmod (dragFrom, to) == 0 && ! structure.setXmod (dragFrom, to, 1))
        {
            refused = { dragFrom, to };
            refusedFrames = 20;
        }
    }
    dragFrom = -1;
    repaint();
}

void TimingBeltView::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int c = pulleyAt (e.position);
    if (c < 0)
        return;
    auto& structure = processor.getStructure();
    structure.setXmod (c, c, structure.getXmod (c, c) == 0 ? 1 : 0);
}

} // namespace camshaft
