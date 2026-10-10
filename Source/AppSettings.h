/*
  ------------------------------------------------------------------------------
    AppSettings.h

    Machine-wide settings shared by every Camshaft instance (and the
    standalone app), in FX-Mechanics/Camshaft.settings under the user's
    application-data folder (~/.config on Linux, ~/Library/Application
    Support on macOS, %APPDATA% on Windows): whether the hover help is shown,
    the default MIDI learn map, and the curve editor's beat grid (whether
    the points of a curve timed in beats snap to it, and its division).

    Header-only, one shared PropertiesFile in a function-local static.
    Message thread.

    Author: Olivier Doaré, github.com/odoare
    Dual-licensed, mirroring the JUCE framework it depends on: under the GNU
    AGPL Version 3.0, or under commercial terms available from the author.
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>

namespace camshaft
{

inline juce::ApplicationProperties& appProperties()
{
    static juce::ApplicationProperties props;
    static const bool inited = []
    {
        juce::PropertiesFile::Options o;
        o.applicationName     = "Camshaft";
        o.filenameSuffix      = "settings";
       #if JUCE_LINUX || JUCE_BSD
        o.folderName          = ".config/FX-Mechanics";   // JUCE puts it under ~ itself on Linux
       #else
        o.folderName          = "FX-Mechanics";           // already under the application data folder
       #endif
        o.osxLibrarySubFolder = "Application Support";
        props.setStorageParameters (o);
        return true;
    }();
    juce::ignoreUnused (inited);
    return props;
}

/** Whether the hover help is shown (on by default). */
inline bool getUiTooltips()
{
    auto* s = appProperties().getUserSettings();
    return s == nullptr || s->getBoolValue ("uiTooltips", true);
}

inline void setUiTooltips (bool on)
{
    if (auto* s = appProperties().getUserSettings())
    {
        s->setValue ("uiTooltips", on);
        s->saveIfNeeded();
    }
}

/** The curve editor's beat grid divisions (for a curve whose duration is
    in beats), in beats. Stored by value, so the list may change. */
struct CurveGridDivision
{
    const char* name;
    double beats;
};

inline constexpr CurveGridDivision curveGridDivisions[] = {
    { "1 bar (4 beats)", 4.0 },
    { "1 beat", 1.0 },
    { "1/2 beat", 0.5 },
    { "1/3 beat", 1.0 / 3.0 },
    { "1/4 beat", 0.25 },
    { "1/6 beat", 1.0 / 6.0 },
    { "1/8 beat", 0.125 },
};

/** Whether the points of a curve timed in beats snap to the beat grid (off
    by default; the grid is drawn either way). */
inline bool getCurveGridSnap()
{
    auto* s = appProperties().getUserSettings();
    return s != nullptr && s->getBoolValue ("curveGridSnap", false);
}

inline void setCurveGridSnap (bool on)
{
    if (auto* s = appProperties().getUserSettings())
    {
        s->setValue ("curveGridSnap", on);
        s->saveIfNeeded();
    }
}

/** The beat grid's division, in beats (a quarter of a beat by default). */
inline double getCurveGridBeats()
{
    auto* s = appProperties().getUserSettings();
    const double b = s != nullptr ? s->getDoubleValue ("curveGridBeats", 0.25) : 0.25;
    return b > 0.0 ? b : 0.25;
}

inline void setCurveGridBeats (double beats)
{
    if (auto* s = appProperties().getUserSettings())
    {
        s->setValue ("curveGridBeats", beats);
        s->saveIfNeeded();
    }
}

/** The default MIDI learn map (an XML <MidiLearn> element), or null. */
inline std::unique_ptr<juce::XmlElement> getDefaultMidiLearn()
{
    if (auto* s = appProperties().getUserSettings())
        return s->getXmlValue ("midiLearnDefault");
    return {};
}

inline void setDefaultMidiLearn (const juce::XmlElement* map)
{
    if (auto* s = appProperties().getUserSettings())
    {
        if (map != nullptr)
            s->setValue ("midiLearnDefault", map);
        else
            s->removeValue ("midiLearnDefault");
        s->saveIfNeeded();
    }
}

} // namespace camshaft
