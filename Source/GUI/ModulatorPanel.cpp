/*
  ------------------------------------------------------------------------------
    ModulatorPanel.cpp

    See ModulatorPanel.h.

    Author: Olivier Doaré, github.com/odoare
    Dual-licensed, mirroring the JUCE framework it depends on: under the GNU
    AGPL Version 3.0, or under commercial terms available from the author.
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
  ------------------------------------------------------------------------------
*/

#include "ModulatorPanel.h"
#include "AppSettings.h"
#include "PluginProcessor.h"
#include "ParamIDs.h"
#include "Theme.h"
#include "Tooltips.h"

namespace camshaft
{

using namespace cam;

//==============================================================================
SourceChip::SourceChip (CamshaftAudioProcessor& p, int s, juce::Colour c)
    : processor (p), source (s), colour (c)
{
    name = sourceName (p.getAPVTS(), s);
    setTooltip (s >= srcModFirst && s < srcModFirst + numModulators ? tips::mods::slotChip : tips::mods::fixedChip);
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    startTimerHz (30);
}

void SourceChip::timerCallback()
{
    const auto& display = processor.getEngine().getDisplay();
    const int m = source - srcModFirst;
    const bool used = m < 0 || m >= numModulators || display.modUsed[(size_t) m].load (std::memory_order_relaxed);
    const float v = used ? display.source[(size_t) source].load (std::memory_order_relaxed) : 0.0f;
    const auto n = sourceName (processor.getAPVTS(), source);
    const bool bi = sourceIsBipolar (processor.getAPVTS(), source);
    if (std::abs (v - level) > 0.002f || n != name || bi != bipolar || used != inUse)
    {
        level = v;
        name = n;
        bipolar = bi;
        inUse = used;
        repaint();
    }
}

void SourceChip::paint (juce::Graphics& g)
{
    // A slot that is off or modulates nothing is grey; the others wear their
    // colour (also the ring they draw around the knobs they modulate).
    auto b = getLocalBounds().toFloat().reduced (1.0f);
    const auto tint = inUse ? colour : theme::dimText.withAlpha (0.6f);
    g.setColour (selected ? tint.withAlpha (0.35f) : theme::panelHi);
    g.fillRoundedRectangle (b, 3.0f);
    g.setColour (selected ? tint : (inUse ? tint.withAlpha (0.55f) : theme::panelLine));
    g.drawRoundedRectangle (b, 3.0f, 1.0f);
    g.setColour (tint);
    g.fillRoundedRectangle (b.withWidth (3.0f).reduced (0.0f, 2.0f), 1.5f);

    // Live output: a bar along the bottom. A bipolar source grows from the
    // centre, -1 at the left edge and +1 at the right; a unipolar one from
    // the left edge, 0 to 1. One scale throughout, so the bar moves as
    // continuously as the value.
    const auto bar = b.removeFromBottom (3.0f).reduced (3.0f, 0.0f);
    g.setColour (colour.withAlpha (0.9f));
    if (! inUse)
    {
        // Off, or feeding nothing: not computed, nothing to show.
    }
    else if (bipolar)
    {
        const float v = juce::jlimit (-1.0f, 1.0f, level);
        const float x0 = bar.getCentreX(), x1 = bar.getCentreX() + 0.5f * bar.getWidth() * v;
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (juce::jmin (x0, x1), bar.getY(), juce::jmax (x0, x1), bar.getBottom()));
        g.setColour (colour.withAlpha (0.35f));
        g.fillRect (juce::Rectangle<float> (x0 - 0.5f, bar.getY() - 1.0f, 1.0f, bar.getHeight() + 1.0f));   // the zero mark
    }
    else
        g.fillRect (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, level)));

    g.setColour (inUse ? theme::text : theme::dimText);
    g.setFont (theme::labelFont (10.5f));
    g.drawText (name, b, juce::Justification::centred, true);
}

void SourceChip::mouseDown (const juce::MouseEvent&)
{
    dragging = false;
}

void SourceChip::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging || e.getDistanceFromDragStart() < 4)
        return;
    dragging = true;
    if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this))
        container->startDragging (sourceDragDescription (source), this);
}

void SourceChip::mouseUp (const juce::MouseEvent&)
{
    if (! dragging && onClick)
        onClick();
    dragging = false;
}

//==============================================================================
ModulatorEditor::ModulatorEditor (CamshaftAudioProcessor& p, int s)
    : processor (p), slot (s), colour (theme::accent), slotColour (theme::modColour (s))
{
    typeBox = &combo (mfType, tips::mods::type);
    level = &knob (mfLevel, tips::mods::level);
    bipolar = &toggle (mfBipolar, "Bipolar", tips::mods::bipolar);

    shapeBox = &combo (mfShape, tips::mods::shape);
    rate = &knob (mfRate, tips::mods::rate);
    sync = &toggle (mfSync, "Sync", tips::mods::sync);
    divisionBox = &combo (mfDivision, tips::mods::division);
    phase = &knob (mfPhase, tips::mods::phase);
    delay = &knob (mfDelay, tips::mods::delay);
    fade = &knob (mfFade, tips::mods::fade);
    modeBox = &combo (mfMode, tips::mods::mode);

    attack = &knob (mfAttack, tips::mods::attack);
    decay = &knob (mfDecay, tips::mods::decay);
    sustain = &knob (mfSustain, tips::mods::sustain);
    release = &knob (mfRelease, tips::mods::release);
    attackCurve = &knob (mfAttackCurve, tips::mods::curve, true);
    decayCurve = &knob (mfDecayCurve, tips::mods::curve, true);
    releaseCurve = &knob (mfReleaseCurve, tips::mods::curve, true);
    velocity = &knob (mfVelocity, tips::mods::velocity);

    duration = &knob (mfDuration, tips::mods::duration);
    beats = &knob (mfBeats, tips::mods::beats);
    speed = &knob (mfSpeed, tips::mods::speed);
    durationMode = &combo (mfDurationMode, tips::mods::durationMode);
    dawSync = &toggle (mfDawSync, "Stick to DAW", tips::mods::daw);

    // The curve in its modulator's colour; the sustain controls in the
    // sustain region's blue.
    fxme::BreakpointCurveEditor::Colours cc;
    cc.curve = slotColour;
    cc.fill = slotColour.withAlpha (0.15f);
    cc.sustain = sustainBlue.withAlpha (0.19f);
    cc.background = theme::plotBack;
    curveEditor.setColours (cc);
    curveEditor.setTooltip (tips::mods::curveEditor);
    curveEditor.setShape (processor.getStructure().getCurve (slot));
    curveEditor.onChange = [this] (const fxme::CurveShape& shape) { curveChanged (shape); };
    curveEditor.onPointHeld = [this] (int point, float value)
    {
        if (onCurveProbe)
            onCurveProbe (point >= 0 ? slot : -1, value);
    };
    addChildComponent (curveEditor);

    auto sustainEdited = [this]
    {
        auto shape = curveEditor.getShape();
        shape.sustainEnabled = sustainToggle.getToggleState();
        const int mode = sustainModeBox.getSelectedId();   // 1 hold, 2 loop, 3 back and forth
        shape.sustainLoop = mode >= 2;
        shape.sustainPingPong = mode == 3;
        shape.releaseEnabled = releaseToggle.getToggleState();
        curveEditor.setShape (shape);
        curveChanged (curveEditor.getShape());
    };
    for (auto* t : { &sustainToggle, &releaseToggle })
    {
        theme::accentToggle (*t, sustainBlue);
        addChildComponent (*t);
        t->onClick = sustainEdited;
    }
    sustainModeBox.addItemList ({ "Hold", "Loop", "Back & forth" }, 1);
    theme::accentCombo (sustainModeBox, sustainBlue);
    sustainModeBox.onChange = sustainEdited;
    addChildComponent (sustainModeBox);
    sustainToggle.setTooltip (tips::mods::sustainRegion);
    sustainModeBox.setTooltip (tips::mods::loop);
    releaseToggle.setTooltip (tips::mods::releaseRegion);

    timerCallback();
    startTimerHz (15);
}

ModulatorEditor::~ModulatorEditor()
{
    stopTimer();
    if (onCurveProbe)
        onCurveProbe (-1, 0.0f);
}

CamKnob& ModulatorEditor::knob (int field, const char* tooltip, bool bipolarKnob)
{
    auto k = std::make_unique<CamKnob> (processor, ids::mod (slot, field), modSpecs[field].name, colour, bipolarKnob);
    k->setTooltip (tooltip);
    auto& ref = *k;
    addChildComponent (ref);
    owned.push_back (std::move (k));
    return ref;
}

juce::ComboBox& ModulatorEditor::combo (int field, const char* tooltip)
{
    auto box = std::make_unique<juce::ComboBox>();
    if (auto* pc = dynamic_cast<juce::AudioParameterChoice*> (processor.getAPVTS().getParameter (ids::mod (slot, field))))
        box->addItemList (pc->choices, 1);
    theme::accentCombo (*box, colour);
    box->setTooltip (tooltip);
    attachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.getAPVTS(), ids::mod (slot, field), *box));
    auto& ref = *box;
    addChildComponent (ref);
    owned.push_back (std::move (box));
    return ref;
}

fxme::FxmeButton& ModulatorEditor::toggle (int field, const char* label, const char* tooltip)
{
    auto b = std::make_unique<fxme::FxmeButton> (processor.getAPVTS(), ids::mod (slot, field), label, colour);
    b->button.setTooltip (tooltip);
    auto& ref = *b;
    addChildComponent (ref);
    owned.push_back (std::move (b));
    return ref;
}

void ModulatorEditor::curveChanged (const fxme::CurveShape& shape)
{
    processor.getStructure().setCurve (slot, shape);
}

void ModulatorEditor::timerCallback()
{
    auto& apvts = processor.getAPVTS();
    const int type = (int) apvts.getRawParameterValue (ids::mod (slot, mfType))->load();
    if (type != shownType)
    {
        shownType = type;
        layoutForType();
    }

    if (type == mtLfo)
    {
        const bool synced = apvts.getRawParameterValue (ids::mod (slot, mfSync))->load() > 0.5f;
        rate->setVisible (! synced);
        divisionBox->setVisible (synced);
        const bool global = apvts.getRawParameterValue (ids::mod (slot, mfMode))->load() > 0.5f;
        delay->setEnabled (! global);
        fade->setEnabled (! global);
    }
    else if (type == mtCurve)
    {
        const bool beatsMode = apvts.getRawParameterValue (ids::mod (slot, mfDurationMode))->load() > 0.5f;
        duration->setVisible (! beatsMode);
        beats->setVisible (beatsMode);

        // Timed in beats: the beat grid (the settings' division, a line per
        // beat, or per bar for divisions of a beat or more), snapping if
        // asked. In seconds: the editor's own grid.
        if (beatsMode)
        {
            const double length = juce::jmax (0.01, (double) apvts.getRawParameterValue (ids::mod (slot, mfBeats))->load());
            const double division = getCurveGridBeats();
            curveEditor.setTimeGrid (division / length, (division >= 1.0 ? 4.0 : 1.0) / length, getCurveGridSnap());
        }
        else
            curveEditor.setTimeGrid (0.0, 0.0, false);

        // The shape may have changed under us (a preset load).
        curveEditor.setShape (processor.getStructure().getCurve (slot));
        const auto& shape = curveEditor.getShape();
        sustainToggle.setToggleState (shape.sustainEnabled, juce::dontSendNotification);
        sustainModeBox.setSelectedId (! shape.sustainLoop ? 1 : (shape.sustainPingPong ? 3 : 2), juce::dontSendNotification);
        releaseToggle.setToggleState (shape.releaseEnabled, juce::dontSendNotification);
        sustainModeBox.setEnabled (shape.sustainEnabled);
        releaseToggle.setEnabled (shape.sustainEnabled);

        std::vector<float> heads;
        for (const auto& v : processor.getEngine().getDisplay().curvePos[(size_t) slot])
        {
            const float pos = v.load (std::memory_order_relaxed);
            if (pos >= 0.0f)
                heads.push_back (pos);
        }
        curveEditor.setPlayheads (heads);
    }
}

void ModulatorEditor::layoutForType()
{
    for (auto& c : owned)
        c->setVisible (false);
    curveEditor.setVisible (false);
    for (auto* t : { (juce::Component*) &sustainToggle, (juce::Component*) &sustainModeBox, (juce::Component*) &releaseToggle })
        t->setVisible (false);

    typeBox->setVisible (true);
    level->setVisible (shownType != mtOff);

    std::vector<juce::Component*> visible;
    if (shownType == mtLfo)
        visible = { bipolar, shapeBox, rate, sync, phase, delay, fade, modeBox };
    else if (shownType == mtAdsr)
        visible = { attack, decay, sustain, release, attackCurve, decayCurve, releaseCurve, velocity };
    else if (shownType == mtCurve)
        visible = { bipolar, &curveEditor, duration, speed, durationMode, dawSync, &sustainToggle, &sustainModeBox, &releaseToggle };
    for (auto* c : visible)
        c->setVisible (true);

    resized();
    repaint();
    timerCallback();
}

void ModulatorEditor::paint (juce::Graphics& g)
{
    auto header = getLocalBounds().removeFromTop (24);
    g.setColour (slotColour);
    g.setFont (theme::labelFont (13.0f));
    g.drawText (sourceName (processor.getAPVTS(), srcModFirst + slot), header.removeFromLeft (80), juce::Justification::centredLeft);

    if (shownType == mtOff)
        theme::paintEngraved (g, "This slot is off. Choose a type, or drag its chip onto a knob (it becomes an LFO).",
                              getLocalBounds().withTrimmedTop (28), juce::Justification::centred, 12.0f);
}

void ModulatorEditor::resized()
{
    auto area = getLocalBounds();
    auto header = area.removeFromTop (24);
    header.removeFromLeft (80);
    typeBox->setBounds (header.removeFromLeft (90).reduced (2));
    level->setBounds (header.removeFromRight (120).reduced (2));
    level->setSliderStyle (juce::Slider::LinearHorizontal);
    bipolar->setBounds (header.removeFromRight (80).reduced (2));
    area.removeFromTop (4);

    auto row = [&] (std::initializer_list<juce::Component*> items, int height)
    {
        auto line = area.removeFromTop (height);
        const int w = line.getWidth() / (int) items.size();
        for (auto* c : items)
        {
            auto cell = line.removeFromLeft (w);
            if (c != nullptr)
                c->setBounds (cell.reduced (3, 1));
        }
        area.removeFromTop (4);
    };

    if (shownType == mtOff)
    {
    }
    else if (shownType == mtLfo)
    {
        row ({ shapeBox, modeBox, sync, nullptr }, 22);
        row ({ rate, phase, delay, fade }, 72);
        divisionBox->setBounds (rate->getBounds().withSizeKeepingCentre (rate->getWidth(), 22));
    }
    else if (shownType == mtAdsr)
    {
        row ({ attack, decay, sustain, release }, 72);
        row ({ attackCurve, decayCurve, releaseCurve, velocity }, 64);
    }
    else
    {
        auto side = area.removeFromRight (juce::jmin (230, area.getWidth() / 3));
        curveEditor.setBounds (area.reduced (2));
        auto line = side.removeFromTop (22);
        durationMode->setBounds (line.removeFromLeft (line.getWidth() / 2).reduced (2, 0));
        dawSync->setBounds (line.reduced (2, 0));
        side.removeFromTop (2);
        auto knobs = side.removeFromTop (70);
        auto left = knobs.removeFromLeft (knobs.getWidth() / 2);
        duration->setBounds (left.reduced (2));
        beats->setBounds (left.reduced (2));
        speed->setBounds (knobs.reduced (2));
        side.removeFromTop (2);
        auto toggles = side.removeFromTop (22);
        sustainToggle.setBounds (toggles.removeFromLeft (toggles.getWidth() / 2));
        releaseToggle.setBounds (toggles);
        side.removeFromTop (2);
        sustainModeBox.setBounds (side.removeFromTop (22).reduced (2, 0));
    }
}

//==============================================================================
ModulatorPanel::ModulatorPanel (CamshaftAudioProcessor& p) : processor (p)
{
    for (int m = 0; m < numModulators; ++m)
    {
        auto chip = std::make_unique<SourceChip> (processor, srcModFirst + m, theme::modColour (m));
        chip->onClick = [this, m] { select (m); };
        addAndMakeVisible (*chip);
        slotChips.push_back (std::move (chip));
    }

    std::vector<int> fixed { srcVelocity, srcReleaseVelocity, srcKeyTrack, srcModWheel, srcPitchBend,
                             srcAftertouch, srcPolyAftertouch, srcRandom };
    for (int i = 0; i < numCcSources; ++i)
        fixed.push_back (srcCcFirst + i);
    for (int c = 0; c < numCams; ++c)
        fixed.push_back (srcAmpEnvFirst + c);
    for (int s : fixed)
    {
        const auto colour = (s >= srcAmpEnvFirst && s < srcAmpEnvFirst + numCams) ? theme::camColour (s - srcAmpEnvFirst)
                                                                                  : theme::dimText;
        auto chip = std::make_unique<SourceChip> (processor, s, colour);
        addAndMakeVisible (*chip);
        fixedChips.push_back (std::move (chip));
    }

    select (0);
}

void ModulatorPanel::select (int slot)
{
    selected = slot;
    for (int m = 0; m < (int) slotChips.size(); ++m)
        slotChips[(size_t) m]->setSelected (m == slot);
    editor = std::make_unique<ModulatorEditor> (processor, slot);
    editor->onCurveProbe = [this] (int s, float v)
    {
        if (onCurveProbe)
            onCurveProbe (s, v);
    };
    addAndMakeVisible (*editor);
    resized();
}

void ModulatorPanel::paint (juce::Graphics& g)
{
    theme::paintPanel (g, getLocalBounds().toFloat(), theme::steel);
}

void ModulatorPanel::resized()
{
    auto area = getLocalBounds().reduced (6);

    // Slot chips: 4 x 4; fixed sources: 4 per row under them; all the rows
    // share the column's height.
    auto left = area.removeFromLeft (juce::jmin (330, area.getWidth() / 3));
    constexpr int cols = 4, gap = 8;
    const int fixedRows = ((int) fixedChips.size() + cols - 1) / cols;
    const int chipH = juce::jmax (18, (left.getHeight() - gap) / (4 + fixedRows));
    const int cw = left.getWidth() / cols;
    auto grid = left.removeFromTop (chipH * 4);
    for (int m = 0; m < (int) slotChips.size(); ++m)
        slotChips[(size_t) m]->setBounds (grid.getX() + (m % cols) * cw, grid.getY() + (m / cols) * chipH, cw, chipH);

    left.removeFromTop (gap);
    for (int i = 0; i < (int) fixedChips.size(); ++i)
        fixedChips[(size_t) i]->setBounds (left.getX() + (i % cols) * cw, left.getY() + (i / cols) * chipH, cw, chipH);

    area.removeFromLeft (10);
    if (editor != nullptr)
        editor->setBounds (area);
}

} // namespace camshaft
