/*
  ------------------------------------------------------------------------------
    CamKnob.cpp

    See CamKnob.h.

    Author: Olivier Doaré, github.com/odoare
    Dual-licensed, mirroring the JUCE framework it depends on: under the GNU
    AGPL Version 3.0, or under commercial terms available from the author.
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
  ------------------------------------------------------------------------------
*/

#include "CamKnob.h"
#include "PluginProcessor.h"
#include "ParamIDs.h"
#include "Theme.h"

namespace camshaft
{

using namespace cam;

juce::String sourceName (juce::AudioProcessorValueTreeState& apvts, int s)
{
    if (s >= srcModFirst && s < srcModFirst + numModulators)
    {
        const int m = s - srcModFirst;
        const auto* type = apvts.getRawParameterValue (ids::mod (m, mfType));
        const int t = type != nullptr ? juce::jlimit (0, countOf (modTypes) - 1, (int) type->load()) : 0;
        return juce::String (modTypes[t]) + " " + juce::String (m + 1);
    }
    return ids::fixedSourceName (s);
}

juce::String sourceDragDescription (int source) { return "modsrc:" + juce::String (source); }

int sourceFromDragDescription (const juce::var& description)
{
    const auto s = description.toString();
    return s.startsWith ("modsrc:") ? s.fromFirstOccurrenceOf (":", false, false).getIntValue() : 0;
}

juce::PopupMenu sourcesMenu (juce::AudioProcessorValueTreeState& apvts, int ticked, bool withNone)
{
    juce::PopupMenu menu, mods, midi, envs;
    if (withNone)
        menu.addItem (1 + srcNone, "None", true, ticked == srcNone);
    for (int m = 0; m < numModulators; ++m)
        mods.addItem (1 + srcModFirst + m, sourceName (apvts, srcModFirst + m), true, ticked == srcModFirst + m);
    for (int s : { (int) srcVelocity, (int) srcReleaseVelocity, (int) srcKeyTrack, (int) srcAftertouch,
                   (int) srcPolyAftertouch, (int) srcPitchBend, (int) srcModWheel, (int) srcRandom })
        midi.addItem (1 + s, ids::fixedSourceName (s), true, ticked == s);
    for (int i = 0; i < numCcSources; ++i)
        midi.addItem (1 + srcCcFirst + i, ids::fixedSourceName (srcCcFirst + i), true, ticked == srcCcFirst + i);
    for (int c = 0; c < numCams; ++c)
        envs.addItem (1 + srcAmpEnvFirst + c, ids::fixedSourceName (srcAmpEnvFirst + c), true, ticked == srcAmpEnvFirst + c);
    menu.addSubMenu ("Modulators", mods);
    menu.addSubMenu ("MIDI and voice", midi);
    menu.addSubMenu ("Amp envelopes", envs);
    return menu;
}

bool sourceIsBipolar (juce::AudioProcessorValueTreeState& apvts, int s)
{
        if (s >= srcModFirst && s < srcModFirst + numModulators)
        {
            const int m = s - srcModFirst;
            const int type = (int) apvts.getRawParameterValue (ids::mod (m, mfType))->load();
            return (type == mtLfo || type == mtCurve) && apvts.getRawParameterValue (ids::mod (m, mfBipolar))->load() > 0.5f;
        }
        return s == srcPitchBend || s == srcKeyTrack || s == srcRandom;
}

namespace
{
    /** FxmeSlider's right-click value entry, reachable from the menu. */
    void showValueEditor (juce::Slider& slider)
    {
        auto* label = new juce::Label();
        label->setEditable (true);
        label->setText (slider.getTextFromValue (slider.getValue()), juce::dontSendNotification);
        label->setJustificationType (juce::Justification::centred);
        label->setColour (juce::Label::backgroundColourId, juce::Colours::black);
        label->setColour (juce::Label::textColourId, juce::Colours::white);
        label->setColour (juce::Label::outlineColourId, juce::Colours::grey);

        const auto bounds = slider.getScreenBounds();
        label->setBounds (0, 0, juce::jmin (90, bounds.getWidth() + 20), 20);
        label->setCentrePosition (bounds.getCentre());

        juce::Component::SafePointer<juce::Slider> safe (&slider);
        label->onTextChange = [safe, label]
        {
            if (safe != nullptr)
                safe->setValue (safe->getValueFromText (label->getText()), juce::sendNotificationSync);
        };
        label->onEditorHide = [label] { juce::MessageManager::callAsync ([label] { delete label; }); };
        label->addToDesktop (juce::ComponentPeer::windowIsTemporary | juce::ComponentPeer::windowHasDropShadow);
        label->setVisible (true);
        label->showEditor();
    }
}

//==============================================================================
CamKnob::CamKnob (CamshaftAudioProcessor& p, const juce::String& id, const juce::String& label,
                  juce::Colour a, bool bipolar)
    : fxme::FxmeSlider (p.getAPVTS(), id, label, a),
      processor (p), paramID (id), accent (a)
{
    setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    setShowLabel (true);
    theme::accentSlider (*this, accent);
    setColour (juce::Slider::textBoxTextColourId, theme::text);

    param = p.getAPVTS().getParameter (id);
    raw = p.getAPVTS().getRawParameterValue (id);

    if (bipolar)
        setCentralValue (0.0);

    // A cam field: its live value comes from the engine's newest voice.
    for (int c = 0; c < numCams && camIndex < 0; ++c)
        for (int f = 0; f < numCamFields; ++f)
            if (ids::cam (c, f) == id)
            {
                camIndex = c;
                camField = f;
                break;
            }

    setPopupDisplayEnabled (false, false, nullptr);
    startTimerHz (15);
}

CamKnob::~CamKnob()
{
    stopTimer();
}

float CamKnob::liveValue() const
{
    const auto& display = processor.getEngine().getDisplay();
    if (camIndex >= 0)
    {
        if (! display.active[(size_t) camIndex].load (std::memory_order_relaxed))
            return -1.0f;
        return display.camValue[(size_t) camIndex][(size_t) camField].load (std::memory_order_relaxed);
    }

    // A global destination: the value the DSP reads, when it differs.
    if (param != nullptr && raw != nullptr)
    {
        const float v = param->convertTo0to1 (raw->load (std::memory_order_relaxed));
        if (std::abs (v - param->getValue()) > 1.0e-4f)
            return v;
    }
    return -1.0f;
}

void CamKnob::timerCallback()
{
    auto& structure = processor.getStructure();
    auto& apvts = processor.getAPVTS();

    float lo = 0.0f, hi = 0.0f;
    bool ring = false;
    for (int i : structure.routesTo (paramID))
    {
        const auto r = structure.getRoute (i);
        const float d = apvts.getRawParameterValue (ids::routeDepth (i))->load();
        if (cam::isZero (d))
            continue;
        ring = true;
        const float smin = sourceIsBipolar (apvts, r.source) ? -1.0f : 0.0f;
        lo += juce::jmin (d * smin, d);
        hi += juce::jmax (d * smin, d);
    }

    const float newLive = liveValue();
    const bool newMapped = processor.getMidiLearn().getCcFor (paramID) >= 0;
    const bool newLocked = structure.isLocked (paramID);
    const bool newLearning = processor.getMidiLearn().getLearningParam() == paramID;
    blink = (blink + 1) % 8;

    if (ring != hasRing || std::abs (lo - ringLo) > 1.0e-6f || std::abs (hi - ringHi) > 1.0e-6f || std::abs (newLive - live) > 1.0e-3f
        || newMapped != mapped || newLocked != locked || newLearning != learning || learning)
    {
        hasRing = ring;
        ringLo = lo;
        ringHi = hi;
        live = newLive;
        mapped = newMapped;
        locked = newLocked;
        learning = newLearning;
        repaint();
    }
}

//==============================================================================
void CamKnob::paint (juce::Graphics& g)
{
    fxme::FxmeSlider::paint (g);
    if (! isRotary())
    {
        if (learning || dropHover)
        {
            g.setColour (theme::accent.withAlpha (0.8f));
            g.drawRect (getLocalBounds(), 1);
        }
        return;
    }

    // The same geometry as FxmeLookAndFeel::drawRotarySlider.
    const auto b = getLocalBounds().toFloat();
    const float diameter = 0.7f * juce::jmin (b.getWidth(), b.getHeight());
    const float labelHeight = juce::jmin (diameter * 0.32f, b.getHeight() - diameter);
    const float radius = diameter * 0.5f;
    const float cx = b.getCentreX();
    const float cy = b.getY() + (b.getHeight() - (diameter + labelHeight)) * 0.5f + radius;
    const auto rp = getRotaryParameters();
    auto angleAt = [&] (float norm)
    {
        return rp.startAngleRadians + juce::jlimit (0.0f, 1.0f, norm) * (rp.endAngleRadians - rp.startAngleRadians);
    };

    const float base = param != nullptr ? param->getValue() : 0.0f;
    const float ringR = radius + diameter / 12.0f * 1.3f;

    if (hasRing)
    {
        juce::Path arc;
        const float a0 = angleAt (base + ringLo), a1 = angleAt (base + ringHi);
        arc.addCentredArc (cx, cy, ringR, ringR, 0.0f, juce::jmin (a0, a1), juce::jmax (a0, a1), true);
        g.setColour (theme::steel.withAlpha (isEnabled() ? 0.85f : 0.3f));
        g.strokePath (arc, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    if (live >= 0.0f)
    {
        const auto pt = juce::Point<float> (cx, cy).getPointOnCircumference (ringR, angleAt (live));
        g.setColour (theme::text);
        g.fillEllipse (juce::Rectangle<float> (4.0f, 4.0f).withCentre (pt));
    }

    g.setFont (theme::labelFont (9.0f));
    if (mapped)
    {
        g.setColour (theme::accent.withAlpha (0.9f));
        g.drawText ("M", juce::Rectangle<float> (b.getRight() - 11.0f, b.getY(), 11.0f, 11.0f), juce::Justification::centred);
    }
    if (locked)
    {
        // A small padlock, top left.
        const auto r = juce::Rectangle<float> (b.getX() + 1.0f, b.getY() + 1.0f, 8.0f, 10.0f);
        g.setColour (theme::dimText);
        g.fillRoundedRectangle (r.withTrimmedTop (4.0f), 1.0f);
        juce::Path shackle;
        shackle.addCentredArc (r.getCentreX(), r.getY() + 4.0f, 2.6f, 3.0f, 0.0f,
                               -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi, true);
        g.strokePath (shackle, juce::PathStrokeType (1.2f));
    }
    if (learning || dropHover)
    {
        g.setColour (theme::accent.withAlpha (learning ? (blink < 4 ? 0.9f : 0.3f) : 0.8f));
        g.drawEllipse (juce::Rectangle<float> (2.0f * ringR, 2.0f * ringR).withCentre ({ cx, cy }), 1.5f);
    }
}

//==============================================================================
void CamKnob::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        showMenu();
        return;
    }
    juce::Slider::mouseDown (e);
}

void CamKnob::showMenu()
{
    auto& apvts = processor.getAPVTS();
    auto& structure = processor.getStructure();
    auto& learn = processor.getMidiLearn();

    enum { typeValue = 1, reset, learnMidi, cancelLearn, forgetMidi, removeMods, toggleLock, sourceBase = 1000 };

    juce::PopupMenu menu;
    menu.addSectionHeader (param != nullptr ? param->getName (48) : paramID);
    menu.addItem (typeValue, "Type a value...");
    menu.addItem (reset, "Reset to default");
    menu.addSeparator();

    const int cc = learn.getCcFor (paramID);
    if (learn.getLearningParam() == paramID)
        menu.addItem (cancelLearn, "Cancel MIDI learn");
    else
        menu.addItem (learnMidi, "Learn MIDI");
    if (cc >= 0)
        menu.addItem (forgetMidi, "Forget MIDI (CC " + juce::String (cc) + ")");

    const bool modulatable = processor.getDestinations().flatFor (paramID) >= 0;
    if (modulatable)
    {
        menu.addSeparator();
        juce::PopupMenu sources;
        const auto list = sourcesMenu (apvts);
        // Re-number the source items so they do not collide with the others.
        for (juce::PopupMenu::MenuItemIterator it (list, true); it.next();)
            if (auto& item = it.getItem(); item.itemID > 0)
                sources.addItem (sourceBase + item.itemID, item.text);
        menu.addSubMenu ("Modulate from", sources);
        menu.addItem (removeMods, "Remove modulations", ! structure.routesTo (paramID).isEmpty());
    }

    menu.addSeparator();
    menu.addItem (toggleLock, "Lock from Kick-start", true, structure.isLocked (paramID));

    juce::Component::SafePointer<CamKnob> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                        [safe] (int result)
    {
        if (safe == nullptr || result == 0)
            return;
        auto& self = *safe;
        auto& st = self.processor.getStructure();
        auto& ml = self.processor.getMidiLearn();

        if (result >= sourceBase)
        {
            st.addRoute (result - sourceBase - 1, self.paramID, 0.3f);
            return;
        }
        switch (result)
        {
            case typeValue:   showValueEditor (self); break;
            case reset:       if (self.param != nullptr) self.param->setValueNotifyingHost (self.param->getDefaultValue()); break;
            case learnMidi:   ml.startLearning (self.paramID); break;
            case cancelLearn: ml.cancelLearning(); break;
            case forgetMidi:  ml.forget (self.paramID); break;
            case removeMods:  for (int i : st.routesTo (self.paramID)) st.clearRoute (i); break;
            case toggleLock:  st.setLocked (self.paramID, ! st.isLocked (self.paramID)); break;
            default: break;
        }
    });
}

//==============================================================================
bool CamKnob::isInterestedInDragSource (const SourceDetails& details)
{
    return sourceFromDragDescription (details.description) > 0
           && processor.getDestinations().flatFor (paramID) >= 0;
}

void CamKnob::itemDropped (const SourceDetails& details)
{
    dropHover = false;
    const int s = sourceFromDragDescription (details.description);
    if (s > 0)
        processor.getStructure().addRoute (s, paramID, 0.3f);
    repaint();
}

} // namespace camshaft
