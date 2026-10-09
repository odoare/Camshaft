/*
  ------------------------------------------------------------------------------
    MidiLearn.h

    MIDI learn: right-click a control, "Learn MIDI", move a controller; from
    then on that CC drives the parameter. The map is part of the state
    (<Structure><MidiLearn>), so it is saved with sessions and presets, and
    can be stored as a machine-wide default that new instances start with.

    The audio thread only looks the CC up in a lock-free table and pushes the
    value into a FIFO; the parameter is set (and the host notified) on the
    message thread, from a timer. While learning, the next CC to arrive is
    pushed the same way and assigned there.

    Author: Olivier Doaré, github.com/odoare
    Dual-licensed, mirroring the JUCE framework it depends on: under the GNU
    AGPL Version 3.0, or under commercial terms available from the author.
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>
#include "StructureState.h"

namespace camshaft
{

class MidiLearn : public juce::ChangeBroadcaster,
                  private juce::Timer,
                  private juce::ChangeListener
{
public:
    MidiLearn (juce::AudioProcessorValueTreeState& apvts, StructureState& structure);
    ~MidiLearn() override;

    //==========================================================================
    // Message thread

    void startLearning (const juce::String& paramID);
    void cancelLearning();
    juce::String getLearningParam() const;

    /** The CC driving `paramID`, -1 when none. */
    int getCcFor (const juce::String& paramID) const;
    void forget (const juce::String& paramID);

    void saveAsDefault();
    void clearDefault();

    /** A fresh instance (no map in its state yet) takes the default map. */
    void applyDefaultIfEmpty();

    //==========================================================================
    /** Audio thread: returns true when the CC belongs to the learn map (or
        was taken by learning) and should not drive anything else. */
    bool handleController (int cc, int value) noexcept;

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void rebuildTable();
    void assign (int cc, int paramIndex);

    juce::AudioProcessorValueTreeState& apvts;
    StructureState& structure;
    juce::Array<juce::RangedAudioParameter*> params;

    std::array<std::atomic<int>, 128> ccToParam {};   // index in params, -1 = none
    std::map<juce::String, int> paramToCc;            // message thread
    std::atomic<int> learning { -1 };

    struct Event { int param; int cc; float value; bool learn; };
    juce::AbstractFifo fifo { 512 };
    std::array<Event, 512> events {};
};

} // namespace camshaft
