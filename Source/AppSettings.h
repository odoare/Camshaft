/*
  ------------------------------------------------------------------------------
    AppSettings.h

    Machine-wide settings shared by every Camshaft instance (and the
    standalone app), in FX-Mechanics/Camshaft.settings under the user's
    application-data folder (~/.config on Linux, ~/Library/Application
    Support on macOS, %APPDATA% on Windows): whether the hover help is shown,
    and the default MIDI learn map.

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
