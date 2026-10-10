/*
  ------------------------------------------------------------------------------
    StructureState.h

    Camshaft's state that is not a host parameter, kept as a <Structure>
    child of the APVTS state so that it travels with presets and sessions:

      <Structure>
        <Routes>     <Route i="0" src="mod1" dst="r1_start" via="none"/> ...
        <Belt>       <Link src="1" dst="0" type="FM"/> ...       (cross-mod)
        <Curves>     <Curve mod="0" points="x,y,c;..." sustain= sStart= sEnd= loop= pingpong= release=/>
        <CcSources   cc1="20" cc2="21" cc3="22" cc4="23"/>
        <MidiLearn>  <Map cc="74" param="r1_f1_cutoff"/> ...
        <Locks>      <Lock param="r1_volume"/> ...                (randomize locks)
        <Settings/>                                              (reserved)
        <Cams>       <Cam index="0" mode="wavetable" attack="0" loopStart="0"
                          loopEnd="0" release="0" rootKey="60"/> (reserved for the
                                                                  sample player)
      </Structure>

    The route depths are host parameters (route<n>_depth); the rest here is
    structural. Sources are stored by their stable key, destinations by
    parameter ID, so neither depends on an index that could move.

    The state root carries schemaVersion; migrate() upgrades older states in
    order. Children and properties this build does not know are left alone,
    so a round trip through an older build loses nothing.

    Every edit goes through here (message thread) and is pushed to the
    engine at once; a whole new state (session or preset load) is pushed by
    pushAllToEngine(), which runs on the tree's redirection.

    Author: Olivier Doaré, github.com/odoare
    Dual-licensed, mirroring the JUCE framework it depends on: under the GNU
    AGPL Version 3.0, or under commercial terms available from the author.
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>
#include "Engine/CamEngine.h"
#include "Destinations.h"
#include <set>

namespace camshaft
{

class StructureState : public juce::ChangeBroadcaster,
                       private juce::ValueTree::Listener
{
public:
    static constexpr int schemaVersion = 2;

    StructureState (juce::AudioProcessorValueTreeState& apvts, cam::CamEngine& engine, const Destinations& destinations);
    ~StructureState() override;

    //==========================================================================
    // Modulation routes

    struct Route
    {
        int source = 0;               // cam::ModSource
        juce::String dest;            // parameter ID ("" = unused)
        int via = 0;

        bool isUsed() const noexcept { return source > 0 && dest.isNotEmpty(); }
    };

    Route getRoute (int i) const;
    void setRoute (int i, const Route& r);
    void clearRoute (int i);

    /** The first unused route, -1 when all 64 are taken. */
    int firstFreeRoute() const;

    /** The route from `source` to `dest`, or -1. */
    int findRoute (int source, const juce::String& dest) const;

    /** Adds (or finds) source -> dest and sets its depth; returns its index
        or -1 when the matrix is full. */
    int addRoute (int source, const juce::String& dest, float depth);

    /** The routes into `paramID`. */
    juce::Array<int> routesTo (const juce::String& paramID) const;

    /** Changes whenever a route changes (or a whole state is loaded): a
        display can keep what it derived from the routes until it moves. */
    int getRoutesVersion() const noexcept { return routesVersion; }

    //==========================================================================
    // Timing belt (cross-modulation between cams)

    int getXmod (int src, int dst) const;
    /** False when src -> dst would close a loop (feedback onto itself is allowed, FM only). */
    bool canConnect (int src, int dst) const;
    /** Sets a connection (0 removes it); refuses (returns false) a loop. */
    bool setXmod (int src, int dst, int type);

    //==========================================================================
    // Curves, CC sources, locks, settings

    fxme::CurveShape getCurve (int m) const;
    void setCurve (int m, const fxme::CurveShape& shape);

    int getCcNumber (int i) const;
    void setCcNumber (int i, int cc);

    bool isLocked (const juce::String& paramID) const;
    void setLocked (const juce::String& paramID, bool locked);


    //==========================================================================
    /** The <Structure> child (made, with its sections, if missing). */
    juce::ValueTree getRoot() const;
    juce::ValueTree getSection (const juce::Identifier& type) const;

    /** Pushes routes, belt, curves and CC numbers to the engine. */
    void pushAllToEngine();

    /** Re-reads the caches after the state was edited from outside (and
        tells the listeners). */
    void refreshFromState() { refreshCaches(); pushAllToEngine(); sendChangeMessage(); }

    /** Upgrades a state written by an older schema, in place. */
    static void migrate (juce::ValueTree& state);

    /** Stamps the schema version on the state root (before a save). */
    void stampVersion();

    static juce::String curveToString (const fxme::CurveShape& shape);
    static fxme::CurveShape curveFromString (const juce::String& text);
    static fxme::CurveShape defaultCurve();

    static const juce::Identifier structureType, routesType, routeType, beltType, linkType, curvesType, curveType,
                                  ccType, learnType, mapType, locksType, lockType, settingsType, camsType, camType,
                                  versionProperty;

private:
    void valueTreeRedirected (juce::ValueTree&) override;
    void refreshCaches();
    Route readRoute (int i) const;
    void pushRoute (int i);
    void pushCurve (int m);

    juce::AudioProcessorValueTreeState& apvts;
    cam::CamEngine& engine;
    const Destinations& destinations;

    // Read by the editor many times a second: kept out of the tree.
    std::array<Route, cam::maxRoutes> routeCache;
    int routesVersion = 0;
    std::set<juce::String> lockCache;
};

} // namespace camshaft
