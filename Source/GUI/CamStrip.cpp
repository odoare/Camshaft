/*
  ------------------------------------------------------------------------------
    CamStrip.cpp

    See CamStrip.h.

    Author: Olivier Doaré, github.com/odoare
    Dual-licensed, mirroring the JUCE framework it depends on: under the GNU
    AGPL Version 3.0, or under commercial terms available from the author.
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
  ------------------------------------------------------------------------------
*/

#include "CamStrip.h"
#include "PluginProcessor.h"
#include "ParamIDs.h"
#include "Theme.h"
#include "Tooltips.h"

namespace camshaft
{

using namespace cam;

//==============================================================================
void ControlPage::resized()
{
    auto area = getLocalBounds();
    int fixed = 0;
    for (const auto& r : rows)
        fixed += r.height;
    const int spare = juce::jmax (0, area.getHeight() - fixed);
    const int gap = rows.empty() ? 0 : spare / (int) (rows.size() + 1);

    area.removeFromTop (gap);
    for (const auto& r : rows)
    {
        auto line = area.removeFromTop (r.height);
        area.removeFromTop (gap);
        if (r.items.empty())
            continue;
        const int w = line.getWidth() / (int) r.items.size();
        for (auto* item : r.items)
            if (item != nullptr)
                item->setBounds (line.removeFromLeft (w).reduced (2, 1));
            else
                line.removeFromLeft (w);
    }
}

juce::ComboBox& ControlPage::combo (juce::AudioProcessorValueTreeState& apvts, const juce::String& id,
                                    juce::Colour accent, const char* tooltip)
{
    auto& box = make<juce::ComboBox>();
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (id)))
        box.addItemList (p->choices, 1);
    theme::accentCombo (box, accent);
    box.setTooltip (tooltip);
    attachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (apvts, id, box));
    return box;
}

//==============================================================================
CamStrip::CamStrip (CamshaftAudioProcessor& p, int c)
    : processor (p), cam (c), colour (theme::camColour (c)),
      audible (p.getAPVTS(), ids::cam (c, cfAudible), "Audible", theme::camColour (c))
{
    audible.button.setTooltip (tips::cam::audible);
    addAndMakeVisible (audible);

    const char* names[] = { "Cam", "Filters", "Amp", "Mix" };
    const char* tabTips[] = { tips::cam::pageCam, tips::cam::pageFilters, tips::cam::pageAmp, tips::cam::pageMix };
    for (int i = 0; i < 4; ++i)
    {
        auto& t = tabs[(size_t) i];
        t.setButtonText (names[i]);
        t.setClickingTogglesState (false);
        t.setTooltip (tabTips[i]);
        t.setColour (juce::TextButton::buttonOnColourId, colour.withAlpha (0.8f));
        t.setColour (juce::TextButton::textColourOnId, theme::background);
        t.setMouseClickGrabsKeyboardFocus (false);
        t.onClick = [this, i] { showPage (i); };
        addAndMakeVisible (t);
        addChildComponent (pages[(size_t) i]);
    }

    buildCamPage();
    buildFilterPage();
    buildAmpPage();
    buildMixPage();
    showPage (0);
    startTimerHz (10);
}

CamKnob& CamStrip::knob (ControlPage& page, int field, const char* tooltip, bool bipolar)
{
    auto& k = page.make<CamKnob> (processor, ids::cam (cam, field), camSpecs[field].name, colour, bipolar);
    k.setTooltip (tooltip);
    return k;
}

void CamStrip::buildCamPage()
{
    auto& page = pages[0];
    auto& apvts = processor.getAPVTS();

    auto& start   = knob (page, cfOpening, tips::cam::start);
    auto& length  = knob (page, cfDuration, tips::cam::length);
    auto& overlap = knob (page, cfOverlap, tips::cam::overlap);
    auto& coarse  = knob (page, cfCoarse, tips::cam::coarse, true);
    auto& fine    = knob (page, cfFine, tips::cam::fine, true);
    page.addRow ({ &start, &length, &overlap, &coarse, &fine }, 64);

    auto& shape = page.combo (apvts, ids::cam (cam, cfOverlapShape), colour, tips::cam::overlapShape);
    auto& snap  = page.combo (apvts, ids::cam (cam, cfSnap), colour, tips::cam::snap);
    page.addRow ({ &shape, &snap }, 22);

    auto& cylinders = knob (page, cfCylinders, tips::cam::cylinders);
    detuneKnob = &knob (page, cfDetune, tips::cam::detune);
    spreadKnob = &knob (page, cfSpread, tips::cam::spread);
    auto& randomPhase = page.make<fxme::FxmeButton> (apvts, ids::cam (cam, cfRandomPhase), "Rnd phase", colour);
    randomPhase.button.setTooltip (tips::cam::randomPhase);
    page.addRow ({ &cylinders, detuneKnob, spreadKnob, &randomPhase }, 64);
}

void CamStrip::buildFilterPage()
{
    auto& page = pages[1];
    auto& apvts = processor.getAPVTS();

    auto& routing = page.combo (apvts, ids::cam (cam, cfFilterRouting), colour, tips::cam::routing);
    balanceKnob = &knob (page, cfFilterBalance, tips::cam::balance, true);
    balanceKnob->setSliderStyle (juce::Slider::LinearHorizontal);
    page.addRow ({ &routing, balanceKnob }, 22);

    for (int fi = 0; fi < 2; ++fi)
    {
        const int o = fi * filterFieldStride;
        auto& on = page.make<fxme::FxmeButton> (apvts, ids::cam (cam, cfF1On + o), fi == 0 ? "F1" : "F2", colour);
        on.button.setTooltip (tips::cam::filterOn);
        typeBoxes[(size_t) fi] = &page.combo (apvts, ids::cam (cam, cfF1Type + o), colour, tips::cam::filterType);
        vowelA[(size_t) fi] = &page.combo (apvts, ids::cam (cam, cfF1VowelA + o), colour, tips::cam::vowelA);
        vowelB[(size_t) fi] = &page.combo (apvts, ids::cam (cam, cfF1VowelB + o), colour, tips::cam::vowelB);
        page.addRow ({ &on, typeBoxes[(size_t) fi], vowelA[(size_t) fi], vowelB[(size_t) fi] }, 22);

        cutoffKnobs[(size_t) fi]   = &knob (page, cfF1Cutoff + o, tips::cam::cutoff);
        resKnobs[(size_t) fi]      = &knob (page, cfF1Res + o, tips::cam::resonance);
        gainKnobs[(size_t) fi]     = &knob (page, cfF1Gain + o, tips::cam::gain, true);
        keyKnobs[(size_t) fi]      = &knob (page, cfF1KeyTrack + o, tips::cam::keyTrack, true);
        envKnobs[(size_t) fi]      = &knob (page, cfF1EnvAmount + o, tips::cam::envAmount, true);
        vowelPosKnobs[(size_t) fi] = &knob (page, cfF1VowelPos + o, tips::cam::vowelPos);
        page.addRow ({ cutoffKnobs[(size_t) fi], resKnobs[(size_t) fi], gainKnobs[(size_t) fi],
                       keyKnobs[(size_t) fi], envKnobs[(size_t) fi], vowelPosKnobs[(size_t) fi] }, 52);
    }
}

void CamStrip::buildAmpPage()
{
    auto& page = pages[2];
    auto& a = knob (page, cfAttack, tips::cam::attack);
    auto& d = knob (page, cfDecay, tips::cam::decay);
    auto& s = knob (page, cfSustain, tips::cam::sustain);
    auto& r = knob (page, cfRelease, tips::cam::release);
    page.addRow ({ &a, &d, &s, &r }, 70);

    auto& ac = knob (page, cfAttackCurve, tips::cam::attackCurve, true);
    auto& dc = knob (page, cfDecayCurve, tips::cam::decayCurve, true);
    auto& rc = knob (page, cfReleaseCurve, tips::cam::releaseCurve, true);
    auto& v  = knob (page, cfVelocity, tips::cam::velocity);
    page.addRow ({ &ac, &dc, &rc, &v }, 64);
}

void CamStrip::buildMixPage()
{
    auto& page = pages[3];
    auto& apvts = processor.getAPVTS();

    auto& volume = knob (page, cfVolume, tips::cam::volume);
    auto& pan    = knob (page, cfPan, tips::cam::pan, true);
    auto& send1  = knob (page, cfSend1, tips::cam::send1);
    auto& send2  = knob (page, cfSend2, tips::cam::send2);
    page.addRow ({ &volume, &pan, &send1, &send2 }, 72);

    auto& tap1 = page.combo (apvts, ids::cam (cam, cfTap1), theme::auxColour (0), tips::cam::tap1);
    auto& tap2 = page.combo (apvts, ids::cam (cam, cfTap2), theme::auxColour (1), tips::cam::tap2);
    page.addRow ({ nullptr, nullptr, &tap1, &tap2 }, 22);

    // The lifts of the timing belt's connections into this cam.
    ControlPage::Row lifts;
    for (int s = 0; s < numCams; ++s)
    {
        auto& k = page.make<CamKnob> (processor, ids::cam (cam, cfLift1 + s), "Lift " + juce::String (s + 1), theme::camColour (s));
        k.setTooltip (tips::cam::lift);
        lifts.push_back (&k);
    }
    page.addRow (lifts, 56);
}

//==============================================================================
void CamStrip::showPage (int page)
{
    currentPage = page;
    for (int i = 0; i < 4; ++i)
    {
        pages[(size_t) i].setVisible (i == page);
        tabs[(size_t) i].setToggleState (i == page, juce::dontSendNotification);
    }
}

void CamStrip::timerCallback()
{
    auto& apvts = processor.getAPVTS();
    auto value = [&] (int f) { return apvts.getRawParameterValue (ids::cam (cam, f))->load(); };

    using T = fxme::MultiModeFilter::Type;
    for (int fi = 0; fi < 2; ++fi)
    {
        const int o = fi * filterFieldStride;
        const bool on = value (cfF1On + o) > 0.5f;
        const auto type = (T) (int) value (cfF1Type + o);
        const bool formant = type == T::formant;
        const bool hasGain = type == T::peak || type == T::lowShelf || type == T::highShelf;

        typeBoxes[(size_t) fi]->setEnabled (on);
        cutoffKnobs[(size_t) fi]->setEnabled (on && ! formant);
        keyKnobs[(size_t) fi]->setEnabled (on && ! formant);
        envKnobs[(size_t) fi]->setEnabled (on && ! formant);
        resKnobs[(size_t) fi]->setEnabled (on);
        gainKnobs[(size_t) fi]->setEnabled (on && hasGain);
        vowelPosKnobs[(size_t) fi]->setEnabled (on && formant);
        vowelA[(size_t) fi]->setEnabled (on && formant);
        vowelB[(size_t) fi]->setEnabled (on && formant);
    }
    balanceKnob->setEnabled (value (cfFilterRouting) > 0.5f);

    const bool unison = value (cfCylinders) > 1.5f;
    detuneKnob->setEnabled (unison);
    spreadKnob->setEnabled (unison);
}

//==============================================================================
void CamStrip::paint (juce::Graphics& g)
{
    theme::paintPanel (g, getLocalBounds().toFloat(), colour);

    auto header = getLocalBounds().reduced (6, 4).removeFromTop (22);
    g.setColour (colour);
    g.fillRoundedRectangle (header.removeFromLeft (4).toFloat(), 2.0f);
    g.setFont (theme::labelFont (14.0f));
    g.setColour (theme::text);
    g.drawText ("CAM " + juce::String (cam + 1), header.withTrimmedLeft (6), juce::Justification::centredLeft);
}

void CamStrip::resized()
{
    auto area = getLocalBounds().reduced (6, 4);
    auto header = area.removeFromTop (22);
    header.removeFromLeft (60);
    audible.setBounds (header.removeFromLeft (76));
    const int tabW = juce::jmin (58, header.getWidth() / 4);
    for (int i = 4; --i >= 0;)
        tabs[(size_t) i].setBounds (header.removeFromRight (tabW).reduced (1, 1));

    area.removeFromTop (4);
    for (auto& p : pages)
        p.setBounds (area);
}

} // namespace camshaft
