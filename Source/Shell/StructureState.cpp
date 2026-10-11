/*
  ------------------------------------------------------------------------------
    StructureState.cpp

    See StructureState.h.

    Author: Olivier Doaré, github.com/odoare
    Dual-licensed, mirroring the JUCE framework it depends on: under the GNU
    AGPL Version 3.0, or under commercial terms available from the author.
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
  ------------------------------------------------------------------------------
*/

#include "StructureState.h"
#include "ParamIDs.h"
#include <FxmeTools/synth/ModGraph.h>

namespace camshaft
{

using namespace cam;

const juce::Identifier StructureState::structureType ("Structure");
const juce::Identifier StructureState::routesType ("Routes");
const juce::Identifier StructureState::routeType ("Route");
const juce::Identifier StructureState::beltType ("Belt");
const juce::Identifier StructureState::linkType ("Link");
const juce::Identifier StructureState::curvesType ("Curves");
const juce::Identifier StructureState::curveType ("Curve");
const juce::Identifier StructureState::ccType ("CcSources");
const juce::Identifier StructureState::learnType ("MidiLearn");
const juce::Identifier StructureState::mapType ("Map");
const juce::Identifier StructureState::locksType ("Locks");
const juce::Identifier StructureState::lockType ("Lock");
const juce::Identifier StructureState::settingsType ("Settings");
const juce::Identifier StructureState::camsType ("Cams");
const juce::Identifier StructureState::camType ("Cam");
const juce::Identifier StructureState::versionProperty ("schemaVersion");

namespace
{
    int xmodFromName (const juce::String& s)
    {
        for (int i = 1; i < numXmodTypes; ++i)
            if (s == xmodTypes[i])
                return i;
        return 0;
    }
}

StructureState::StructureState (juce::AudioProcessorValueTreeState& state, CamEngine& e, const Destinations& d)
    : apvts (state), engine (e), destinations (d)
{
    apvts.state.addListener (this);
    getRoot();
    stampVersion();
    refreshCaches();
    pushAllToEngine();
}

StructureState::~StructureState()
{
    apvts.state.removeListener (this);
}

void StructureState::valueTreeRedirected (juce::ValueTree&)
{
    // A session or a preset replaced the whole state.
    auto state = apvts.state;
    migrate (state);
    getRoot();
    refreshCaches();
    pushAllToEngine();
    sendChangeMessage();
}

//==============================================================================
juce::ValueTree StructureState::getRoot() const
{
    auto state = apvts.state;
    auto root = state.getOrCreateChildWithName (structureType, nullptr);
    for (const auto* t : { &routesType, &beltType, &curvesType, &ccType, &learnType, &locksType, &settingsType, &camsType })
        root.getOrCreateChildWithName (*t, nullptr);

    // The sample player's markers, reserved in schema 1 and not used yet.
    auto cams = root.getChildWithName (camsType);
    for (int c = 0; c < numCams; ++c)
    {
        if (cams.getChildWithProperty ("index", c).isValid())
            continue;
        juce::ValueTree cam (camType);
        cam.setProperty ("index", c, nullptr);
        cam.setProperty ("mode", "wavetable", nullptr);
        cam.setProperty ("attack", 0, nullptr);
        cam.setProperty ("loopStart", 0, nullptr);
        cam.setProperty ("loopEnd", 0, nullptr);
        cam.setProperty ("release", 0, nullptr);
        cam.setProperty ("rootKey", 60, nullptr);
        cams.appendChild (cam, nullptr);
    }
    return root;
}

juce::ValueTree StructureState::getSection (const juce::Identifier& type) const
{
    return getRoot().getChildWithName (type);
}

void StructureState::stampVersion()
{
    apvts.state.setProperty (versionProperty, schemaVersion, nullptr);
}

void StructureState::migrate (juce::ValueTree& state)
{
    // A missing version is a pre-release session, read as 1. Migrations
    // run in order: if (version < n) { ...; version = n; } ...
    int version = (int) state.getProperty (versionProperty, 1);
    if (version > schemaVersion)
        return;   // written by a newer build: leave what we do not know untouched

    if (version < 2)
    {
        // Schema 2: a cam's range is set by its opening and duration
        // (r<n>_start, r<n>_length) instead of its centre and width
        // (r<n>_center, r<n>_width). The width carries over as the duration;
        // the centre becomes the opening (exact would need the recording's
        // length, unknown here: the range moves by half its width).
        auto renamed = [] (const juce::String& id)
        {
            const auto key = id.fromFirstOccurrenceOf ("_", false, false);
            if (! id.startsWith ("r") || ! id.contains ("_") || (key != "center" && key != "width"))
                return juce::String();
            return id.upToFirstOccurrenceOf ("_", true, false) + (key == "center" ? "start" : "length");
        };
        for (auto child : state)
            if (child.hasType ("PARAM"))
                if (const auto to = renamed (child.getProperty ("id").toString()); to.isNotEmpty())
                    child.setProperty ("id", to, nullptr);
        for (auto route : state.getChildWithName (structureType).getChildWithName (routesType))
            if (const auto to = renamed (route.getProperty ("dst").toString()); to.isNotEmpty())
                route.setProperty ("dst", to, nullptr);
        version = 2;
    }

    if (version < 3)
    {
        // Schema 3: the IDs follow the names, r<n>_start -> r<n>_opening and
        // r<n>_length -> r<n>_duration (same values, exact).
        auto renamed = [] (const juce::String& id)
        {
            const auto key = id.fromFirstOccurrenceOf ("_", false, false);
            if (! id.startsWith ("r") || ! id.contains ("_") || (key != "start" && key != "length"))
                return juce::String();
            return id.upToFirstOccurrenceOf ("_", true, false) + (key == "start" ? "opening" : "duration");
        };
        for (auto child : state)
            if (child.hasType ("PARAM"))
                if (const auto to = renamed (child.getProperty ("id").toString()); to.isNotEmpty())
                    child.setProperty ("id", to, nullptr);
        for (auto route : state.getChildWithName (structureType).getChildWithName (routesType))
            if (const auto to = renamed (route.getProperty ("dst").toString()); to.isNotEmpty())
                route.setProperty ("dst", to, nullptr);
        version = 3;
    }

    state.setProperty (versionProperty, version, nullptr);
}

void StructureState::pushAllToEngine()
{
    for (int i = 0; i < maxRoutes; ++i)
        pushRoute (i);

    for (int s = 0; s < numCams; ++s)
        for (int d = 0; d < numCams; ++d)
            engine.setXmod (s, d, getXmod (s, d));

    for (int m = 0; m < numModulators; ++m)
        pushCurve (m);

    for (int i = 0; i < numCcSources; ++i)
        engine.setCcNumber (i, getCcNumber (i));
}

//==============================================================================
void StructureState::refreshCaches()
{
    for (int i = 0; i < maxRoutes; ++i)
        routeCache[(size_t) i] = readRoute (i);
    ++routesVersion;

    lockCache.clear();
    for (const auto& lock : getSection (locksType))
        lockCache.insert (lock.getProperty ("param").toString());
}

StructureState::Route StructureState::getRoute (int i) const
{
    return juce::isPositiveAndBelow (i, maxRoutes) ? routeCache[(size_t) i] : Route();
}

StructureState::Route StructureState::readRoute (int i) const
{
    Route r;
    const auto node = getSection (routesType).getChildWithProperty ("i", i);
    if (node.isValid())
    {
        r.source = ids::sourceFromKey (node.getProperty ("src").toString());
        r.dest = node.getProperty ("dst").toString();
        r.via = ids::sourceFromKey (node.getProperty ("via").toString());
    }
    return r;
}

void StructureState::setRoute (int i, const Route& r)
{
    if (i < 0 || i >= maxRoutes)
        return;
    auto routes = getSection (routesType);
    auto node = routes.getChildWithProperty ("i", i);
    if (! r.isUsed())
    {
        if (node.isValid())
            routes.removeChild (node, nullptr);
    }
    else
    {
        if (! node.isValid())
        {
            node = juce::ValueTree (routeType);
            node.setProperty ("i", i, nullptr);
            routes.appendChild (node, nullptr);
        }
        node.setProperty ("src", ids::sourceKey (r.source), nullptr);
        node.setProperty ("dst", r.dest, nullptr);
        node.setProperty ("via", ids::sourceKey (r.via), nullptr);
    }
    routeCache[(size_t) i] = r.isUsed() ? r : Route();
    ++routesVersion;
    pushRoute (i);

    // Routing from (or through) a modulator that is off switches it on, as
    // an LFO: a connection to a silent source would look broken.
    if (r.isUsed())
        for (int s : { r.source, r.via })
            if (s >= srcModFirst && s < srcModFirst + numModulators)
                if (auto* type = apvts.getParameter (ids::mod (s - srcModFirst, mfType)))
                    if ((int) apvts.getRawParameterValue (ids::mod (s - srcModFirst, mfType))->load() == mtOff)
                        type->setValueNotifyingHost (type->convertTo0to1 ((float) mtLfo));

    sendChangeMessage();
}

void StructureState::clearRoute (int i)
{
    setRoute (i, {});
    if (auto* p = apvts.getParameter (ids::routeDepth (i)))
        p->setValueNotifyingHost (p->getDefaultValue());
}

void StructureState::pushRoute (int i)
{
    const auto r = getRoute (i);
    const int flat = r.isUsed() ? destinations.flatFor (r.dest) : -1;
    engine.setRoute (i, r.source, flat, r.via);
}

int StructureState::firstFreeRoute() const
{
    for (int i = 0; i < maxRoutes; ++i)
        if (! getRoute (i).isUsed())
            return i;
    return -1;
}

int StructureState::findRoute (int source, const juce::String& dest) const
{
    for (int i = 0; i < maxRoutes; ++i)
    {
        const auto r = getRoute (i);
        if (r.source == source && r.dest == dest)
            return i;
    }
    return -1;
}

int StructureState::addRoute (int source, const juce::String& dest, float depth)
{
    int i = findRoute (source, dest);
    if (i < 0)
        i = firstFreeRoute();
    if (i < 0)
        return -1;

    Route r;
    r.source = source;
    r.dest = dest;
    setRoute (i, r);

    if (auto* p = apvts.getParameter (ids::routeDepth (i)))
        p->setValueNotifyingHost (p->convertTo0to1 (depth));
    return i;
}

juce::Array<int> StructureState::routesTo (const juce::String& paramID) const
{
    juce::Array<int> result;
    for (int i = 0; i < maxRoutes; ++i)
        if (getRoute (i).dest == paramID)
            result.add (i);
    return result;
}

//==============================================================================
int StructureState::getXmod (int src, int dst) const
{
    const auto belt = getSection (beltType);
    for (const auto& link : belt)
        if ((int) link.getProperty ("src", -1) == src && (int) link.getProperty ("dst", -1) == dst)
            return xmodFromName (link.getProperty ("type").toString());
    return 0;
}

bool StructureState::canConnect (int src, int dst) const
{
    if (src == dst)
        return true;
    fxme::ModGraph graph (numCams);
    for (int s = 0; s < numCams; ++s)
        for (int d = 0; d < numCams; ++d)
            if (s != d && getXmod (s, d) != 0 && ! (s == src && d == dst))
                graph.connect (s, d);
    return graph.canConnect (src, dst);
}

bool StructureState::setXmod (int src, int dst, int type)
{
    if (src < 0 || src >= numCams || dst < 0 || dst >= numCams)
        return false;
    if (type != 0 && src == dst && type != 1)
        return false;                                  // only FM may feed back
    if (type != 0 && ! canConnect (src, dst))
        return false;

    auto belt = getSection (beltType);
    for (int i = belt.getNumChildren(); --i >= 0;)
    {
        const auto link = belt.getChild (i);
        if ((int) link.getProperty ("src", -1) == src && (int) link.getProperty ("dst", -1) == dst)
            belt.removeChild (i, nullptr);
    }
    if (type != 0)
    {
        juce::ValueTree link (linkType);
        link.setProperty ("src", src, nullptr);
        link.setProperty ("dst", dst, nullptr);
        link.setProperty ("type", xmodTypes[juce::jlimit (0, numXmodTypes - 1, type)], nullptr);
        belt.appendChild (link, nullptr);
    }
    engine.setXmod (src, dst, type);
    sendChangeMessage();
    return true;
}

//==============================================================================
juce::String StructureState::curveToString (const fxme::CurveShape& s)
{
    juce::StringArray parts;
    for (int i = 0; i < s.numPoints; ++i)
        parts.add (juce::String (s.x[(size_t) i], 5) + "," + juce::String (s.y[(size_t) i], 5) + ","
                   + juce::String (s.curve[(size_t) i], 4));
    return parts.joinIntoString (";");
}

fxme::CurveShape StructureState::curveFromString (const juce::String& text)
{
    fxme::CurveShape s;
    const auto parts = juce::StringArray::fromTokens (text, ";", "");
    int n = 0;
    for (const auto& p : parts)
    {
        const auto v = juce::StringArray::fromTokens (p, ",", "");
        if (v.size() < 2 || n >= fxme::CurveShape::maxPoints)
            continue;
        s.x[(size_t) n] = v[0].getFloatValue();
        s.y[(size_t) n] = v[1].getFloatValue();
        s.curve[(size_t) n] = v.size() > 2 ? v[2].getFloatValue() : 0.0f;
        ++n;
    }
    if (n >= 2)
        s.numPoints = n;
    s.sanitise();
    return s;
}

fxme::CurveShape StructureState::defaultCurve()
{
    fxme::CurveShape s;
    s.numPoints = 3;
    s.x[0] = 0.0f;  s.y[0] = 0.0f; s.curve[0] = -0.4f;
    s.x[1] = 0.25f; s.y[1] = 1.0f; s.curve[1] = 0.3f;
    s.x[2] = 1.0f;  s.y[2] = 0.0f; s.curve[2] = 0.0f;
    s.sustainStart = 1;
    s.sustainEnd = 1;
    s.sanitise();
    return s;
}

fxme::CurveShape StructureState::getCurve (int m) const
{
    const auto node = getSection (curvesType).getChildWithProperty ("mod", m);
    if (! node.isValid())
        return defaultCurve();

    auto s = curveFromString (node.getProperty ("points").toString());
    s.sustainEnabled = (bool) node.getProperty ("sustain", false);
    s.sustainStart = (int) node.getProperty ("sStart", 0);
    s.sustainEnd = (int) node.getProperty ("sEnd", 0);
    s.sustainLoop = (bool) node.getProperty ("loop", true);
    s.sustainPingPong = (bool) node.getProperty ("pingpong", false);
    s.releaseEnabled = (bool) node.getProperty ("release", false);
    s.sanitise();
    return s;
}

void StructureState::setCurve (int m, const fxme::CurveShape& shape)
{
    auto curves = getSection (curvesType);
    auto node = curves.getChildWithProperty ("mod", m);
    if (! node.isValid())
    {
        node = juce::ValueTree (curveType);
        node.setProperty ("mod", m, nullptr);
        curves.appendChild (node, nullptr);
    }
    node.setProperty ("points", curveToString (shape), nullptr);
    node.setProperty ("sustain", shape.sustainEnabled, nullptr);
    node.setProperty ("sStart", shape.sustainStart, nullptr);
    node.setProperty ("sEnd", shape.sustainEnd, nullptr);
    node.setProperty ("loop", shape.sustainLoop, nullptr);
    node.setProperty ("pingpong", shape.sustainPingPong, nullptr);
    node.setProperty ("release", shape.releaseEnabled, nullptr);
    pushCurve (m);
}

void StructureState::pushCurve (int m)
{
    engine.curve (m).write (getCurve (m));
}

//==============================================================================
int StructureState::getCcNumber (int i) const
{
    return (int) getSection (ccType).getProperty ("cc" + juce::String (i + 1), 20 + i);
}

void StructureState::setCcNumber (int i, int cc)
{
    getSection (ccType).setProperty ("cc" + juce::String (i + 1), juce::jlimit (0, 127, cc), nullptr);
    engine.setCcNumber (i, juce::jlimit (0, 127, cc));
}

bool StructureState::isLocked (const juce::String& paramID) const
{
    return lockCache.count (paramID) > 0;
}

void StructureState::setLocked (const juce::String& paramID, bool locked)
{
    auto locks = getSection (locksType);
    auto node = locks.getChildWithProperty ("param", paramID);
    if (locked && ! node.isValid())
    {
        juce::ValueTree lock (lockType);
        lock.setProperty ("param", paramID, nullptr);
        locks.appendChild (lock, nullptr);
    }
    else if (! locked && node.isValid())
        locks.removeChild (node, nullptr);
    if (locked) lockCache.insert (paramID); else lockCache.erase (paramID);
    sendChangeMessage();
}

} // namespace camshaft
