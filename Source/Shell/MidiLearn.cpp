/*
  ------------------------------------------------------------------------------
    MidiLearn.cpp

    See MidiLearn.h.

    Author: Olivier Doaré, github.com/odoare
    Dual-licensed, mirroring the JUCE framework it depends on: under the GNU
    AGPL Version 3.0, or under commercial terms available from the author.
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
  ------------------------------------------------------------------------------
*/

#include "MidiLearn.h"
#include "AppSettings.h"

namespace camshaft
{

MidiLearn::MidiLearn (juce::AudioProcessorValueTreeState& state, StructureState& s)
    : apvts (state), structure (s)
{
    for (auto* p : apvts.processor.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            params.add (rp);

    for (auto& c : ccToParam)
        c.store (-1);

    structure.addChangeListener (this);
    rebuildTable();
    startTimerHz (30);
}

MidiLearn::~MidiLearn()
{
    structure.removeChangeListener (this);
    stopTimer();
}

void MidiLearn::changeListenerCallback (juce::ChangeBroadcaster*)
{
    rebuildTable();
}

void MidiLearn::rebuildTable()
{
    std::array<int, 128> table;
    table.fill (-1);
    paramToCc.clear();

    const auto section = structure.getSection (StructureState::learnType);
    for (const auto& map : section)
    {
        const int cc = (int) map.getProperty ("cc", -1);
        const auto id = map.getProperty ("param").toString();
        if (cc < 0 || cc > 127)
            continue;
        for (int i = 0; i < params.size(); ++i)
            if (params.getUnchecked (i)->paramID == id)
                table[(size_t) cc] = i;
        paramToCc[id] = cc;
    }

    for (int cc = 0; cc < 128; ++cc)
        ccToParam[(size_t) cc].store (table[(size_t) cc], std::memory_order_relaxed);
}

//==============================================================================
void MidiLearn::startLearning (const juce::String& paramID)
{
    for (int i = 0; i < params.size(); ++i)
        if (params.getUnchecked (i)->paramID == paramID)
        {
            learning.store (i);
            sendChangeMessage();
            return;
        }
}

void MidiLearn::cancelLearning()
{
    learning.store (-1);
    sendChangeMessage();
}

juce::String MidiLearn::getLearningParam() const
{
    const int i = learning.load();
    return juce::isPositiveAndBelow (i, params.size()) ? params.getUnchecked (i)->paramID : juce::String();
}

int MidiLearn::getCcFor (const juce::String& paramID) const
{
    const auto it = paramToCc.find (paramID);
    return it != paramToCc.end() ? it->second : -1;
}

void MidiLearn::forget (const juce::String& paramID)
{
    auto section = structure.getSection (StructureState::learnType);
    for (int i = section.getNumChildren(); --i >= 0;)
        if (section.getChild (i).getProperty ("param").toString() == paramID)
            section.removeChild (i, nullptr);
    rebuildTable();
    sendChangeMessage();
}

void MidiLearn::assign (int cc, int paramIndex)
{
    if (! juce::isPositiveAndBelow (paramIndex, params.size()) || cc < 0 || cc > 127)
        return;
    const auto id = params.getUnchecked (paramIndex)->paramID;

    // One CC drives one parameter, and a parameter listens to one CC.
    auto section = structure.getSection (StructureState::learnType);
    for (int i = section.getNumChildren(); --i >= 0;)
    {
        const auto map = section.getChild (i);
        if ((int) map.getProperty ("cc", -1) == cc || map.getProperty ("param").toString() == id)
            section.removeChild (i, nullptr);
    }

    juce::ValueTree map (StructureState::mapType);
    map.setProperty ("cc", cc, nullptr);
    map.setProperty ("param", id, nullptr);
    section.appendChild (map, nullptr);
    rebuildTable();
    sendChangeMessage();
}

void MidiLearn::saveAsDefault()
{
    const auto section = structure.getSection (StructureState::learnType);
    if (auto xml = section.createXml())
        setDefaultMidiLearn (xml.get());
}

void MidiLearn::clearDefault()
{
    setDefaultMidiLearn (nullptr);
}

void MidiLearn::applyDefaultIfEmpty()
{
    auto section = structure.getSection (StructureState::learnType);
    if (section.getNumChildren() > 0)
        return;
    if (auto xml = getDefaultMidiLearn())
    {
        const auto loaded = juce::ValueTree::fromXml (*xml);
        for (const auto& map : loaded)
            section.appendChild (map.createCopy(), nullptr);
        rebuildTable();
    }
}

//==============================================================================
bool MidiLearn::handleController (int cc, int value) noexcept
{
    if (cc < 0 || cc > 127)
        return false;

    const int learn = learning.exchange (-1);
    const int target = learn >= 0 ? learn : ccToParam[(size_t) cc].load (std::memory_order_relaxed);
    if (target < 0)
        return false;

    int start1, size1, start2, size2;
    fifo.prepareToWrite (1, start1, size1, start2, size2);
    if (size1 > 0)
    {
        events[(size_t) start1] = { target, cc, (float) value / 127.0f, learn >= 0 };
        fifo.finishedWrite (1);
    }
    return true;
}

void MidiLearn::timerCallback()
{
    int start1, size1, start2, size2;
    const int n = fifo.getNumReady();
    if (n == 0)
        return;
    fifo.prepareToRead (n, start1, size1, start2, size2);

    // Only the last value of each parameter matters.
    std::map<int, float> latest;
    auto handle = [&] (const Event& e)
    {
        if (e.learn)
            assign (e.cc, e.param);
        latest[e.param] = e.value;
    };
    for (int i = 0; i < size1; ++i) handle (events[(size_t) (start1 + i)]);
    for (int i = 0; i < size2; ++i) handle (events[(size_t) (start2 + i)]);
    fifo.finishedRead (size1 + size2);

    for (const auto& [index, value] : latest)
    {
        auto* p = params.getUnchecked (index);
        p->beginChangeGesture();
        p->setValueNotifyingHost (value);
        p->endChangeGesture();
    }
}

} // namespace camshaft
